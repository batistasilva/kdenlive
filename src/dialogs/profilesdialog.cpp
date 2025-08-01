/*
    SPDX-FileCopyrightText: 2016 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "profilesdialog.h"
#include "core.h"
#include "effects/effectsrepository.hpp"
#include "kdenlivesettings.h"
#include "profiles/profilemodel.hpp"
#include "profiles/profilerepository.hpp"

#include "klocalizedstring.h"
#include <KMessageBox>
#include <KMessageWidget>

#include "kdenlive_debug.h"
#include <QCloseEvent>
#include <QDir>
#include <QStandardPaths>

ProfilesDialog::ProfilesDialog(const QString &profileDescription, QWidget *parent)
    : QDialog(parent)
{
    // ask profile repository for a refresh
    ProfileRepository::get()->refresh();

    m_view.setupUi(this);
    showMessage();

    // Fill colorspace list (see mlt_profile.h)
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(601), 601);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(709), 709);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(240), 240);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(2020), 2020);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(0), 0);

    QStringList profilesFilter;
    profilesFilter << QStringLiteral("*");

    fillList(profileDescription);
    slotUpdateDisplay();
    connectDialog();
}

int ProfilesDialog::gcd(int a, int b)
{
    // Everything divides 0
    if (a == 0) return b;
    if (b == 0) return a;

    // If both numbers are equal
    if (a == b) return a;

    // If a is greater
    if (a > b) return gcd(a - b, b);

    // If b is greater
    return gcd(a, b - a);
}

void ProfilesDialog::connectDialog()
{
    connect(m_view.profiles_list, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [&](int ix) { slotUpdateDisplay(m_view.profiles_list->itemData(ix).toString()); });
    connect(m_view.button_create, &QAbstractButton::clicked, this, &ProfilesDialog::slotCreateProfile);
    connect(m_view.button_save, &QAbstractButton::clicked, this, &ProfilesDialog::slotSaveProfile);
    connect(m_view.button_delete, &QAbstractButton::clicked, this, &ProfilesDialog::slotDeleteProfile);
    connect(m_view.button_default, &QAbstractButton::clicked, this, &ProfilesDialog::slotSetDefaultProfile);
    connect(m_view.description, &QLineEdit::textChanged, this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.frame_num, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.frame_den, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.display_num, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.display_den, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.scanning, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.scanning, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &ProfilesDialog::slotScanningChanged);
    connect(m_view.size_h, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.size_h, &QAbstractSpinBox::editingFinished, this, &ProfilesDialog::slotAdjustHeight);
    m_view.size_h->setSingleStep(2);
    connect(m_view.size_w, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &ProfilesDialog::slotProfileEdited);
    connect(m_view.size_w, &QAbstractSpinBox::editingFinished, this, &ProfilesDialog::slotAdjustWidth);
    m_view.size_w->setSingleStep(2);
}

ProfilesDialog::ProfilesDialog(const QString &profilePath, bool, QWidget *parent)
    : QDialog(parent)
    , m_isCustomProfile(true)
    , m_customProfilePath(profilePath)
{
    m_view.setupUi(this);
    showMessage();

    // Fill colorspace list (see mlt_profile.h)
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(601), 601);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(709), 709);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(240), 240);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(2020), 2020);
    m_view.colorspace->addItem(ProfileRepository::getColorspaceDescription(0), 0);

    QStringList profilesFilter;
    profilesFilter << QStringLiteral("*");

    m_view.button_create->setHidden(true);
    m_view.profiles_list->setHidden(true);
    m_view.button_delete->setHidden(true);
    m_view.button_default->setHidden(true);
    m_view.description->setEnabled(false);

    slotUpdateDisplay(profilePath);
    connectDialog();
}

void ProfilesDialog::slotAdjustWidth()
{
    // A profile's width should always be a multiple of 2
    QSignalBlocker blk(m_view.size_w);
    int val = m_view.size_w->value();
    int correctedWidth = val + (val % 2);
    if (val == correctedWidth) {
        // Ok, no action required, width is a multiple of 2
        showMessage();
    } else {
        m_view.size_w->setValue(correctedWidth);
        showMessage(i18n("Profile width must be a multiple of 2. It was adjusted to %1", correctedWidth));
    }
}

void ProfilesDialog::slotAdjustHeight()
{
    // A profile's height should always be a multiple of 2
    QSignalBlocker blk(m_view.size_h);
    int val = m_view.size_h->value();
    int correctedHeight = val + (val % 2);
    if (val == correctedHeight) {
        // Ok, no action required, height is a multiple of 2
        showMessage();
    } else {
        m_view.size_h->setValue(correctedHeight);
        showMessage(i18n("Profile height must be a multiple of 2. It was adjusted to %1", correctedHeight));
    }
}

void ProfilesDialog::slotScanningChanged(int ix)
{
    m_view.field_order->setEnabled(ix == 0);
    m_view.label_field_order->setEnabled(ix == 0);
    if (ix == 0 && !EffectsRepository::get()->hasInternalEffect(QStringLiteral("avfilter.fieldorder"))) {
        m_view.effect_warning->show();
    } else {
        m_view.effect_warning->hide();
    }
}

void ProfilesDialog::slotProfileEdited()
{
    m_profileIsModified = true;
    // Calculate pixel aspect ratio
    int par_num = m_view.display_num->value() * m_view.size_h->value();
    int par_den = m_view.display_den->value() * m_view.size_w->value();
    int num = gcd(par_num, par_den);
    if (num > 0) {
        par_num /= num;
        par_den /= num;
    }
    m_view.aspect_num->setText(QString::number(par_num));
    m_view.aspect_den->setText(QString::number(par_den));
}

void ProfilesDialog::fillList(const QString &selectedProfile)
{
    m_view.profiles_list->clear();
    // Retrieve the list from the repository
    QVector<QPair<QString, QString>> profiles = ProfileRepository::get()->getAllProfiles();
    for (const auto &p : std::as_const(profiles)) {
        m_view.profiles_list->addItem(p.first, p.second);
    }

    if (!KdenliveSettings::default_profile().isEmpty()) {
        int ix = m_view.profiles_list->findData(KdenliveSettings::default_profile());
        if (ix > -1) {
            m_view.profiles_list->setCurrentIndex(ix);
        } else {
            // Error, profile not found
            qCWarning(KDENLIVE_LOG) << "Project profile not found, disable  editing";
        }
    }
    int ix = m_view.profiles_list->findText(selectedProfile);
    if (ix != -1) {
        m_view.profiles_list->setCurrentIndex(ix);
    }
    m_selectedProfileIndex = m_view.profiles_list->currentIndex();
}

void ProfilesDialog::accept()
{
    if (askForSave()) {
        QDialog::accept();
    }
}

void ProfilesDialog::reject()
{
    if (askForSave()) {
        QDialog::reject();
    }
}

void ProfilesDialog::closeEvent(QCloseEvent *event)
{
    if (askForSave()) {
        event->accept();
    } else {
        event->ignore();
    }
}

bool ProfilesDialog::askForSave()
{
    if (!m_profileIsModified) {
        return true;
    }
    if (KMessageBox::questionTwoActions(this, i18n("The custom profile was modified, do you want to save it?"), {}, KStandardGuiItem::save(),
                                        KStandardGuiItem::discard()) != KMessageBox::PrimaryAction) {
        return true;
    }
    return slotSaveProfile();
}

void ProfilesDialog::slotCreateProfile()
{
    m_view.button_delete->setEnabled(false);
    m_view.button_create->setEnabled(false);
    m_view.button_save->setEnabled(true);
    m_view.properties->setEnabled(true);
    m_view.description->blockSignals(true);
    m_view.description->setText(m_view.description->text() + " " + i18n("(copy)"));
    m_view.description->blockSignals(false);
}

void ProfilesDialog::slotSetDefaultProfile()
{
    if (m_profileIsModified) {
        showMessage(i18n("Save your profile before setting it to default"));
        return;
    }
    int ix = m_view.profiles_list->currentIndex();
    QString path = m_view.profiles_list->itemData(ix).toString();
    if (!path.isEmpty()) {
        KdenliveSettings::setDefault_profile(path);
    }
}

bool ProfilesDialog::slotSaveProfile()
{
    slotAdjustWidth();

    if (!m_customProfilePath.isEmpty()) {
        saveProfile(m_customProfilePath);
        return true;
    }
    const QString profileDesc = m_view.description->text();
    int ix = m_view.profiles_list->findText(profileDesc);
    if (ix != -1) {
        // this profile name already exists
        const QString path = m_view.profiles_list->itemData(ix).toString();
        if (!path.contains(QLatin1Char('/'))) {
            KMessageBox::error(
                this, i18n("A profile with same name already exists in MLT's default profiles, please choose another description for your custom profile."));
            return false;
        }
        saveProfile(path);
    } else {
        int i = 0;
        QDir dir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/profiles/"));
        if (!dir.exists()) {
            dir.mkpath(QStringLiteral("."));
        }
        QString customName = QStringLiteral("customprofile");
        QString profilePath = dir.absoluteFilePath(customName + QString::number(i));
        while (QFile::exists(profilePath)) {
            ++i;
            profilePath = dir.absoluteFilePath(customName + QString::number(i));
        }
        saveProfile(profilePath);
    }
    m_profileIsModified = false;
    fillList(profileDesc);
    m_view.button_create->setEnabled(true);
    m_profilesChanged = true;
    return true;
}

void ProfilesDialog::saveProfile(const QString &path)
{
    std::unique_ptr<ProfileParam> profile(new ProfileParam(pCore->getCurrentProfile().get()));
    profile->m_description = m_view.description->text();
    profile->m_frame_rate_num = m_view.frame_num->value();
    profile->m_frame_rate_den = m_view.frame_den->value();
    profile->m_width = m_view.size_w->value();
    profile->m_height = m_view.size_h->value();
    profile->m_progressive = m_view.scanning->currentIndex() == 1;
    profile->m_bottom_field_first = m_view.field_order->currentIndex() == 1;
    profile->m_sample_aspect_num = m_view.aspect_num->text().toInt();
    profile->m_sample_aspect_den = m_view.aspect_den->text().toInt();
    profile->m_display_aspect_num = m_view.display_num->value();
    profile->m_display_aspect_den = m_view.display_den->value();
    int colorSpace = m_view.colorspace->itemData(m_view.colorspace->currentIndex()).toInt();
    if (colorSpace == 0) {
        colorSpace = 709;
    }
    profile->m_colorspace = colorSpace;
    ProfileRepository::get()->saveProfile(profile.get(), path);
}

void ProfilesDialog::slotDeleteProfile()
{
    const QString path = m_view.profiles_list->itemData(m_view.profiles_list->currentIndex()).toString();
    bool success = ProfileRepository::get()->deleteProfile(path);
    if (success) {
        m_profilesChanged = true;
        fillList();
    }
}

void ProfilesDialog::slotUpdateDisplay(QString currentProfilePath)
{
    qDebug() << "/ / / /UPDATING DISPLAY FOR PROFILE: " << currentProfilePath;
    if (!askForSave()) {
        m_view.profiles_list->blockSignals(true);
        m_view.profiles_list->setCurrentIndex(m_selectedProfileIndex);
        m_view.profiles_list->blockSignals(false);
        return;
    }
    QLocale locale; // Used for UI output → OK
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    m_selectedProfileIndex = m_view.profiles_list->currentIndex();
    if (currentProfilePath.isEmpty()) {
        currentProfilePath = m_view.profiles_list->itemData(m_view.profiles_list->currentIndex()).toString();
    }
    m_isCustomProfile = currentProfilePath.contains(QLatin1Char('/'));
    // Don't allow editing of the current Project, since this produces crashes at the moment
    bool isCurrentlyUsed = pCore->getCurrentProfilePath() == currentProfilePath;
    showMessage(isCurrentlyUsed ? i18n("The profile of the current project cannot be edited while the project is open.") : QString());
    m_view.button_create->setEnabled(true);
    m_view.button_delete->setEnabled(m_isCustomProfile && !isCurrentlyUsed);
    m_view.properties->setEnabled(m_isCustomProfile && !isCurrentlyUsed);
    m_view.button_save->setEnabled(m_isCustomProfile && !isCurrentlyUsed);
    std::unique_ptr<ProfileModel> &curProfile = ProfileRepository::get()->getProfile(currentProfilePath);
    m_view.description->setText(curProfile->description());
    m_view.size_w->setValue(curProfile->width());
    m_view.size_h->setValue(curProfile->height());
    m_view.aspect_num->setText(QString::number(curProfile->sample_aspect_num()));
    m_view.aspect_den->setText(QString::number(curProfile->sample_aspect_den()));
    m_view.display_num->setValue(curProfile->display_aspect_num());
    m_view.display_den->setValue(curProfile->display_aspect_den());
    m_view.frame_num->setValue(curProfile->frame_rate_num());
    m_view.frame_den->setValue(curProfile->frame_rate_den());
    m_view.scanning->setCurrentIndex(curProfile->progressive() ? 1 : 0);
    m_view.field_order->setCurrentIndex(curProfile->bottom_field_first() ? 1 : 0);
    slotScanningChanged(m_view.scanning->currentIndex());
    if (curProfile->progressive() != 0) {
        m_view.fields->setText(locale.toString(double(curProfile->frame_rate_num() / curProfile->frame_rate_den()), 'f', 2));
    } else {
        m_view.fields->setText(locale.toString(2.0 * curProfile->frame_rate_num() / curProfile->frame_rate_den(), 'f', 2));
    }

    int colorix = m_view.colorspace->findData(curProfile->colorspace());
    if (colorix > -1) {
        m_view.colorspace->setCurrentIndex(colorix);
    }
    m_profileIsModified = false;
}

bool ProfilesDialog::profileTreeChanged() const
{
    return m_profilesChanged;
}

void ProfilesDialog::showMessage(const QString &text, KMessageWidget::MessageType type)
{
    if (text.isEmpty()) {
        m_view.info_message->hide();
    } else {
        m_view.info_message->setText(text);
        m_view.info_message->setMessageType(type);
        m_view.info_message->animatedShow();
    }
}
