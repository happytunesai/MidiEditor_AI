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

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSurfaceFormat>
#include <QOpenGLContext>
#include <QTimer>

#include "gui/MainWindow.h"
#include "gui/Appearance.h"
#include "gui/AutoUpdater.h"
#include "midi/MidiInput.h"
#include "midi/MidiOutput.h"
#include "ai/EditorContext.h"
#include "LoggingConfig.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

// File-based logging so users can attach a log when something crashes
// (e.g. LAN Live join). Writes to <exe-dir>/midieditor_ai.log so the user
// can find it instantly without hunting through %LocalAppData%.
//
// Rotation policy (revised 2026-05-08 after a Live-session test produced
// 60 MB of log in a few minutes when Info-level was enabled):
//   • Active log capped at 10 MB
//   • On overflow, rotate: .log → .log.1, .log.1 → .log.2, .log.2 → .log.3,
//     oldest .log.3 deleted. Total disk usage capped at ~40 MB.
//   • Rotation triggers in-flight (during the running process) — the
//     earlier code only checked at startup, so a long-running session
//     could fill the disk with no recovery until restart.

// Win32 includes BEFORE the anonymous namespace — otherwise the typedefs
// HINSTANCE / LPSTR / WINAPI / EXCEPTION_POINTERS land inside the anon
// namespace and the global-scope WinMain (in the NO_CONSOLE_MODE branch
// below) can't see them. <windows.h> has header-guards so the later
// re-include in the NO_CONSOLE_MODE block is a no-op (BUG-COLLAB-035).
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QFile *g_logFile = nullptr;
QMutex g_logMutex;
QString g_logPath;                     // remembered so the in-process
                                       // rotate path can find the base
qint64 g_logBytes = 0;                 // bytes written since last rotate
constexpr qint64 kLogMaxBytes = 10 * 1024 * 1024;  // 10 MB
constexpr int kLogRotateBackups = 3;   // .1 .2 .3

// Caller MUST hold g_logMutex.
void rotateLogFiles_locked() {
    if (g_logPath.isEmpty()) return;
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->flush();
        g_logFile->close();
    }
    // .2 → .3, .1 → .2, base → .1 (oldest .3 dropped first)
    for (int i = kLogRotateBackups; i >= 1; --i) {
        QString dst = g_logPath + QStringLiteral(".%1").arg(i);
        QString src = (i == 1) ? g_logPath
                                : g_logPath + QStringLiteral(".%1").arg(i - 1);
        QFile::remove(dst);
        if (QFileInfo::exists(src)) QFile::rename(src, dst);
    }
    if (g_logFile) {
        g_logFile->setFileName(g_logPath);
        if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            // Reopening failed — fall back to stderr-only.
            delete g_logFile;
            g_logFile = nullptr;
        }
    }
    g_logBytes = 0;
}

void midiEditorLogHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
    QMutexLocker lock(&g_logMutex);
    const char *level = "DBG";
    switch (type) {
        case QtDebugMsg:    level = "DBG"; break;
        case QtInfoMsg:     level = "INF"; break;
        case QtWarningMsg:  level = "WRN"; break;
        case QtCriticalMsg: level = "ERR"; break;
        case QtFatalMsg:    level = "FTL"; break;
    }
    QString line = QStringLiteral("%1 %2 [%3] %4\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                       .arg(QString::fromLatin1(level))
                       .arg(ctx.category ? QString::fromLatin1(ctx.category) : QStringLiteral("default"))
                       .arg(msg);
    QByteArray utf = line.toUtf8();
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->write(utf);
        g_logFile->flush();
        g_logBytes += utf.size();
        if (g_logBytes >= kLogMaxBytes) {
            rotateLogFiles_locked();
        }
    }
    fputs(utf.constData(), stderr);
    if (type == QtFatalMsg) {
        if (g_logFile && g_logFile->isOpen()) g_logFile->flush();
        abort();
    }
}

#ifdef Q_OS_WIN
// <windows.h> is included at file scope above the anonymous namespace —
// see the BUG-COLLAB-035 comment there. We only need the WINAPI typedefs
// here; the actual include has already happened.
LONG WINAPI midiEditorCrashHandler(EXCEPTION_POINTERS *info) {
    QMutexLocker lock(&g_logMutex);
    QString msg = QStringLiteral(
        "%1 FTL [crash] UNHANDLED EXCEPTION code=0x%2 addr=0x%3 — process is about to die\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(QString::number(info->ExceptionRecord->ExceptionCode, 16))
        .arg(QString::number(reinterpret_cast<quintptr>(info->ExceptionRecord->ExceptionAddress), 16));
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->write(msg.toUtf8());
        g_logFile->flush();
        g_logFile->close();
    }
    fputs(msg.toUtf8().constData(), stderr);
    return EXCEPTION_CONTINUE_SEARCH;  // let WER take over for the actual crash dialog
}
#endif

void installFileLogHandler() {
    // Primary: next to the executable. Easy to find, no env-var hunting.
    QString primary = QCoreApplication::applicationDirPath() + QStringLiteral("/midieditor_ai.log");
    // Fallback: %LocalAppData%/MidiEditor AI/midieditor_ai.log if exe-dir
    // isn't writable (rare — Program Files install with read-only perms).
    QString fallback;
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!dataDir.isEmpty()) {
        QDir().mkpath(dataDir);
        fallback = dataDir + QStringLiteral("/midieditor_ai.log");
    }

    // Cleanup of the legacy `.prev` rotation slot from earlier builds —
    // we now use numbered `.1/.2/.3` backups and don't want stale files
    // lingering with a 60 MB Info-spam payload.
    QFile::remove(primary + QStringLiteral(".prev"));
    if (!fallback.isEmpty()) QFile::remove(fallback + QStringLiteral(".prev"));

    auto tryOpen = [](const QString &path) -> QFile * {
        if (path.isEmpty()) return nullptr;
        QFileInfo fi(path);
        // Pre-rotate at startup if the existing log already overflows.
        // Subsequent overflows are handled in-flight by midiEditorLogHandler.
        if (fi.exists() && fi.size() >= kLogMaxBytes) {
            for (int i = kLogRotateBackups; i >= 1; --i) {
                QString dst = path + QStringLiteral(".%1").arg(i);
                QString src = (i == 1) ? path
                                        : path + QStringLiteral(".%1").arg(i - 1);
                QFile::remove(dst);
                if (QFileInfo::exists(src)) QFile::rename(src, dst);
            }
        }
        QFile *f = new QFile(path);
        if (!f->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            delete f;
            return nullptr;
        }
        return f;
    };

    g_logFile = tryOpen(primary);
    QString chosen = primary;
    if (!g_logFile) {
        g_logFile = tryOpen(fallback);
        chosen = fallback;
    }
    if (!g_logFile) return;
    g_logPath = chosen;
    g_logBytes = g_logFile->size();  // continue counting from existing length

    qInstallMessageHandler(midiEditorLogHandler);
    QString header = QStringLiteral("\n========== %1 startup (log path: %2) ==========\n")
                         .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                         .arg(chosen);
    QByteArray hdrUtf = header.toUtf8();
    g_logFile->write(hdrUtf);
    g_logFile->flush();
    g_logBytes += hdrUtf.size();

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(midiEditorCrashHandler);
#endif
}
}

#ifdef NO_CONSOLE_MODE
#include <tchar.h>
#include <windows.h>
std::string wstrtostr(const std::wstring &wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
    if (size_needed <= 0) return std::string();
    std::string strTo(size_needed - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show) {
    int argc = 1;
    char *argv[] = {"", ""};
    std::string str;
    LPWSTR *szArglist = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (NULL != szArglist && argc > 1) {
        str = wstrtostr(szArglist[1]);
        argv[1] = &str.at(0);
        argc = 2;
    }

#else
int main(int argc, char *argv[]) {
#endif

    // Load high DPI scaling settings before creating QApplication
    // These must be set before QApplication is created
    Appearance::loadEarlySettings();
    bool ignoreSystemScaling = Appearance::ignoreSystemScaling();
    bool useRoundedScaling = Appearance::useRoundedScaling();
    bool ignoreFontScaling = Appearance::ignoreFontScaling();
    int msaaSamples = Appearance::msaaSamples();
    bool enableVSync = Appearance::enableVSync();
    bool useHardwareAcceleration = Appearance::useHardwareAcceleration();

    // Debug output to verify scaling settings
    qDebug() << "=== DPI Scaling Configuration ===";
    qDebug() << "Ignore system scaling:" << ignoreSystemScaling;
    qDebug() << "Ignore font scaling:" << ignoreFontScaling;
    qDebug() << "Use rounded scaling:" << useRoundedScaling;
    qDebug() << "MSAA samples:" << msaaSamples;
    qDebug() << "VSync enabled:" << enableVSync;
    qDebug() << "Hardware acceleration:" << useHardwareAcceleration;

    // High DPI scaling is always enabled in Qt 6, so we only need to configure the scaling policy
    if (ignoreSystemScaling) {
        // For Qt 6, we need to be more aggressive to truly ignore system scaling
        qDebug() << "Setting aggressive scaling override to ignore system scaling";

        // Set multiple environment variables to force 1.0 scaling
        qputenv("QT_SCALE_FACTOR", "1.0");
        qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
        qputenv("QT_DEVICE_PIXEL_RATIO", "1.0");
        qputenv("QT_SCREEN_SCALE_FACTORS", "1.0");
    } else {
        if (useRoundedScaling) {
            // Use rounded scaling behavior for sharper rendering
            // Set rounding policy to Round instead of PassThrough (Qt6 default)
            qDebug() << "Setting rounded scaling policy";
            QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);

            // Enable high DPI scaling with rounding
            qputenv("QT_ENABLE_HIGHDPI_SCALING", "1");
            qputenv("QT_SCALE_FACTOR_ROUNDING_POLICY", "Round");

            // Use integer-based DPI awareness
            qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "1");
        } else {
            // Use Qt6 default behavior (PassThrough with fractional scaling)
            qDebug() << "Using Qt6 default PassThrough scaling policy";
            QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
        }
    }

    // Handle font scaling separately from UI scaling
    if (ignoreFontScaling) {
        qDebug() << "Setting font scaling override to ignore font scaling";
        // Disable font DPI scaling to keep fonts at their original sizes
        qputenv("QT_FONT_DPI", "96"); // Force standard 96 DPI for fonts
        qputenv("QT_USE_PHYSICAL_DPI", "0"); // Don't use physical DPI for font sizing
    }

    // Add application directory plugins path before QApplication construction
    {
        QString appDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        QCoreApplication::addLibraryPath(appDir + "/plugins");
    }

    QApplication a(argc, argv);

    // Additional font scaling control after QApplication creation
    if (ignoreFontScaling) {
        // Force the application to use 96 DPI for all font calculations
        QApplication::setAttribute(Qt::AA_Use96Dpi, true);
    }

    // Debug actual scaling factors after QApplication creation
    qDebug() << "=== Actual DPI Scaling Results ===";
    QScreen *screen = QApplication::primaryScreen();
    if (screen) {
        qDebug() << "Primary screen DPI:" << screen->logicalDotsPerInch();
        qDebug() << "Device pixel ratio:" << screen->devicePixelRatio();
        qDebug() << "Physical DPI:" << screen->physicalDotsPerInch();
        qDebug() << "Screen geometry:" << screen->geometry();
        qDebug() << "Available geometry:" << screen->availableGeometry();
    }
    qDebug() << "QT_SCALE_FACTOR env var:" << qgetenv("QT_SCALE_FACTOR");
    qDebug() << "QT_AUTO_SCREEN_SCALE_FACTOR env var:" << qgetenv("QT_AUTO_SCREEN_SCALE_FACTOR");
    qDebug() << "QT_ENABLE_HIGHDPI_SCALING env var:" << qgetenv("QT_ENABLE_HIGHDPI_SCALING");
    qDebug() << "QT_SCALE_FACTOR_ROUNDING_POLICY env var:" << qgetenv("QT_SCALE_FACTOR_ROUNDING_POLICY");
    qDebug() << "QT_FONT_DPI env var:" << qgetenv("QT_FONT_DPI");

    // Debug font scaling information
    QFont defaultFont = QApplication::font();
    qDebug() << "=== Font Scaling Information ===";
    qDebug() << "Default application font:" << defaultFont.family() << "size:" << defaultFont.pointSize() << "pixel size:" << defaultFont.pixelSize();
    QFontMetrics fm(defaultFont);
    qDebug() << "Font metrics height:" << fm.height() << "ascent:" << fm.ascent();

    // Initialize OpenGL only if hardware acceleration is enabled
    if (useHardwareAcceleration) {
        qDebug() << "=== Initializing OpenGL 4.6 for Maximum Performance ===";
        QSurfaceFormat format;

        // Request OpenGL 4.6 Core Profile for latest features and best performance
        format.setVersion(4, 6);
        format.setProfile(QSurfaceFormat::CoreProfile);

        // Enable all performance features
        format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        format.setRenderableType(QSurfaceFormat::OpenGL);

        // High-quality rendering settings
        format.setDepthBufferSize(24);
        format.setStencilBufferSize(8);
        format.setRedBufferSize(8);
        format.setGreenBufferSize(8);
        format.setBlueBufferSize(8);
        format.setAlphaBufferSize(8);

        // Enable multisampling based on user settings (loaded early from QSettings)
        format.setSamples(msaaSamples); // Use configured MSAA level

        // Configure VSync based on user preference for balance between responsiveness and smoothness
        // VSync setting loaded from Appearance::enableVSync() (from user settings)
        format.setSwapInterval(enableVSync ? 1 : 0); // 0 = VSync off, 1 = VSync on

        QSurfaceFormat::setDefaultFormat(format);

        qDebug() << "OpenGL 4.6 Core Profile format set:" << format;
        qDebug() << "MSAA samples configured:" << msaaSamples << "(from user settings)";
        qDebug() << "VSync configured:" << (enableVSync ? "Enabled (smooth playback)" : "Disabled (responsive editing)");
    } else {
        qDebug() << "=== Hardware Acceleration Disabled ===";
        qDebug() << "Skipping OpenGL initialization - using software rendering";
    }

    // Version comes from CMakeLists.txt → MIDIEDITOR_RELEASE_VERSION_STRING
    // Single source of truth: only update version in CMakeLists.txt line 3
#ifdef MIDIEDITOR_RELEASE_VERSION_STRING_DEF
    a.setApplicationVersion(MIDIEDITOR_RELEASE_VERSION_STRING_DEF);
#else
    a.setApplicationVersion("0.0.0-dev");
#endif
    a.setApplicationName("MidiEditor AI");
    a.setQuitOnLastWindowClosed(true);

    // File log handler — must be after setApplicationName so QStandardPaths
    // resolves to the right %LocalAppData% subdir.
    installFileLogHandler();
    // Plan §11.10i: apply user-configured Qt logging filter rules so the
    // chosen level / per-category overrides are in effect from the very
    // first qInfo / qCDebug call. Default (no QSettings entry) is
    // Warnings, matching Qt's own default.
    LoggingConfig::applyFromSettings();
    qInfo() << "MidiEditor AI" << a.applicationVersion() << "starting up";

    a.setAttribute(Qt::AA_CompressHighFrequencyEvents, true);
    a.setAttribute(Qt::AA_CompressTabletEvents, true);

    // Use more reliable architecture detection
#if defined(__ARCH64__) || defined(_WIN64) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64) || defined(_M_X64)
    a.setProperty("arch", "64");
#else
    a.setProperty("arch", "32");
#endif

    MidiOutput::init();
    MidiInput::init();

    // Clean up .bak files from a previous self-update
    AutoUpdater::cleanupOldBackups();

    // Load custom system prompts if present
    QString promptsPath = QCoreApplication::applicationDirPath() + "/system_prompts.json";
    if (QFile::exists(promptsPath)) {
        if (!EditorContext::loadCustomPrompts(promptsPath)) {
            qWarning() << "Invalid system_prompts.json — using defaults";
        }
    }

    // Parse command-line arguments
    // Supports: MidiEditorAI.exe [file.mid]
    //           MidiEditorAI.exe --open <file.mid>  (used by auto-updater)
    //           MidiEditorAI.exe --open-settings     (reopen settings on Appearance tab after theme restart)
    //           MidiEditorAI.exe --updated-from=<ver> (show post-update dialog after self-update)
    QString openFilePath;
    bool openSettings = false;
    QString updatedFromVersion;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--open" && i + 1 < argc) {
            openFilePath = QString::fromLocal8Bit(argv[++i]);
        } else if (arg == "--open-settings") {
            openSettings = true;
        } else if (arg.startsWith("--updated-from=")) {
            updatedFromVersion = arg.mid(QString("--updated-from=").length());
        } else if (!arg.startsWith("-")) {
            openFilePath = arg;
        }
    }

    MainWindow *w;
    if (!openFilePath.isEmpty())
        w = new MainWindow(openFilePath.toLocal8Bit().data());
    else
        w = new MainWindow();
    w->showMaximized();

    // After startup, reopen settings dialog on Appearance tab if requested
    if (openSettings) {
        QTimer::singleShot(300, w, &MainWindow::openConfigOnAppearanceTab);
    }

    // Show post-update dialog if we just completed a self-update
    if (!updatedFromVersion.isEmpty()) {
        QTimer::singleShot(500, w, [w, updatedFromVersion]() {
            w->showPostUpdateDialog(updatedFromVersion);
        });
    }

    int result = a.exec();
    delete w;
    return result;
}
