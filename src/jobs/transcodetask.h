/*
    SPDX-FileCopyrightText: 2021 Jean-Baptiste Mardelle <jb@kdenlive.org>

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "abstracttask.h"
#include "project/transcodeseek.h"

class QProcess;

class TranscodeTask : public AbstractTask
{
public:
    TranscodeTask(const ObjectId &owner, const QString &suffix, const QString &preParams, const QString &params, TranscodeSeek::TranscodeInfo info, int in,
                  int out, bool replaceProducer, QObject *object, bool checkProfile);
    static void start(const ObjectId &owner, const QString &suffix, const QString &preParams, const QString &params, TranscodeSeek::TranscodeInfo info, int in,
                      int out, bool replaceProducer, QObject *object, bool force = false, bool checkProfile = false);

protected:
    void run() override;

private Q_SLOTS:
    void processLogInfo();

private:
    int m_jobDuration;
    bool m_isFfmpegJob;
    QString m_suffix;
    QString m_transcodeParams;
    QString m_transcodePreParams;
    bool m_replaceProducer;
    int m_inPoint;
    int m_outPoint;
    bool m_checkProfile;
    TranscodeSeek::TranscodeInfo m_transcodeInfo;
    QProcess *m_jobProcess;
    QString m_errorMessage;
    QString m_logDetails;
};
