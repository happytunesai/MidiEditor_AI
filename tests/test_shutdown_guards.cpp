/*
 * test_shutdown_guards
 *
 * SP-01 (external system/performance review, 2026-09-06): closing the editor
 * while an MCP client sent switch_document (or with a tab click queued) could
 * crash it - performEarlyCleanup() destroys the editor views, nulls their
 * pointers and then pumps the event loop, and the tab-activation path ended
 * in bindPrimaryView() dereferencing the null primary view.
 *
 * MainWindow cannot be instantiated headless, so - like test_opengl_paint_guard
 * for GLBLANK-001 - this pins the shutdown ORDER and the guards at source
 * level, so none of them can be dropped again unnoticed:
 *   - closeEvent() begins the shutdown before anything pumps the loop
 *   - beginShutdown() raises the flag, stops the MCP server and aborts the
 *     agent request; performEarlyCleanup() reaches it on the destructor path
 *   - the tab slots and activateDocumentByListIndex() honour the flag
 *   - bindPrimaryView() tolerates a torn-down primary view
 *   - McpServer::stop() drops its client sockets and never waits on a thread
 *     (a GUI-thread stop() must not block against a BlockingQueuedConnection
 *     that is itself waiting for the GUI thread)
 *   - the MCP document intercept refuses work once shutdown began, so a call
 *     that was already queued sees the state when it finally runs
 *
 * SHUTDOWN_REPO_ROOT is injected by CMake for the source scan.
 */

#include <QtTest/QtTest>
#include <QFile>
#include <QString>
#include <QStringList>

namespace {

QString readRepoFile(const QString &relativePath)
{
    QFile f(QStringLiteral(SHUTDOWN_REPO_ROOT "/") + relativePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    // The sources are checked out with mixed line endings (autocrlf); the
    // body scan below keys on "\n}\n", so normalise first.
    return QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

/** The body of the function whose definition starts with `signature`: from
 *  the signature to the first closing brace in column 0. */
QString functionBody(const QString &src, const QString &signature)
{
    const int start = src.indexOf(signature);
    if (start < 0) {
        return QString();
    }
    const int end = src.indexOf(QStringLiteral("\n}\n"), start);
    if (end < 0) {
        return QString();
    }
    return src.mid(start, end - start);
}

} // namespace

class TestShutdownGuards : public QObject {
    Q_OBJECT

private:
    QString _mainWindow;
    QString _mcpServer;

private slots:
    void initTestCase()
    {
        _mainWindow = readRepoFile(QStringLiteral("src/gui/MainWindow.cpp"));
        QVERIFY2(!_mainWindow.isEmpty(), "src/gui/MainWindow.cpp is not readable");
        _mcpServer = readRepoFile(QStringLiteral("src/ai/McpServer.cpp"));
        QVERIFY2(!_mcpServer.isEmpty(), "src/ai/McpServer.cpp is not readable");
    }

    void closeEventBeginsShutdownBeforeTeardown()
    {
        const QString body = functionBody(
            _mainWindow, QStringLiteral("void MainWindow::closeEvent(QCloseEvent *event)"));
        QVERIFY2(!body.isEmpty(), "closeEvent() definition not found");
        // The CALLS (with the statement's semicolon): comments in closeEvent()
        // mention performEarlyCleanup() long before the teardown block.
        const int begin = body.indexOf(QStringLiteral("beginShutdown();"));
        const int cleanup = body.indexOf(QStringLiteral("performEarlyCleanup();"));
        QVERIFY2(begin >= 0, "closeEvent() no longer calls beginShutdown()");
        QVERIFY2(cleanup >= 0, "closeEvent() no longer calls performEarlyCleanup()");
        QVERIFY2(begin < cleanup,
                 "beginShutdown() must run BEFORE performEarlyCleanup() pumps the "
                 "event loop with the editor views already destroyed (SP-01)");
    }

    void earlyCleanupBeginsShutdownOnTheDestructorPath()
    {
        const QString body = functionBody(
            _mainWindow, QStringLiteral("void MainWindow::performEarlyCleanup()"));
        QVERIFY2(!body.isEmpty(), "performEarlyCleanup() definition not found");
        QVERIFY2(body.contains(QStringLiteral("beginShutdown()")),
                 "performEarlyCleanup() must begin the shutdown itself - the "
                 "destructor reaches it without closeEvent()");
    }

    void beginShutdownRaisesTheFlagAndStopsTheSources()
    {
        const QString body = functionBody(
            _mainWindow, QStringLiteral("void MainWindow::beginShutdown()"));
        QVERIFY2(!body.isEmpty(), "beginShutdown() definition not found");
        QVERIFY2(body.contains(QStringLiteral("_shuttingDown = true")),
                 "beginShutdown() no longer raises _shuttingDown");
        QVERIFY2(body.contains(QStringLiteral("_mcpServer->stop()")),
                 "beginShutdown() no longer stops the MCP server");
        QVERIFY2(body.contains(QStringLiteral("abortActiveRequest()")),
                 "beginShutdown() no longer aborts the MidiPilot request");
    }

    void documentActivationHonoursTheFlag()
    {
        const QStringList signatures = {
            QStringLiteral("void MainWindow::onDocumentTabChanged(int index)"),
            QStringLiteral("void MainWindow::onGroup1TabChanged(int index)"),
            QStringLiteral("bool MainWindow::activateDocumentByListIndex(int index)"),
        };
        for (const QString &signature : signatures) {
            const QString body = functionBody(_mainWindow, signature);
            QVERIFY2(!body.isEmpty(), qPrintable(signature + " not found"));
            QVERIFY2(body.contains(QStringLiteral("_shuttingDown")),
                     qPrintable(signature + " no longer checks _shuttingDown"));
        }
    }

    void bindPrimaryViewToleratesTornDownView()
    {
        const QString body = functionBody(
            _mainWindow, QStringLiteral("void MainWindow::bindPrimaryView(MidiFile *f)"));
        QVERIFY2(!body.isEmpty(), "bindPrimaryView() definition not found");
        QVERIFY2(body.contains(QStringLiteral("else if (mw_matrixWidget)")),
                 "bindPrimaryView() dereferences mw_matrixWidget unguarded again - "
                 "it is null after performEarlyCleanup()");
    }

    void mcpStopDropsClientsWithoutWaiting()
    {
        const QString body = functionBody(_mcpServer, QStringLiteral("void McpServer::stop()"));
        QVERIFY2(!body.isEmpty(), "McpServer::stop() definition not found");
        QVERIFY2(body.contains(QStringLiteral("abort()")),
                 "McpServer::stop() no longer aborts its client sockets - a buffered "
                 "request would still be dispatched by the shutdown event pump");
        QVERIFY2(!body.contains(QStringLiteral("wait(")),
                 "McpServer::stop() must never wait on another thread: called from "
                 "the GUI thread it would deadlock against a tool call blocked in a "
                 "BlockingQueuedConnection towards the GUI thread");
        // The code form - the explanatory comment in stop() names the mechanism.
        QVERIFY2(!body.contains(QStringLiteral("Qt::BlockingQueuedConnection")),
                 "McpServer::stop() must not block on a queued call");
    }

    // SP-06: the session lock lives in McpSessionTable, which never calls into a
    // socket - so no caller can close a stream while holding it, and the
    // disconnected handler (which takes the lock) cannot re-enter it.
    void sessionLockingIsConfinedToTheTableAndTheTableNeverTouchesSockets()
    {
        QVERIFY2(!_mcpServer.contains(QStringLiteral("_sessionMutex"))
                     && !_mcpServer.contains(QStringLiteral("QMutexLocker")),
                 "McpServer.cpp locks the session table itself again - closing a "
                 "stream under that lock re-enters it from the disconnected handler "
                 "(the shutdown deadlock, SP-06)");
        const QString table = readRepoFile(QStringLiteral("src/ai/McpSessionTable.cpp"));
        QVERIFY2(!table.isEmpty(), "src/ai/McpSessionTable.cpp is not readable");
        const QStringList socketCalls = {
            QStringLiteral("->close("), QStringLiteral("->abort("),
            QStringLiteral("->write("), QStringLiteral("->flush("),
            QStringLiteral("->disconnectFromHost("), QStringLiteral("deleteLater("),
        };
        for (const QString &call : socketCalls) {
            QVERIFY2(!table.contains(call),
                     qPrintable(QStringLiteral("McpSessionTable.cpp calls into a socket (")
                                + call + QStringLiteral(") - it must hand sockets back instead")));
        }
    }

    void mcpDocumentInterceptRefusesDuringShutdown()
    {
        const int start = _mcpServer.indexOf(QStringLiteral("auto runDocTool"));
        QVERIFY2(start >= 0, "MCP document intercept (runDocTool) not found");
        const int end = _mcpServer.indexOf(QStringLiteral("runDocTool();"), start);
        QVERIFY2(end > start, "MCP document intercept is never invoked");
        const QString intercept = _mcpServer.mid(start, end - start);
        QVERIFY2(intercept.contains(QStringLiteral("isShuttingDown()")),
                 "the MCP list_documents / switch_document intercept no longer "
                 "refuses work during shutdown");
    }
};

QTEST_GUILESS_MAIN(TestShutdownGuards)
#include "test_shutdown_guards.moc"
