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

#include "AutoUpdater.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

AutoUpdater::AutoUpdater(QWidget *parentWidget, QSettings *settings, QObject *parent)
    : QObject(parent),
      _parentWidget(parentWidget),
      _settings(settings),
      _networkManager(new QNetworkAccessManager(this)),
      _downloadReply(nullptr),
      _progressDialog(nullptr),
      _downloadFile(nullptr)
{
}

AutoUpdater::~AutoUpdater()
{
    cancelDownload();
}

void AutoUpdater::downloadUpdate(const QString &zipUrl, qint64 expectedSize)
{
    if (zipUrl.isEmpty()) {
        emit downloadFailed(tr("No download URL available for this release."));
        return;
    }

    // Download to temp directory
    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString zipPath = QDir(tempDir).filePath("MidiEditorAI-update.zip");

    // Remove any leftover partial download
    if (QFile::exists(zipPath)) {
        QFile::remove(zipPath);
    }

    _downloadFile = new QFile(zipPath);
    if (!_downloadFile->open(QIODevice::WriteOnly)) {
        emit downloadFailed(tr("Could not create temporary file for download:\n%1").arg(zipPath));
        delete _downloadFile;
        _downloadFile = nullptr;
        return;
    }

    QUrl url(zipUrl);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, "MidiEditor AI");

    _downloadReply = _networkManager->get(request);
    connect(_downloadReply, &QNetworkReply::downloadProgress, this, &AutoUpdater::onDownloadProgress);
    connect(_downloadReply, &QNetworkReply::finished, this, &AutoUpdater::onDownloadFinished);
    connect(_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (_downloadFile && _downloadReply) {
            _downloadFile->write(_downloadReply->readAll());
        }
    });

    // Progress dialog — CRITICAL: disable autoReset and autoClose!
    // QProgressDialog::autoReset (default: true) calls reset() when value hits maximum.
    // reset() sets an internal cancellation_flag=true, which can trigger cancelDownload()
    // via the canceled() signal BEFORE onDownloadFinished processes the successful result.
    // This was the root cause of the updater never working — downloadComplete was never emitted.
    _progressDialog = new QProgressDialog(tr("Downloading update..."), tr("Cancel"), 0, 100, _parentWidget);
    _progressDialog->setWindowTitle(tr("Auto-Update"));
    _progressDialog->setWindowModality(Qt::WindowModal);
    _progressDialog->setAutoReset(false);
    _progressDialog->setAutoClose(false);
    _progressDialog->setMinimumDuration(0);
    _progressDialog->setValue(0);

    if (expectedSize > 0) {
        _progressDialog->setLabelText(tr("Downloading update (%1 MB)...")
            .arg(QString::number(expectedSize / (1024.0 * 1024.0), 'f', 1)));
    }

    connect(_progressDialog, &QProgressDialog::canceled, this, &AutoUpdater::cancelDownload);
}

void AutoUpdater::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (_progressDialog && bytesTotal > 0) {
        _progressDialog->setMaximum(100);
        _progressDialog->setValue(static_cast<int>((bytesReceived * 100) / bytesTotal));
    }
}

void AutoUpdater::onDownloadFinished()
{
    // CRITICAL: Disconnect the canceled signal BEFORE closing the progress dialog!
    // QProgressDialog::closeEvent() emits canceled(), which would trigger cancelDownload(),
    // nulling out _downloadReply and _downloadFile and causing this function to return early.
    if (_progressDialog) {
        disconnect(_progressDialog, &QProgressDialog::canceled, this, &AutoUpdater::cancelDownload);
        _progressDialog->close();
        _progressDialog->deleteLater();
        _progressDialog = nullptr;
    }

    if (!_downloadReply || !_downloadFile) {
        return;
    }

    // Write any remaining data that readyRead may not have delivered
    QByteArray remaining = _downloadReply->readAll();
    if (!remaining.isEmpty()) {
        _downloadFile->write(remaining);
    }

    _downloadFile->close();
    QString zipPath = _downloadFile->fileName();

    if (_downloadReply->error() != QNetworkReply::NoError) {
        if (_downloadReply->error() != QNetworkReply::OperationCanceledError) {
            emit downloadFailed(tr("Download failed: %1").arg(_downloadReply->errorString()));
        }
        _downloadFile->remove();
        delete _downloadFile;
        _downloadFile = nullptr;
        _downloadReply->deleteLater();
        _downloadReply = nullptr;
        return;
    }

    // Verify the file was actually written
    QFileInfo fi(zipPath);
    if (fi.size() < 1024) {
        emit downloadFailed(tr("Downloaded file is too small (%1 bytes). The download may have failed.").arg(fi.size()));
        _downloadFile->remove();
        delete _downloadFile;
        _downloadFile = nullptr;
        _downloadReply->deleteLater();
        _downloadReply = nullptr;
        return;
    }

    delete _downloadFile;
    _downloadFile = nullptr;
    _downloadReply->deleteLater();
    _downloadReply = nullptr;

    _downloadedZipPath = zipPath;
    qDebug() << "AutoUpdater: Download complete:" << zipPath << "size:" << fi.size();
    emit downloadComplete(zipPath);
}

void AutoUpdater::cancelDownload()
{
    if (_downloadReply) {
        _downloadReply->abort();
        _downloadReply->deleteLater();
        _downloadReply = nullptr;
    }
    if (_progressDialog) {
        _progressDialog->close();
        _progressDialog->deleteLater();
        _progressDialog = nullptr;
    }
    if (_downloadFile) {
        QString path = _downloadFile->fileName();
        _downloadFile->close();
        QFile::remove(path);
        delete _downloadFile;
        _downloadFile = nullptr;
    }
}

void AutoUpdater::scheduleUpdateOnExit()
{
    if (!_downloadedZipPath.isEmpty() && QFile::exists(_downloadedZipPath)) {
        _settings->setValue("pending_update_zip", _downloadedZipPath);
    }
}

bool AutoUpdater::hasPendingUpdate() const
{
    QString zipPath = _settings->value("pending_update_zip").toString();
    return !zipPath.isEmpty() && QFile::exists(zipPath);
}

void AutoUpdater::executeUpdateNow(const QString &currentMidiPath)
{
    if (_downloadedZipPath.isEmpty() || !QFile::exists(_downloadedZipPath)) {
        QMessageBox::warning(_parentWidget, tr("Update Error"), tr("Update file not found. Please try again."));
        return;
    }

    qDebug() << "AutoUpdater: executeUpdateNow ZIP:" << _downloadedZipPath;
    applyUpdate(_downloadedZipPath, currentMidiPath);
}

void AutoUpdater::launchPendingUpdate(const QString &currentMidiPath)
{
    QString zipPath = _settings->value("pending_update_zip").toString();
    if (zipPath.isEmpty() || !QFile::exists(zipPath)) {
        _settings->remove("pending_update_zip");
        return;
    }

    _settings->remove("pending_update_zip");
    applyUpdate(zipPath, currentMidiPath);
}

bool AutoUpdater::applyUpdate(const QString &zipPath, const QString &midiPath)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString appPath = QCoreApplication::applicationFilePath();
    QString exeName = QFileInfo(appPath).fileName();
    QString bakPath = appPath + ".bak";

    qDebug() << "AutoUpdater::applyUpdate";
    qDebug() << "  appDir:" << appDir;
    qDebug() << "  appPath:" << appPath;
    qDebug() << "  zipPath:" << zipPath;

    // Step 1: Rename the running EXE to .bak
    // On Windows, a running EXE can be RENAMED (but not deleted or overwritten).
    // This frees the filename so the new EXE can be placed there.
    if (QFile::exists(bakPath)) {
        QFile::remove(bakPath);
    }
    if (!QFile::rename(appPath, bakPath)) {
        QMessageBox::warning(_parentWidget, tr("Update Error"),
            tr("Could not rename the running application.\n\n"
               "Make sure you have write permissions to:\n%1").arg(appDir));
        return false;
    }
    qDebug() << "  Step 1: Renamed EXE to .bak";

    // Step 2: Extract ZIP to a temp staging directory
    QString stagingDir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                             .filePath("MidiEditorAI-update-staging");

    // Clean up any old staging dir
    if (QDir(stagingDir).exists()) {
        QDir(stagingDir).removeRecursively();
    }

    QString nativeZip = QDir::toNativeSeparators(zipPath);
    QString nativeStaging = QDir::toNativeSeparators(stagingDir);

    qDebug() << "  Step 2: Extracting to staging:" << stagingDir;

    QProcess extractProc;
    extractProc.setProgram("powershell.exe");
    extractProc.setArguments({
        "-NoProfile", "-Command",
        QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force").arg(nativeZip, nativeStaging)
    });
    extractProc.start();
    extractProc.waitForFinished(120000); // 2 minute timeout

    if (extractProc.exitCode() != 0) {
        qDebug() << "  Extraction failed:" << extractProc.readAllStandardError();
        // Restore the original EXE
        QFile::rename(bakPath, appPath);
        QMessageBox::warning(_parentWidget, tr("Update Error"),
            tr("Failed to extract the update ZIP.\n\n%1").arg(QString(extractProc.readAllStandardError())));
        return false;
    }
    qDebug() << "  Step 2: Extraction complete";

    // Step 3: Find the actual content directory
    // ZIP may have a nested subfolder like MidiEditorAI-v1.1.4-win64/
    QString sourceDir = stagingDir;
    QDir staging(stagingDir);
    QStringList subdirs = staging.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (subdirs.size() == 1) {
        sourceDir = staging.filePath(subdirs.first());
        qDebug() << "  Step 3: Using nested subfolder:" << sourceDir;
    }

    // Step 4: Copy new files from staging to app directory
    int filesCopied = 0;
    int filesSkipped = 0;
    QDirIterator it(sourceDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QString relativePath = QDir(sourceDir).relativeFilePath(it.filePath());
        QString destPath = QDir(appDir).filePath(relativePath);

        // Ensure destination directory exists
        QDir().mkpath(QFileInfo(destPath).absolutePath());

        // Try to remove existing file first
        if (QFile::exists(destPath)) {
            if (!QFile::remove(destPath)) {
                // File is locked (e.g. loaded DLL) — try renaming to .bak
                QString destBak = destPath + ".bak";
                if (QFile::exists(destBak)) {
                    QFile::remove(destBak);
                }
                if (!QFile::rename(destPath, destBak)) {
                    qDebug() << "  Skipped (locked):" << relativePath;
                    filesSkipped++;
                    continue;
                }
            }
        }

        if (QFile::copy(it.filePath(), destPath)) {
            filesCopied++;
        } else {
            qDebug() << "  Failed to copy:" << relativePath;
            filesSkipped++;
        }
    }

    qDebug() << "  Step 4: Files copied:" << filesCopied << "skipped:" << filesSkipped;

    // Step 5: Cleanup temp files
    QFile::remove(zipPath);
    QDir(stagingDir).removeRecursively();
    qDebug() << "  Step 5: Cleanup done";

    // Step 6: Launch the new EXE
    // QProcess::startDetached works reliably for launching an EXE
    // (unlike batch files which had all the console/quoting problems)
    QString newExePath = QDir(appDir).filePath(exeName);
    QStringList args;
    if (!midiPath.isEmpty()) {
        args << "--open" << midiPath;
    }

    qDebug() << "  Step 6: Launching:" << newExePath << args;
    bool launched = QProcess::startDetached(newExePath, args, appDir);
    qDebug() << "  Launch result:" << launched;

    if (!launched) {
        QMessageBox::warning(_parentWidget, tr("Update Error"),
            tr("Update extracted successfully but failed to restart.\n\n"
               "Please start %1 manually.").arg(exeName));
        return false;
    }

    // Step 7: Terminate the current process
    // ExitProcess is safe here — we've already saved the user's file
    // and settings, and the new process is already running.
    qDebug() << "  Step 7: Terminating current process";
#ifdef Q_OS_WIN
    ExitProcess(0);
#else
    _exit(0);
#endif

    return true; // never reached
}

void AutoUpdater::cleanupOldBackups()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    QStringList bakFiles = dir.entryList({"*.bak"}, QDir::Files);
    for (const QString &bakFile : bakFiles) {
        QString bakPath = dir.filePath(bakFile);
        if (QFile::remove(bakPath)) {
            qDebug() << "AutoUpdater: Cleaned up backup:" << bakFile;
        }
    }
}
