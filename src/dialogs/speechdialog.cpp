/*
    SPDX-FileCopyrightText: 2021 Jean-Baptiste Mardelle <jb@kdenlive.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "speechdialog.h"

#include "bin/model/subtitlemodel.hpp"
#include "core.h"
#include "kdenlive_debug.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "pythoninterfaces/speechtotextvosk.h"
#include "pythoninterfaces/speechtotextwhisper.h"

#include "mlt++/MltConsumer.h"
#include "mlt++/MltProfile.h"
#include "mlt++/MltTractor.h"

#include <KLocalizedString>
#include <KMessageBox>
#include <KMessageWidget>
#include <QButtonGroup>
#include <QDir>
#include <QFontDatabase>
#include <QProcess>

#include <memory>
#include <utility>

SpeechDialog::SpeechDialog(std::shared_ptr<TimelineItemModel> timeline, QPoint zone, int tid, bool, bool, QWidget *parent)
    : QDialog(parent)
    , m_timeline(timeline)
    , m_zone(zone)
    , m_tid(-1)

{
    setFont(QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont));
    setupUi(this);
    speech_info->setWordWrap(true);
    speech_info->hide();
    logOutput->setVisible(false);
    setWindowTitle(i18n("Automatic Subtitling"));
    m_speechConfig = new QAction(i18n("Configure"), this);
    connect(m_speechConfig, &QAction::triggered, this, [this]() {
        pCore->window()->slotShowPreferencePage(Kdenlive::PageSpeech);
        close();
    });
    m_logAction = new QAction(i18n("Show log"), this);
    connect(m_logAction, &QAction::triggered, this,
            [this]() { KMessageBox::detailedError(QApplication::activeWindow(), i18n("Speech Recognition log"), m_errorLog); });
    maxChars->setValue(KdenliveSettings::whisperMaxChars());
    check_maxchars->setChecked(KdenliveSettings::cutWhisperMaxChars());

    if (KdenliveSettings::speechEngine() == QLatin1String("whisper")) {
        // Whisper model
        m_stt = new SpeechToTextWhisper(this);
    } else {
        // Vosk model
        whisper_settings->setVisible(false);
        m_stt = new SpeechToTextVosk(this);
    }
    const QStringList speechModels = m_stt->getInstalledModels();
    buildSpeechModelsList(m_stt->engineType(), speechModels);
    connect(pCore.get(), &Core::speechModelUpdate, this, &SpeechDialog::buildSpeechModelsList);
    buttonBox->button(QDialogButtonBox::Apply)->setText(i18n("Process"));
    adjustSize();

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->addButton(timeline_full, 1);
    m_buttonGroup->addButton(timeline_zone, 2);
    m_buttonGroup->addButton(timeline_track, 3);
    m_buttonGroup->addButton(timeline_clips, 4);
    connect(m_buttonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this,
            [=, selectedTrack = tid, sourceZone = zone, bg = m_buttonGroup](QAbstractButton *button) {
                if (speech_info->messageType() == KMessageWidget::Information) {
                    speech_info->animatedHide();
                }
                KdenliveSettings::setSubtitleMode(bg->checkedId());
                if (speech_model->count() > 0) {
                    buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
                }
                if (button == timeline_full) {
                    m_tid = -1;
                    m_zone = QPoint(0, pCore->projectDuration() - 1);
                } else if (button == timeline_clips) {
                    std::unordered_set<int> selection = timeline->getCurrentSelection();
                    int cid = -1;
                    m_tid = -1;
                    int firstPos = -1;
                    for (const auto &s : selection) {
                        // Find first clip
                        if (!timeline->isClip(s)) {
                            continue;
                        }
                        int pos = timeline->getClipPosition(s);
                        if (firstPos == -1 || pos < firstPos) {
                            cid = s;
                            firstPos = pos;
                            m_tid = timeline->getClipTrackId(cid);
                            if (!timeline->isAudioTrack(m_tid)) {
                                m_tid = timeline->getMirrorAudioTrackId(m_tid);
                            }
                        }
                    }
                    if (m_tid == -1) {
                        speech_info->setMessageType(KMessageWidget::Information);
                        speech_info->setText(i18n("No audio track available for selected clip"));
                        speech_info->animatedShow();
                        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
                        return;
                    }
                    if (timeline->isClip(cid)) {
                        m_zone.setX(timeline->getClipPosition(cid));
                        m_zone.setY(m_zone.x() + timeline->getClipPlaytime(cid));
                    } else {
                        speech_info->setMessageType(KMessageWidget::Information);
                        speech_info->setText(i18n("Select a clip in timeline to perform analysis"));
                        speech_info->animatedShow();
                        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
                    }
                } else {
                    if (button == timeline_track) {
                        m_tid = selectedTrack;
                        if (timeline->isSubtitleTrack(m_tid)) {
                            m_tid = -1;
                        } else if (!timeline->isAudioTrack(m_tid)) {
                            m_tid = timeline->getMirrorAudioTrackId(m_tid);
                        }
                        if (m_tid == -1) {
                            speech_info->setMessageType(KMessageWidget::Information);
                            speech_info->setText(i18n("No audio track found"));
                            speech_info->animatedShow();
                            buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
                        }
                    } else {
                        m_tid = -1;
                    }
                    m_zone = sourceZone;
                }
            });
    QAbstractButton *button = m_buttonGroup->button(KdenliveSettings::subtitleMode());
    if (button) {
        button->setChecked(true);
        Q_EMIT m_buttonGroup->buttonClicked(button);
    }
    connect(speech_model, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this]() {
        if (KdenliveSettings::speechEngine() == QLatin1String("whisper")) {
            const QString modelName = speech_model->currentData().toString();
            KdenliveSettings::setWhisperModel(modelName);
            speech_language->setEnabled(!modelName.endsWith(QLatin1String(".en")));
            translate_box->setEnabled(modelName != QLatin1String("turbo"));
        } else {
            KdenliveSettings::setVosk_srt_model(speech_model->currentText());
        }
    });
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() { slotProcessSpeech(); });
    frame_progress->setVisible(false);
    connect(button_abort, &QToolButton::clicked, this, [this]() {
        if (m_speechJob && m_speechJob->state() == QProcess::Running) {
            m_speechJob->kill();
        }
    });
    if (!KdenliveSettings::speech_system_python()) {
        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
        connect(m_stt, &SpeechToText::installStatusChanged, this, &SpeechDialog::checkDeps);
        qDebug() << "SAM INTERFASCE STATUS: " << m_stt->status();
        if (m_stt->status() == AbstractPythonInterface::Installed) {
            checkDeps();
        } else {
            QTimer::singleShot(200, this, [&]() { m_stt->checkSetup(); });
        }
    }
}

SpeechDialog::~SpeechDialog() {}

void SpeechDialog::checkDeps()
{
    if (m_stt->status() == AbstractPythonInterface::Installed) {
        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
        QList<QAbstractButton *> buttons = m_buttonGroup->buttons();
        for (auto &b : buttons) {
            b->setEnabled(true);
        }
    } else {
        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
        speech_info->setText(i18n("Please configure speech to text."));
        speech_info->addAction(m_speechConfig);
        speech_info->setMessageType(KMessageWidget::Warning);
        speech_info->animatedShow();
        QList<QAbstractButton *> buttons = m_buttonGroup->buttons();
        for (auto &b : buttons) {
            b->setEnabled(false);
        }
    }
    // Only enable subtitle max size if srt_equalizer is found
    bool equalizerAvailable = m_stt->optionalDependencyAvailable(QLatin1String("srt_equalizer"));
    check_maxchars->setEnabled(equalizerAvailable);
    maxChars->setEnabled(equalizerAvailable);
}

void SpeechDialog::buildSpeechModelsList(SpeechToTextEngine::EngineType engine, const QStringList models)
{
    speech_model->clear();
    if (models.isEmpty()) {
        speech_info->addAction(m_speechConfig);
        speech_info->setMessageType(KMessageWidget::Warning);
        speech_info->setText(i18n("Please install speech recognition models"));
        speech_info->show();
        buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
        return;
    }
    speech_info->hide();
    if (engine == SpeechToTextEngine::EngineWhisper) {
        // Whisper
        for (auto &w : models) {
            if (w.isEmpty()) {
                continue;
            }
            QString modelName = w;
            modelName[0] = w.at(0).toUpper();
            speech_model->addItem(modelName, w);
        }
        int ix = speech_model->findData(KdenliveSettings::whisperModel());
        if (ix > -1) {
            speech_model->setCurrentIndex(ix);
        }
        translate_box->setEnabled(KdenliveSettings::whisperModel() != QLatin1String("turbo"));
        translate_seamless->setEnabled(KdenliveSettings::enableSeamless());
        translate_seamless->setChecked(KdenliveSettings::srtSeamlessTranslate());
        connect(translate_seamless, &QCheckBox::toggled, this, [this](bool toggled) {
            translate_box->setVisible(!toggled);
            seamless_in->setVisible(toggled);
            seamless_out->setVisible(toggled);
            seamless_in_label->setVisible(toggled);
            seamless_out_label->setVisible(toggled);
        });
        if (KdenliveSettings::enableSeamless()) {
            fillSeamlessLanguages();
        }
        bool seamlessEnabled = translate_seamless->isChecked();
        translate_box->setVisible(!seamlessEnabled);
        seamless_in->setVisible(seamlessEnabled);
        seamless_out->setVisible(seamlessEnabled);
        seamless_in_label->setVisible(seamlessEnabled);
        seamless_out_label->setVisible(seamlessEnabled);
        if (speech_language->count() == 0) {
            // Fill whisper languages
            QMap<QString, QString> languages = m_stt->speechLanguages();
            QMapIterator<QString, QString> j(languages);
            while (j.hasNext()) {
                j.next();
                speech_language->addItem(j.key(), j.value());
            }
            int ix = speech_language->findData(KdenliveSettings::whisperLanguage());
            if (ix > -1) {
                speech_language->setCurrentIndex(ix);
            }
        }
        speech_language->setEnabled(!KdenliveSettings::whisperModel().endsWith(QLatin1String(".en")));
        translate_box->setChecked(KdenliveSettings::whisperTranslate());
    } else {
        // Vosk
        speech_model->addItems(models);
        if (!KdenliveSettings::vosk_srt_model().isEmpty() && models.contains(KdenliveSettings::vosk_srt_model())) {
            int ix = speech_model->findText(KdenliveSettings::vosk_srt_model());
            if (ix > -1) {
                speech_model->setCurrentIndex(ix);
            }
        }
    }
    buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
}

void SpeechDialog::slotProcessSpeech()
{
    if (translate_seamless->isChecked()) {
        KdenliveSettings::setSrtSeamlessTranslate(true);
        KdenliveSettings::setSeamless_input(seamless_in->currentData().toString());
        KdenliveSettings::setSeamless_output(seamless_out->currentData().toString());
    } else {
        KdenliveSettings::setSrtSeamlessTranslate(false);
    }
    buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    speech_info->clearActions();
    speech_info->setMessageType(KMessageWidget::Information);
    speech_info->setText(i18n("Starting audio export"));
    speech_info->show();
    qApp->processEvents();
    QString sceneList;
    QString audio;
    QTemporaryFile tmpPlaylist(QDir::temp().absoluteFilePath(QStringLiteral("XXXXXX.mlt")));
    m_tmpAudio = std::make_unique<QTemporaryFile>(QDir::temp().absoluteFilePath(QStringLiteral("XXXXXX.wav")));
    if (tmpPlaylist.open()) {
        sceneList = tmpPlaylist.fileName();
    }
    tmpPlaylist.close();
    if (m_tmpAudio->open()) {
        audio = m_tmpAudio->fileName();
    }
    m_tmpAudio->close();
    QString speech = QFileInfo(audio).completeBaseName();
    speech.append(QStringLiteral(".srt"));
    m_tmpSrtPath = QDir::temp().absoluteFilePath(speech);
    m_timeline->sceneList(QDir::temp().absolutePath(), sceneList);
    // TODO: do the rendering in another thread to not block the UI
    QReadLocker lock(&pCore->xmlMutex);
    Mlt::Producer producer(m_timeline->tractor()->get_profile(), "xml", sceneList.toUtf8().constData());
    int tracksCount = m_timeline->tractor()->count();
    std::shared_ptr<Mlt::Service> s(new Mlt::Service(producer));
    std::shared_ptr<Mlt::Multitrack> multi = nullptr;
    bool multitrackFound = false;
    for (int i = 0; i < 10; i++) {
        s.reset(s->producer());
        if (s == nullptr || !s->is_valid()) {
            break;
        }
        if (s->type() == mlt_service_multitrack_type) {
            multi.reset(new Mlt::Multitrack(*s.get()));
            if (multi->count() == tracksCount) {
                // Match
                multitrackFound = true;
                break;
            }
        }
    }
    if (multitrackFound) {
        int trackPos = -1;
        if (m_tid > -1) {
            trackPos = m_timeline->getTrackMltIndex(m_tid);
        }
        int tid = 0;
        for (int i = 0; i < multi->count(); i++) {
            std::shared_ptr<Mlt::Producer> tk(multi->track(i));
            if (tk->get_int("hide") == 1) {
                // Video track, hide it
                tk->set("hide", 3);
            } else if (tid == 0 || (trackPos > -1 && trackPos != tid)) {
                // We only want a specific audio track
                tk->set("hide", 3);
            }
            tid++;
        }
    }
    Mlt::Consumer xmlConsumer(m_timeline->tractor()->get_profile(), "avformat", audio.toUtf8().constData());
    if (!xmlConsumer.is_valid() || !producer.is_valid()) {
        qDebug() << "=== STARTING CONSUMER ERROR";
        if (!producer.is_valid()) {
            qDebug() << "=== PRODUCER INVALID";
        }
        speech_info->setMessageType(KMessageWidget::Warning);
        speech_info->setText(i18n("Audio export failed"));
        qApp->processEvents();
        return;
    }
    speech_progress->setValue(0);
    m_errorLog.clear();
    logOutput->clear();
    speech_info->clearActions();
    frame_progress->setVisible(true);
    buttonBox->button(QDialogButtonBox::Apply)->setEnabled(false);
    qApp->processEvents();
    xmlConsumer.set("terminate_on_pause", 1);
    xmlConsumer.set("properties", "WAV");
    producer.set_in_and_out(m_zone.x(), m_zone.y());
    xmlConsumer.connect(producer);

    qDebug() << "=== STARTING RENDER C, IN:" << m_zone.x() << " - " << m_zone.y();
    m_duration = m_zone.y() - m_zone.x();
    qApp->processEvents();
    xmlConsumer.run();
    qApp->processEvents();
    qDebug() << "=== STARTING RENDER D";
    speech_info->setMessageType(KMessageWidget::Information);
    speech_info->setText(i18n("Starting speech recognition"));
    qApp->processEvents();
    QString modelDirectory = m_stt->modelFolder();
    m_speechJob = std::make_unique<QProcess>(this);
    connect(m_speechJob.get(), static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this, &SpeechDialog::slotProcessSpeechStatus);
    if (KdenliveSettings::speechEngine() == QLatin1String("whisper")) {
        // Whisper
        QString modelName = speech_model->currentData().toString();
        m_speechJob->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_speechJob.get(), &QProcess::readyReadStandardOutput, this, &SpeechDialog::slotProcessWhisperProgress);
        QString language = speech_language->isEnabled() ? speech_language->currentData().toString().simplified() : QString();
        int maxCount = 0;
        if (check_maxchars->isChecked() && check_maxchars->isEnabled()) {
            maxCount = maxChars->value();
            KdenliveSettings::setWhisperMaxChars(maxCount);
        }
        KdenliveSettings::setCutWhisperMaxChars(check_maxchars->isChecked());
        QStringList arguments = {m_stt->subtitleScript(), audio, modelName, QStringLiteral("ffmpeg_path=%1").arg(KdenliveSettings::ffmpegpath())};
        if (!KdenliveSettings::whisperDevice().isEmpty()) {
            arguments << QStringLiteral("device=%1").arg(KdenliveSettings::whisperDevice());
        }
        if (translate_seamless->isChecked()) {
            arguments << QStringLiteral("seamless_source=%1").arg(seamless_in->currentData().toString());
            arguments << QStringLiteral("seamless_target=%1").arg(seamless_out->currentData().toString());
        } else if (translate_box->isChecked() && translate_box->isEnabled()) {
            arguments << QStringLiteral("task=translate");
        }
        if (!language.isEmpty()) {
            arguments << QStringLiteral("language=%1").arg(language);
        }
        if (KdenliveSettings::whisperDisableFP16()) {
            arguments << QStringLiteral("fp16=False");
        }
        if (maxCount > 0) {
            arguments << QStringLiteral("max_line_width=%1").arg(maxCount);
            arguments << QStringLiteral("max_line_count=1");
        }
        qDebug() << "::: PASSING SPEECH ARGS: " << arguments;

        m_speechJob->start(m_stt->venvPythonExecs().python, arguments);
    } else {
        // Vosk
        QString modelName = speech_model->currentText();
        connect(m_speechJob.get(), &QProcess::readyReadStandardOutput, this, &SpeechDialog::slotProcessProgress);
        const QStringList voskArgs = {m_stt->subtitleScript(),
                                      QStringLiteral("--model_directory=%1").arg(modelDirectory),
                                      QStringLiteral("--model=%1").arg(modelName),
                                      QStringLiteral("--src=\"%1\"").arg(audio),
                                      QStringLiteral("--output=%1").arg(m_tmpSrtPath),
                                      QStringLiteral("--ffmpeg_path=%1").arg(KdenliveSettings::ffmpegpath())};
        qDebug() << "::: STARTING VOSK JOB: " << voskArgs;
        m_speechJob->start(m_stt->venvPythonExecs().python, voskArgs);
    }
}

void SpeechDialog::slotProcessSpeechStatus(int exitCode, QProcess::ExitStatus status)
{
    if (!m_errorLog.isEmpty()) {
        speech_info->addAction(m_logAction);
    }
    buttonBox->button(QDialogButtonBox::Apply)->setEnabled(true);
    if (status == QProcess::CrashExit) {
        speech_info->setMessageType(KMessageWidget::Warning);
        speech_info->setText(i18n("Speech recognition aborted."));
        speech_info->animatedShow();
        return;
    }
    if (exitCode == 1 || !QFile::exists(m_tmpSrtPath)) {
        speech_info->setMessageType(KMessageWidget::Warning);
        speech_info->setText(i18n("Speech recognition failed:\n%1", m_speechJob->readAllStandardError()));
        speech_info->animatedShow();
        return;
    }

    m_timeline->getSubtitleModel()->importSubtitle(m_tmpSrtPath, m_zone.x(), true);
    speech_info->setMessageType(KMessageWidget::Positive);
    speech_info->setText(i18n("Subtitles imported"));
    QFile::remove(m_tmpSrtPath);
    frame_progress->setVisible(false);
}

void SpeechDialog::slotProcessProgress()
{
    const QString saveData = QString::fromUtf8(m_speechJob->readAll());
    if (saveData.startsWith(QStringLiteral("progress:"))) {
        double prog = saveData.section(QLatin1Char(':'), 1).toInt() * 3.12;
        speech_progress->setValue(static_cast<int>(100 * prog / m_duration));
    }
}

void SpeechDialog::slotProcessWhisperProgress()
{
    const QString saveData = QString::fromUtf8(m_speechJob->readAll());
    qDebug() << ":::: GOT SCRIPT OUTPUT: " << saveData;
    if (saveData.contains(QStringLiteral("UserWarning:"))) {
        const QString log = saveData.section(QStringLiteral("UserWarning:"), 1).section(QLatin1String("warnings.warn"), 0, 0).simplified() + QLatin1Char('\n');
        QTextCursor text_cursor = QTextCursor(logOutput->document());
        text_cursor.movePosition(QTextCursor::End);
        text_cursor.insertText(log);
        if (!logOutput->isVisible()) {
            logOutput->setVisible(true);
        }
    }
    if (saveData.contains(QStringLiteral("%|"))) {
        int prog = saveData.section(QLatin1Char('%'), 0, 0).toInt();
        speech_progress->setValue(prog);
        if (translate_seamless->isChecked() && prog == 0) {
            if (saveData.contains(QStringLiteral("translating"))) {
                speech_info->setText(i18n("Translating text to %1", seamless_out->currentText()));
            } else if (saveData.contains(QStringLiteral("initialize"))) {
                speech_info->setText(i18n("Initializing translation model"));
            }
        }
    } else {
        m_errorLog.append(saveData);
    }
}

void SpeechDialog::fillSeamlessLanguages()
{
    QMap<QString, QString> langs;
    langs.insert(i18n("Afrikaans"), QStringLiteral("afr"));
    langs.insert(i18n("Amharic"), QStringLiteral("amh"));
    langs.insert(i18n("Armenian"), QStringLiteral("hye"));
    langs.insert(i18n("Assamese"), QStringLiteral("asm"));
    langs.insert(i18n("Basque"), QStringLiteral("eus"));
    langs.insert(i18n("Belarusian"), QStringLiteral("bel"));
    langs.insert(i18n("Bengali"), QStringLiteral("ben"));
    langs.insert(i18n("Bosnian"), QStringLiteral("bos"));
    langs.insert(i18n("Bulgarian"), QStringLiteral("bul"));
    langs.insert(i18n("Burmese"), QStringLiteral("mya"));
    langs.insert(i18n("Cantonese"), QStringLiteral("yue"));
    langs.insert(i18n("Catalan"), QStringLiteral("cat"));
    langs.insert(i18n("Cebuano"), QStringLiteral("ceb"));
    langs.insert(i18n("Central Kurdish"), QStringLiteral("ckb"));
    langs.insert(i18n("Croatian"), QStringLiteral("hrv"));
    langs.insert(i18n("Czech"), QStringLiteral("ces"));
    langs.insert(i18n("Danish"), QStringLiteral("dan"));
    langs.insert(i18n("Dutch"), QStringLiteral("nld"));
    langs.insert(i18n("Egyptian Arabic"), QStringLiteral("arz"));
    langs.insert(i18n("English"), QStringLiteral("eng"));
    langs.insert(i18n("Estonian"), QStringLiteral("est"));
    langs.insert(i18n("Finnish"), QStringLiteral("fin"));
    langs.insert(i18n("French"), QStringLiteral("fra"));
    langs.insert(i18n("Galician"), QStringLiteral("glg"));
    langs.insert(i18n("Ganda"), QStringLiteral("lug"));
    langs.insert(i18n("Georgian"), QStringLiteral("kat"));
    langs.insert(i18n("German"), QStringLiteral("deu"));
    langs.insert(i18n("Greek"), QStringLiteral("ell"));
    langs.insert(i18n("Gujarati"), QStringLiteral("guj"));
    langs.insert(i18n("Halh Mongolian"), QStringLiteral("khk"));
    langs.insert(i18n("Hebrew"), QStringLiteral("heb"));
    langs.insert(i18n("Hindi"), QStringLiteral("hin"));
    langs.insert(i18n("Hungarian"), QStringLiteral("hun"));
    langs.insert(i18n("Icelandic"), QStringLiteral("isl"));
    langs.insert(i18n("Igbo"), QStringLiteral("ibo"));
    langs.insert(i18n("Indonesian"), QStringLiteral("ind"));
    langs.insert(i18n("Irish"), QStringLiteral("gle"));
    langs.insert(i18n("Italian"), QStringLiteral("ita"));
    langs.insert(i18n("Japanese"), QStringLiteral("jpn"));
    langs.insert(i18n("Javanese"), QStringLiteral("jav"));
    langs.insert(i18n("Kannada"), QStringLiteral("kan"));
    langs.insert(i18n("Kazakh"), QStringLiteral("kaz"));
    langs.insert(i18n("Khmer"), QStringLiteral("khm"));
    langs.insert(i18n("Korean"), QStringLiteral("kor"));
    langs.insert(i18n("Kyrgyz"), QStringLiteral("kir"));
    langs.insert(i18n("Lao"), QStringLiteral("lao"));
    langs.insert(i18n("Lithuanian"), QStringLiteral("lit"));
    langs.insert(i18n("Luo"), QStringLiteral("luo"));
    langs.insert(i18n("Macedonian"), QStringLiteral("mkd"));
    langs.insert(i18n("Maithili"), QStringLiteral("mai"));
    langs.insert(i18n("Malayalam"), QStringLiteral("mal"));
    langs.insert(i18n("Maltese"), QStringLiteral("mlt"));
    langs.insert(i18n("Mandarin Chinese"), QStringLiteral("cmn"));
    langs.insert(i18n("Mandarin Chinese"), QStringLiteral("cmn_Hant"));
    langs.insert(i18n("Marathi"), QStringLiteral("mar"));
    langs.insert(i18n("Meitei"), QStringLiteral("mni"));
    langs.insert(i18n("Modern Standard Arabic"), QStringLiteral("arb"));
    langs.insert(i18n("Moroccan Arabic"), QStringLiteral("ary"));
    langs.insert(i18n("Nepali"), QStringLiteral("npi"));
    langs.insert(i18n("Nigerian Fulfulde"), QStringLiteral("fuv"));
    langs.insert(i18n("North Azerbaijani"), QStringLiteral("azj"));
    langs.insert(i18n("Northern Uzbek"), QStringLiteral("uzn"));
    langs.insert(i18n("Norwegian Bokmål"), QStringLiteral("nob"));
    langs.insert(i18n("Norwegian Nynorsk"), QStringLiteral("nno"));
    langs.insert(i18n("Nyanja"), QStringLiteral("nya"));
    langs.insert(i18n("Odia"), QStringLiteral("ory"));
    langs.insert(i18n("Polish"), QStringLiteral("pol"));
    langs.insert(i18n("Portuguese"), QStringLiteral("por"));
    langs.insert(i18n("Punjabi"), QStringLiteral("pan"));
    langs.insert(i18n("Romanian"), QStringLiteral("ron"));
    langs.insert(i18n("Russian"), QStringLiteral("rus"));
    langs.insert(i18n("Serbian"), QStringLiteral("srp"));
    langs.insert(i18n("Shona"), QStringLiteral("sna"));
    langs.insert(i18n("Sindhi"), QStringLiteral("snd"));
    langs.insert(i18n("Slovak"), QStringLiteral("slk"));
    langs.insert(i18n("Slovenian"), QStringLiteral("slv"));
    langs.insert(i18n("Somali"), QStringLiteral("som"));
    langs.insert(i18n("Southern Pashto"), QStringLiteral("pbt"));
    langs.insert(i18n("Spanish"), QStringLiteral("spa"));
    langs.insert(i18n("Standard Latvian"), QStringLiteral("lvs"));
    langs.insert(i18n("Standard Malay"), QStringLiteral("zsm"));
    langs.insert(i18n("Swahili"), QStringLiteral("swh"));
    langs.insert(i18n("Swedish"), QStringLiteral("swe"));
    langs.insert(i18n("Tagalog"), QStringLiteral("tgl"));
    langs.insert(i18n("Tajik"), QStringLiteral("tgk"));
    langs.insert(i18n("Tamil"), QStringLiteral("tam"));
    langs.insert(i18n("Telugu"), QStringLiteral("tel"));
    langs.insert(i18n("Thai"), QStringLiteral("tha"));
    langs.insert(i18n("Turkish"), QStringLiteral("tur"));
    langs.insert(i18n("Ukrainian"), QStringLiteral("ukr"));
    langs.insert(i18n("Urdu"), QStringLiteral("urd"));
    langs.insert(i18n("Vietnamese"), QStringLiteral("vie"));
    langs.insert(i18n("Welsh"), QStringLiteral("cym"));
    langs.insert(i18n("West Central Oromo"), QStringLiteral("gaz"));
    langs.insert(i18n("Western Persian"), QStringLiteral("pes"));
    langs.insert(i18n("Yoruba"), QStringLiteral("yor"));
    langs.insert(i18n("Zulu"), QStringLiteral("zul"));
    QMapIterator<QString, QString> langIter(langs);
    while (langIter.hasNext()) {
        langIter.next();
        seamless_in->addItem(langIter.key(), langIter.value());
        seamless_out->addItem(langIter.key(), langIter.value());
    }
    if (!KdenliveSettings::seamless_input().isEmpty()) {
        int ix = seamless_in->findData(KdenliveSettings::seamless_input());
        int outIx = seamless_out->findData(KdenliveSettings::seamless_output());
        if (ix > -1) {
            seamless_in->setCurrentIndex(ix);
        }
        if (outIx > -1) {
            seamless_out->setCurrentIndex(outIx);
        }
    }
}
