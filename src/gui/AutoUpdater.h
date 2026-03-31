/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef AUTOUPDATER_H
#define AUTOUPDATER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QFile;
class QWidget;
class QSettings;

/**
 * \class AutoUpdater
 * \brief Downloads update ZIPs from GitHub and orchestrates the update process.
 *
 * Self-update approach (no external batch file needed):
 * 1. Downloads ZIP to temp directory
 * 2. Renames running EXE to .bak (Windows allows renaming a running EXE)
 * 3. Extracts ZIP over the app directory (PowerShell Expand-Archive)
 * 4. Launches the new EXE via QProcess::startDetached
 * 5. Terminates with ExitProcess(0)
 * 6. On next startup, old .bak files are cleaned up
 *
 * Supports two modes:
 * - "Update Now": Downloads ZIP, applies immediately, restarts.
 * - "After Exit": Downloads ZIP, stores path in QSettings, applies on close.
 */
class AutoUpdater : public QObject {
    Q_OBJECT

public:
    explicit AutoUpdater(QWidget *parentWidget, QSettings *settings, QObject *parent = nullptr);
    ~AutoUpdater();

    /** Start downloading the update ZIP from the given URL. Shows a progress dialog. */
    void downloadUpdate(const QString &zipUrl, qint64 expectedSize);

    /** Mark the downloaded ZIP as "pending" — will be applied on app exit. */
    void scheduleUpdateOnExit();

    /** Apply the update immediately: extract, restart, exit. */
    void executeUpdateNow(const QString &currentMidiPath);

    /** Returns true if a pending update ZIP is stored in QSettings. */
    bool hasPendingUpdate() const;

    /** Apply a pending update (called from closeEvent). */
    void launchPendingUpdate(const QString &currentMidiPath);

    /** Cancel an in-progress download. */
    void cancelDownload();

    /** Returns the path of the downloaded ZIP (empty if not yet downloaded). */
    QString downloadedZipPath() const { return _downloadedZipPath; }

    /** Clean up .bak files from a previous update. Call on startup. */
    static void cleanupOldBackups();

signals:
    void downloadComplete(const QString &zipPath);
    void downloadFailed(const QString &error);

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();

private:
    bool applyUpdate(const QString &zipPath, const QString &midiPath);

    QWidget *_parentWidget;
    QSettings *_settings;
    QNetworkAccessManager *_networkManager;
    QNetworkReply *_downloadReply;
    QProgressDialog *_progressDialog;
    QFile *_downloadFile;
    QString _downloadedZipPath;
};

#endif // AUTOUPDATER_H
