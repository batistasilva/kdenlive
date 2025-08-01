/*
SPDX-FileCopyrightText: 2016 Jean-Baptiste Mardelle <jb@kdenlive.org>
SPDX-FileCopyrightText: 2017 Nicolas Carion
This file is part of Kdenlive. See www.kdenlive.org.

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "profilewidget.h"
#include "kdenlivesettings.h"
#include "profiles/profilemodel.hpp"
#include "profiles/profilerepository.hpp"
#include "profiles/tree/profilefilter.hpp"
#include "profiles/tree/profiletreemodel.hpp"

#include <KLocalizedString>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QSplitter>
#include <QTextEdit>
#include <QTreeView>

ProfileWidget::ProfileWidget(QWidget *parent)
    : QWidget(parent)
    , m_originalProfile(QStringLiteral("invalid"))
{
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    auto *lay = new QVBoxLayout;
    lay->setContentsMargins(0, 0, 0, 0);
    auto *labelLay = new QHBoxLayout;

    // Search bar
    QLineEdit *searchBar = new QLineEdit(this);
    searchBar->setPlaceholderText(i18n("Search…"));
    searchBar->setClearButtonEnabled(true);
    labelLay->addWidget(searchBar);

    auto *fpsLabel = new QLabel(i18n("Fps:"), this);
    m_fpsFilt = new QComboBox(this);
    fpsLabel->setBuddy(m_fpsFilt);
    labelLay->addWidget(fpsLabel);
    labelLay->addWidget(m_fpsFilt);

    auto *scanningLabel = new QLabel(i18nc("@label Scanning as the video method: interlaced or progressive", "Scanning:"), this);
    m_scanningFilt = new QComboBox(this);
    scanningLabel->setBuddy(m_scanningFilt);
    labelLay->addWidget(scanningLabel);
    labelLay->addWidget(m_scanningFilt);
    labelLay->addStretch(1);

    auto *manage_profiles = new QToolButton(this);
    labelLay->addWidget(manage_profiles);
    manage_profiles->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    manage_profiles->setToolTip(i18n("Manage project profiles"));
    manage_profiles->setWhatsThis(
        xi18nc("@info:whatsthis",
               "Opens the profile dialog window in which you can change project profiles. Note: The profile used in the open project cannot be changed."));
    connect(manage_profiles, &QAbstractButton::clicked, this, &ProfileWidget::slotEditProfiles);
    lay->addLayout(labelLay);

    auto *profileSplitter = new QSplitter;

    m_treeView = new QTreeView(this);
    m_treeModel = ProfileTreeModel::construct(this);
    m_filter = new ProfileFilter(this);
    m_filter->setSourceModel(m_treeModel.get());
    m_treeView->setModel(m_filter);

    connect(searchBar, &QLineEdit::textChanged, this, [this](const QString &str) {
        m_filter->slotSetSearchString(str);
        if (str.isEmpty()) {
            // focus last selected item when clearing search line
            trySelectProfile(m_currentProfile);
        }
    });

    for (int i = 1; i < m_treeModel->columnCount(); ++i) {
        m_treeView->hideColumn(i);
    }
    m_treeView->header()->hide();
    QItemSelectionModel *selectionModel = m_treeView->selectionModel();
    connect(selectionModel, &QItemSelectionModel::currentRowChanged, this, &ProfileWidget::slotChangeSelection);
    connect(selectionModel, &QItemSelectionModel::selectionChanged, this, [&](const QItemSelection &selected, const QItemSelection &deselected) {
        QModelIndex current;
        QModelIndex old;
        if (!selected.indexes().isEmpty()) {
            current = selected.indexes().front();
        }
        if (!deselected.indexes().isEmpty()) {
            old = deselected.indexes().front();
        }
        slotChangeSelection(current, old);
    });
    int treeViewFontHeight = QFontInfo(m_treeView->font()).pixelSize();
    m_treeView->setMinimumHeight(treeViewFontHeight);
    profileSplitter->addWidget(m_treeView);
    m_treeView->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    m_descriptionPanel = new QTextEdit(this);
    m_descriptionPanel->setReadOnly(true);
    m_descriptionPanel->viewport()->setCursor(Qt::ArrowCursor);
    m_descriptionPanel->viewport()->setBackgroundRole(QPalette::Mid);
    m_descriptionPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_descriptionPanel->setFrameStyle(QFrame::NoFrame);
    m_descriptionPanel->setMinimumHeight(treeViewFontHeight);
    profileSplitter->addWidget(m_descriptionPanel);

    lay->addWidget(profileSplitter);
    profileSplitter->setStretchFactor(0, 4);
    profileSplitter->setStretchFactor(1, 3);

    refreshFpsCombo();
    auto updateFps = [&]() {
        double current = m_fpsFilt->currentData().toDouble();
        KdenliveSettings::setProfile_fps_filter(m_fpsFilt->currentText());
        m_filter->setFilterFps(current > 0, current);
        slotFilterChanged();
    };
    connect(m_fpsFilt, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), updateFps);
    int ix = m_fpsFilt->findText(KdenliveSettings::profile_fps_filter());
    if (ix > -1) {
        m_fpsFilt->setCurrentIndex(ix);
    }
    m_scanningFilt->addItem(i18n("Any"), -1);
    m_scanningFilt->addItem(i18n("Interlaced"), 0);
    m_scanningFilt->addItem(i18n("Progressive"), 1);

    auto updateScanning = [&]() {
        int current = m_scanningFilt->currentData().toInt();
        KdenliveSettings::setProfile_scanning_filter(m_scanningFilt->currentText());
        m_filter->setFilterInterlaced(current != -1, current == 0);
        slotFilterChanged();
    };
    connect(m_scanningFilt, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), updateScanning);

    ix = m_scanningFilt->findText(KdenliveSettings::profile_scanning_filter());
    if (ix > -1) {
        m_scanningFilt->setCurrentIndex(ix);
    }
    setLayout(lay);
}

ProfileWidget::~ProfileWidget() = default;

void ProfileWidget::refreshFpsCombo()
{
    QLocale locale; // Used for UI → OK
    QVariant currentValue;
    if (m_fpsFilt->count() > 1) {
        // remember last selected value
        currentValue = m_fpsFilt->currentData();
    }
    m_fpsFilt->clear();
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    m_fpsFilt->addItem(i18n("Any"), -1);
    auto all_fps = ProfileRepository::get()->getAllFps();
    for (double fps : std::as_const(all_fps)) {
        m_fpsFilt->addItem(locale.toString(fps), fps);
    }
    if (currentValue.isValid()) {
        int ix = m_fpsFilt->findData(currentValue);
        if (ix > -1) {
            m_fpsFilt->setCurrentIndex(ix);
        }
    }
}

void ProfileWidget::loadProfile(const QString &profile)
{
    auto index = m_treeModel->findProfile(profile);
    if (index.isValid()) {
        m_originalProfile = m_currentProfile = m_lastValidProfile = profile;
        if (!trySelectProfile(profile)) {
            // When loading a profile, ensure it is visible so reset filters if necessary
            m_fpsFilt->setCurrentIndex(0);
            m_scanningFilt->setCurrentIndex(0);
        }
    }
}

const QString ProfileWidget::selectedProfile() const
{
    return m_currentProfile;
}

void ProfileWidget::slotEditProfiles()
{
    auto *w = new ProfilesDialog(ProfileRepository::get()->getProfile(m_currentProfile)->description());
    w->exec();
    if (w->profileTreeChanged()) {
        // Rebuild profiles tree
        m_treeModel.reset();
        m_treeModel = ProfileTreeModel::construct(this);
        m_filter->setSourceModel(m_treeModel.get());
        refreshFpsCombo();
        loadProfile(m_currentProfile);
    }
    delete w;
}

void ProfileWidget::fillDescriptionPanel(const QString &profile_path)
{
    QString description;
    if (profile_path.isEmpty()) {
        description += i18n("No profile selected");
    } else {
        std::unique_ptr<ProfileModel> &profile = ProfileRepository::get()->getProfile(profile_path);

        description += QStringLiteral("<h5>%1</h5>").arg(i18n("Video Settings"));
        description +=
            QStringLiteral("<p style='font-size:small'>%1<br/>")
                .arg(i18n("Frame size: %1 x %2 (%3:%4)", profile->width(), profile->height(), profile->display_aspect_num(), profile->display_aspect_den()));
        description += i18n("Frame rate: %1 fps", profile->fps());
        description += QStringLiteral("<br/>");
        description += i18n("Pixel aspect ratio: %1", profile->sar());
        description += QStringLiteral("<br/>");
        description += i18n("Color space: %1", profile->colorspaceDescription());
        description += QStringLiteral("<br/>");
        description += i18n("Interlaced: %1", profile->progressive() ? i18n("no") : i18n("yes"));
        if (!profile->progressive()) {
            description += QStringLiteral("<br/>");
            description += i18n("Field order: %1", profile->bottom_field_first() ? i18n("Bottom field first") : i18n("Top field first"));
        }
        description += QStringLiteral("</p>");
    }
    m_descriptionPanel->setHtml(description);
}

void ProfileWidget::slotChangeSelection(const QModelIndex &current, const QModelIndex &previous)
{
    auto originalIndex = m_filter->mapToSource(current);
    if (m_treeModel->parent(originalIndex) == QModelIndex()) {
        // in that case, we have selected a category, which we don't want
        QItemSelectionModel *selection = m_treeView->selectionModel();
        selection->select(previous, QItemSelectionModel::Select);
        return;
    }
    m_currentProfile = m_treeModel->getProfile(originalIndex);
    if (!m_currentProfile.isEmpty()) {
        m_lastValidProfile = m_currentProfile;
    }
    if (m_originalProfile != m_currentProfile) {
        Q_EMIT profileChanged();
    }
    fillDescriptionPanel(m_currentProfile);
}

bool ProfileWidget::trySelectProfile(const QString &profile)
{
    auto index = m_treeModel->findProfile(profile);
    if (index.isValid()) {
        // check if element is visible
        if (m_filter->isVisible(index)) {
            // reselect
            QItemSelectionModel *selection = m_treeView->selectionModel();
            selection->select(m_filter->mapFromSource(index), QItemSelectionModel::Select);
            // expand corresponding category
            auto parent = m_treeModel->parent(index);
            m_treeView->expand(m_filter->mapFromSource(parent));
            m_treeView->scrollTo(m_filter->mapFromSource(index), QAbstractItemView::PositionAtCenter);
            return true;
        }
    }
    return false;
}

void ProfileWidget::slotFilterChanged()
{
    // When filtering change, we must check if the current profile is still visible.
    if (!trySelectProfile(m_currentProfile)) {
        // we try to back-up the last valid profile
        if (!trySelectProfile(m_lastValidProfile)) {
            // Everything fails, we don't have any profile
            m_currentProfile = QString();
            Q_EMIT profileChanged();
            fillDescriptionPanel(QString());
        }
    }
}
