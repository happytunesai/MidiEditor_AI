#include "McpServer.h"
#include "../AppPaths.h"
#include "ToolDefinitions.h"
#include "EditorContext.h"

#ifdef MIDIEDITOR_COLLAB_ENABLED
#include "../collab/LanLiveSession.h"
#endif

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../gui/MidiPilotWidget.h"
#include "../gui/MainWindow.h"

#include <QJsonDocument>
#include <QRandomGenerator>
#include <QCoreApplication>
#include <QSettings>
#include <QUrl>
#include <QUuid>
#include <QThread>

#include "../MidiEvent/MidiEvent.h"

// MCP protocol version we implement
static const char *MCP_PROTOCOL_VERSION = "2025-03-26";

// JSON-RPC 2.0 error codes
static const int JSONRPC_PARSE_ERROR = -32700;
static const int JSONRPC_INVALID_REQUEST = -32600;
static const int JSONRPC_METHOD_NOT_FOUND = -32601;
static const int JSONRPC_INVALID_PARAMS = -32602;
static const int JSONRPC_INTERNAL_ERROR = -32603;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

McpServer::McpServer(QObject *parent)
    : QObject(parent), _server(new QTcpServer(this)) {
    connect(_server, &QTcpServer::newConnection, this, &McpServer::onNewConnection);

    // Cleanup stale sessions every 5 minutes
    _cleanupTimer.setInterval(300000);
    connect(&_cleanupTimer, &QTimer::timeout, this, &McpServer::cleanupStaleSessions);
}

McpServer::~McpServer() {
    stop();
}

// ---------------------------------------------------------------------------
// Server lifecycle
// ---------------------------------------------------------------------------

bool McpServer::start(quint16 port) {
    if (_server->isListening())
        stop();

    // Bind to localhost only for security
    if (!_server->listen(QHostAddress::LocalHost, port)) {
        emit logMessage(QString("MCP Server failed to start on port %1: %2")
                            .arg(port)
                            .arg(_server->errorString()));
        return false;
    }
    _port = _server->serverPort();
    _cleanupTimer.start();
    emit started(_port);
    emit logMessage(QString("MCP Server listening on localhost:%1").arg(_port));
    return true;
}

void McpServer::stop() {
    if (!_server->isListening())
        return;

    _cleanupTimer.stop();

    // Every session goes. The SSE streams come back to be closed HERE, with
    // the table's lock already released: close() emits disconnected()
    // synchronously, and that handler needs the table. Closing under the lock
    // was a self-deadlock on the GUI thread - the editor never finished
    // quitting (SP-06, external review 2026-09-06).
    const QList<QTcpSocket *> streams = _sessions.clear();
    for (QTcpSocket *stream : streams) {
        if (stream->isOpen()) {
            stream->close();
        }
    }

    _server->close();

    // Drop every client connection as well, not only the listener: a request
    // that already sits in a socket buffer would otherwise still be dispatched
    // by the next event-loop pass - and MainWindow pumps the loop during
    // shutdown AFTER the editor views are gone (SP-01, external review
    // 2026-09-06). abort() discards the buffer and the read notifier. Nothing
    // in here waits: the server lives on the GUI thread, so a stop() from
    // there can never deadlock against a tool call that is blocked in a
    // BlockingQueuedConnection towards this very thread.
    const QList<QTcpSocket *> clients = _server->findChildren<QTcpSocket *>();
    for (QTcpSocket *client : clients) {
        client->abort();
    }
    _pendingData.clear();

    _port = 0;
    emit stopped();
    emit logMessage("MCP Server stopped");
}

bool McpServer::isRunning() const {
    return _server->isListening();
}

quint16 McpServer::port() const {
    return _port;
}

void McpServer::setFile(MidiFile *file) {
    _file = file;
}

void McpServer::forgetFile(MidiFile *file) {
    if (!file) {
        return;
    }
    // Phase 28 (editor groups): a document was closed - drop it as any session's
    // bound document so the next tool call rebinds to the active one instead of
    // acting on a freed file.
    if (_file == file) {
        _file = nullptr;
    }
    _sessions.forgetFile(file);
}

void McpServer::setWidget(MidiPilotWidget *widget) {
    _widget = widget;
}

void McpServer::setAuthToken(const QString &token) {
    _authToken = token;
}

QString McpServer::authToken() const {
    return _authToken;
}

QString McpServer::generateToken() {
    // Generate a URL-safe random token (32 bytes -> 43 chars base64url)
    QByteArray bytes(32, 0);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(bytes.data()),
                                          bytes.size() / sizeof(quint32));
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QString McpServer::clientConfigSnippet() const {
    QJsonObject config;
    config["url"] = QString("http://localhost:%1/mcp").arg(_port);
    if (!_authToken.isEmpty()) {
        QJsonObject headers;
        headers["Authorization"] = QString("Bearer %1").arg(_authToken);
        config["headers"] = headers;
    }

    QJsonObject wrapper;
    wrapper["midieditor"] = config;

    return QJsonDocument(wrapper).toJson(QJsonDocument::Indented);
}

int McpServer::sessionCount() const {
    return _sessions.count();
}

// ---------------------------------------------------------------------------
// Connection handling
// ---------------------------------------------------------------------------

void McpServer::onNewConnection() {
    while (_server->hasPendingConnections()) {
        QTcpSocket *socket = _server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleClient(socket);
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            _pendingData.remove(socket);
            // Forget the socket where it was a session's SSE stream. This
            // runs synchronously from close() - which is why no caller may
            // close a stream while holding the table's lock (SP-06).
            const QString id = _sessions.detachSocket(socket);
            if (!id.isEmpty()) {
                emit clientDisconnected(id);
                emit logMessage(QString("SSE connection closed for session %1").arg(id));
            }
            socket->deleteLater();
        });
    }
}

void McpServer::handleClient(QTcpSocket *socket) {
    // Accumulate data (HTTP request may arrive in multiple chunks)
    _pendingData[socket].append(socket->readAll());
    QByteArray &buf = _pendingData[socket];

    // Check if we have a complete HTTP request (headers + body)
    int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;  // Wait for more data

    // Parse Content-Length to determine if body is complete
    HttpRequest req = parseHttpRequest(buf);
    if (!req.valid) {
        sendErrorResponse(socket, 400, "Bad Request");
        _pendingData.remove(socket);
        return;
    }

    // Check if we have the full body
    int contentLength = req.headers.value("content-length", "0").toInt();

    // Reject oversized requests (MCP-006) - 1 MB limit
    if (contentLength < 0 || contentLength > 1048576) {
        sendErrorResponse(socket, 413, "Request body too large");
        _pendingData.remove(socket);
        return;
    }

    int bodyStart = headerEnd + 4;
    if (buf.size() - bodyStart < contentLength)
        return;  // Wait for more body data

    // Extract the actual body
    req.body = buf.mid(bodyStart, contentLength);
    _pendingData.remove(socket);

    processHttpRequest(socket, req);
}

McpServer::HttpRequest McpServer::parseHttpRequest(const QByteArray &data) {
    HttpRequest req;
    int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0) return req;

    QString headerSection = QString::fromUtf8(data.left(headerEnd));
    QStringList lines = headerSection.split("\r\n");
    if (lines.isEmpty()) return req;

    // Parse request line
    QStringList requestLine = lines[0].split(' ');
    if (requestLine.size() < 3) return req;

    req.method = requestLine[0];
    req.path = requestLine[1];

    // Parse headers
    for (int i = 1; i < lines.size(); ++i) {
        int colonPos = lines[i].indexOf(':');
        if (colonPos > 0) {
            QString key = lines[i].left(colonPos).trimmed().toLower();
            QString value = lines[i].mid(colonPos + 1).trimmed();
            req.headers[key] = value;
        }
    }

    req.valid = true;
    return req;
}

// ---------------------------------------------------------------------------
// HTTP request routing
// ---------------------------------------------------------------------------

void McpServer::processHttpRequest(QTcpSocket *socket, const HttpRequest &req) {
    // Only allow /mcp endpoint
    if (req.path != "/mcp") {
        sendErrorResponse(socket, 404, "Not Found");
        return;
    }

    // Security: validate Origin header (DNS rebinding protection)
    if (!validateOrigin(req)) {
        sendErrorResponse(socket, 403, "Forbidden: invalid Origin");
        emit logMessage("Rejected request: invalid Origin header");
        return;
    }

    // Security: validate auth token
    if (!validateAuth(req)) {
        sendErrorResponse(socket, 401, "Unauthorized");
        emit logMessage("Rejected request: invalid auth token");
        return;
    }

    // Only an Origin that already passed validateOrigin() is ever reflected back
    // in a CORS header; empty means a native client, which needs none.
    const QString reqOrigin = req.headers.value("origin");

    if (req.method == "POST") {
        // JSON-RPC 2.0 request
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(req.body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject err = makeJsonRpcError(QJsonValue::Null, JSONRPC_PARSE_ERROR,
                                               "Parse error: " + parseErr.errorString());
            sendJsonResponse(socket, 200, err, QString(), reqOrigin);
            return;
        }

        QJsonObject rpcRequest = doc.object();

        // Validate JSON-RPC structure
        if (rpcRequest["jsonrpc"].toString() != "2.0" || !rpcRequest.contains("method")) {
            QJsonObject err = makeJsonRpcError(rpcRequest["id"], JSONRPC_INVALID_REQUEST,
                                               "Invalid JSON-RPC 2.0 request");
            sendJsonResponse(socket, 200, err, QString(), reqOrigin);
            return;
        }

        // Find or create session from Mcp-Session-Id header
        QString sessionId = req.headers.value("mcp-session-id");
        QString method = rpcRequest["method"].toString();

        // initialize doesn't need a session yet
        if (method == "initialize") {
            Session newSession;
            newSession.id = createSession();
            newSession.created = QDateTime::currentDateTime();
            newSession.lastActivity = newSession.created;
            newSession.rateLimitWindow = newSession.created;

            QJsonObject result = handleInitialize(rpcRequest["params"].toObject(), newSession);
            QJsonObject response = makeJsonRpcResult(rpcRequest["id"], result);

            _sessions.insert(newSession);

            sendJsonResponse(socket, 200, response, newSession.id, reqOrigin);
            emit clientConnected(newSession.id);
            emit logMessage(QString("New MCP session: %1").arg(newSession.id));
            return;
        }

        // All other methods require a valid session
        if (sessionId.isEmpty()) {
            QJsonObject err = makeJsonRpcError(rpcRequest["id"], JSONRPC_INVALID_REQUEST,
                                               "Missing Mcp-Session-Id header. Call initialize first.");
            sendJsonResponse(socket, 200, err, QString(), reqOrigin);
            return;
        }

        Session *session = findSession(sessionId);
        if (!session) {
            QJsonObject err = makeJsonRpcError(rpcRequest["id"], JSONRPC_INVALID_REQUEST,
                                               "Invalid or expired session. Call initialize again.");
            sendJsonResponse(socket, 200, err, QString(), reqOrigin);
            return;
        }

        session->lastActivity = QDateTime::currentDateTime();

        // Handle notifications (no id = notification, no response needed)
        if (!rpcRequest.contains("id")) {
            // Notifications like "notifications/initialized" - just acknowledge
            // Don't send a response for notifications per JSON-RPC 2.0 spec
            socket->write("HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
            socket->flush();
            return;
        }

        QJsonObject rpcResponse = handleJsonRpc(rpcRequest, *session);
        sendJsonResponse(socket, 200, rpcResponse, sessionId, reqOrigin);

    } else if (req.method == "GET") {
        // SSE stream for server-initiated messages
        QString sessionId = req.headers.value("mcp-session-id");
        if (sessionId.isEmpty()) {
            sendErrorResponse(socket, 400, "Missing Mcp-Session-Id header");
            return;
        }

        Session *session = findSession(sessionId);
        if (!session) {
            sendErrorResponse(socket, 404, "Invalid session");
            return;
        }

        // Register the new stream first, then close the previous one (MCP-004)
        // - outside the table's lock, and after the switch, so the old
        // stream's disconnected handler finds it already replaced and emits no
        // spurious clientDisconnected for a re-established stream.
        QTcpSocket *previous = _sessions.attachSse(sessionId, socket);
        if (previous && previous->isOpen()) {
            previous->close();
        }
        session->lastActivity = QDateTime::currentDateTime();

        // Send SSE headers (keep-alive connection)
        // Same reflection rule as sendJsonResponse - never "*".
        QByteArray headers = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n";
        if (!reqOrigin.isEmpty()) {
            headers.append(QString("Access-Control-Allow-Origin: %1\r\n").arg(reqOrigin).toUtf8());
            headers.append("Vary: Origin\r\n");
        }
        headers.append("\r\n");
        socket->write(headers);
        socket->flush();

        emit logMessage(QString("SSE connection established for session %1").arg(sessionId));

    } else if (req.method == "DELETE") {
        // Session termination
        QString sessionId = req.headers.value("mcp-session-id");
        if (!sessionId.isEmpty()) {
            removeSession(sessionId);
        }
        socket->write("HTTP/1.1 204 No Content\r\n\r\n");
        socket->flush();

    } else if (req.method == "OPTIONS") {
        // CORS preflight - reflect the validated origin, never "*".
        QByteArray resp = "HTTP/1.1 204 No Content\r\n";
        if (!reqOrigin.isEmpty()) {
            resp.append(QString("Access-Control-Allow-Origin: %1\r\n").arg(reqOrigin).toUtf8());
            resp.append("Vary: Origin\r\n");
        }
        resp.append("Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type, Mcp-Session-Id, Authorization\r\n"
                    "Access-Control-Max-Age: 86400\r\n"
                    "\r\n");
        socket->write(resp);
        socket->flush();
    } else {
        sendErrorResponse(socket, 405, "Method Not Allowed");
    }
}

// ---------------------------------------------------------------------------
// HTTP response helpers
// ---------------------------------------------------------------------------

void McpServer::sendJsonResponse(QTcpSocket *socket, int statusCode,
                                  const QJsonObject &body,
                                  const QString &sessionId,
                                  const QString &allowOrigin) {
    QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QByteArray resp;
    resp.append(QString("HTTP/1.1 %1 OK\r\n").arg(statusCode).toUtf8());
    resp.append("Content-Type: application/json\r\n");
    resp.append(QString("Content-Length: %1\r\n").arg(json.size()).toUtf8());
    if (!sessionId.isEmpty()) {
        resp.append(QString("Mcp-Session-Id: %1\r\n").arg(sessionId).toUtf8());
    }
    // WHY: "Access-Control-Allow-Origin: *" plus the exposed Mcp-Session-Id let
    // any browser page read the session id and every response body. Reflect only
    // the origin that already passed validateOrigin(); native clients send no
    // Origin, and then no CORS header is emitted at all.
    if (!allowOrigin.isEmpty()) {
        resp.append(QString("Access-Control-Allow-Origin: %1\r\n").arg(allowOrigin).toUtf8());
        resp.append("Access-Control-Expose-Headers: Mcp-Session-Id\r\n");
        resp.append("Vary: Origin\r\n");
    }
    resp.append("\r\n");
    resp.append(json);

    socket->write(resp);
    socket->flush();
}

void McpServer::sendErrorResponse(QTcpSocket *socket, int httpStatus,
                                   const QString &message) {
    QJsonObject body;
    body["error"] = message;
    QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QString statusText;
    switch (httpStatus) {
    case 400: statusText = "Bad Request"; break;
    case 401: statusText = "Unauthorized"; break;
    case 403: statusText = "Forbidden"; break;
    case 404: statusText = "Not Found"; break;
    case 405: statusText = "Method Not Allowed"; break;
    case 429: statusText = "Too Many Requests"; break;
    default: statusText = "Error"; break;
    }

    QByteArray resp;
    resp.append(QString("HTTP/1.1 %1 %2\r\n").arg(httpStatus).arg(statusText).toUtf8());
    resp.append("Content-Type: application/json\r\n");
    resp.append(QString("Content-Length: %1\r\n").arg(json.size()).toUtf8());
    resp.append("\r\n");
    resp.append(json);

    socket->write(resp);
    socket->flush();
}

void McpServer::sendSseEvent(QTcpSocket *socket, const QJsonObject &data) {
    if (!socket || !socket->isOpen())
        return;

    QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
    QByteArray event = "data: " + json + "\n\n";
    socket->write(event);
    socket->flush();
}

// ---------------------------------------------------------------------------
// JSON-RPC 2.0 dispatcher
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleJsonRpc(const QJsonObject &request, Session &session) {
    QString method = request["method"].toString();
    QJsonValue id = request["id"];
    QJsonObject params = request["params"].toObject();

    if (method == "tools/list") {
        return makeJsonRpcResult(id, handleToolsList(params));
    }
    if (method == "tools/call") {
        // Rate limiting
        if (!checkRateLimit(session)) {
            return makeJsonRpcError(id, -32000, "Rate limit exceeded (100 calls/min). Please slow down.");
        }
        return makeJsonRpcResult(id, handleToolsCall(params, session));
    }
    if (method == "resources/list") {
        return makeJsonRpcResult(id, handleResourcesList(params));
    }
    if (method == "resources/read") {
        return makeJsonRpcResult(id, handleResourcesRead(params, session));
    }
    if (method == "ping") {
        return makeJsonRpcResult(id, QJsonObject());
    }

    return makeJsonRpcError(id, JSONRPC_METHOD_NOT_FOUND,
                            QString("Method not found: %1").arg(method));
}

QJsonObject McpServer::makeJsonRpcError(const QJsonValue &id, int code,
                                         const QString &message) {
    QJsonObject err;
    err["code"] = code;
    err["message"] = message;

    QJsonObject response;
    response["jsonrpc"] = QString("2.0");
    response["id"] = id;
    response["error"] = err;
    return response;
}

QJsonObject McpServer::makeJsonRpcResult(const QJsonValue &id,
                                          const QJsonObject &result) {
    QJsonObject response;
    response["jsonrpc"] = QString("2.0");
    response["id"] = id;
    response["result"] = result;
    return response;
}

// ---------------------------------------------------------------------------
// MCP method: initialize
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleInitialize(const QJsonObject &params, Session &session) {
    // Store client info for Protocol panel display
    QJsonObject clientInfo = params["clientInfo"].toObject();
    QString cName = clientInfo["name"].toString();
    QString cVersion = clientInfo["version"].toString();
    if (!cName.isEmpty()) {
        session.clientName = cVersion.isEmpty() ? cName : cName + " " + cVersion;
    }

    QJsonObject serverInfo;
    serverInfo["name"] = QString("MidiEditor AI");
    serverInfo["version"] = QCoreApplication::applicationVersion();

    QJsonObject capabilities;

    // Tools capability
    QJsonObject toolsCap;
    toolsCap["listChanged"] = true;  // We send notifications when FFXIV mode toggles
    capabilities["tools"] = toolsCap;

    // Resources capability
    QJsonObject resourcesCap;
    resourcesCap["listChanged"] = false;  // Resource list is static
    capabilities["resources"] = resourcesCap;

    QJsonObject result;
    result["protocolVersion"] = QString(MCP_PROTOCOL_VERSION);
    result["serverInfo"] = serverInfo;
    result["capabilities"] = capabilities;
    return result;
}

// ---------------------------------------------------------------------------
// MCP method: tools/list
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleToolsList(const QJsonObject &params) {
    Q_UNUSED(params)

    QJsonObject result;
    result["tools"] = convertToolSchemas();
    return result;
}

// ---------------------------------------------------------------------------
// MCP method: tools/call
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleToolsCall(const QJsonObject &params, Session &session) {
    QString toolName = params["name"].toString();
    QJsonObject args = params["arguments"].toObject();

    if (toolName.isEmpty()) {
        QJsonObject result;
        QJsonArray content;
        QJsonObject textContent;
        textContent["type"] = QString("text");
        textContent["text"] = QString("Error: missing tool name");
        content.append(textContent);
        result["content"] = content;
        result["isError"] = true;
        return result;
    }

    if (!_file) {
        QJsonObject result;
        QJsonArray content;
        QJsonObject textContent;
        textContent["type"] = QString("text");
        textContent["text"] = QString("Error: no MIDI file is currently loaded in MidiEditor");
        content.append(textContent);
        result["content"] = content;
        result["isError"] = true;
        return result;
    }

#ifdef MIDIEDITOR_COLLAB_ENABLED
    // Phase 9.9c §15.2: Show Mode viewer lock. When the local editor is
    // in a Show-mode session and we are NOT the current presenter, all
    // tool calls are refused — there is no "harmless" edit because every
    // tool either mutates MIDI directly or would prompt the user to do
    // so. Read-only state is still reachable via the resources/* MCP
    // methods (midi://state, midi://tracks, midi://config), so external
    // observers stay informed; only the write path is closed.
    {
        LanLiveSession *live = LanLiveSession::instance();
        if (live->role() != LanLiveSession::Role::Idle
            && live->mode() == LanLiveSession::SessionMode::Show
            && !live->isPresenter()) {
            QJsonObject result;
            QJsonArray content;
            QJsonObject textContent;
            textContent["type"] = QString("text");
            textContent["text"] = QStringLiteral(
                "Error: Local editor is in Show Mode (viewer); editing is "
                "locked until you take the hat or the session ends. Use "
                "the midi://state, midi://tracks, and midi://config "
                "resources for read-only access.");
            content.append(textContent);
            result["content"] = content;
            result["isError"] = true;
            return result;
        }
    }
#endif

    // Build source string: "mcp" or "mcp:ClientName Version"
    QString source = session.clientName.isEmpty()
                         ? QStringLiteral("mcp")
                         : QStringLiteral("mcp:") + session.clientName;

    // v2.0 (naming updated v2.4.0): document/tab tools. They act on the
    // WINDOW (which tabs exist / which one is active), not on the session's
    // bound document, so they run BEFORE the bound-file resolution below.
    // After switch_document the client must call get_editor_state to re-bind
    // the session to the newly active document (the binding itself is
    // deliberately untouched). list_documents became a CORE tool in v2.4.0;
    // this intercept still answers it FIRST (shadowing the core executor,
    // whose {success, documents} shape is identical) so it keeps working even
    // while session.boundFileClosed would refuse stateful tools below.
    // switch_document stays MCP's own (activate-the-tab contract) - the
    // MidiPilot runner's rebind-only switch_document is gated out of the
    // default schema and never reaches MCP.
    if (toolName == QStringLiteral("list_documents")
        || toolName == QStringLiteral("switch_document")) {
        QJsonObject toolResult;
        auto runDocTool = [&]() {
            MainWindow *mw = _widget
                ? qobject_cast<MainWindow *>(_widget->window())
                : nullptr;
            if (!mw) {
                toolResult["success"] = false;
                toolResult["error"] = QStringLiteral("Main window not available.");
                return;
            }
            // A call that was queued before beginShutdown() still runs when the
            // loop is pumped during teardown - refuse it here as well, the
            // editor views it would activate are gone (SP-01).
            if (mw->isShuttingDown()) {
                toolResult["success"] = false;
                toolResult["error"] = QStringLiteral("The editor is shutting down.");
                return;
            }
            if (toolName == QStringLiteral("list_documents")) {
                toolResult["success"] = true;
                toolResult["documents"] = mw->listOpenDocumentsJson();
            } else {
                const int index = args.value(QStringLiteral("index")).toInt(-1);
                if (mw->activateDocumentByListIndex(index)) {
                    toolResult["success"] = true;
                    toolResult["activeDocumentIndex"] = index;
                    toolResult["note"] = QStringLiteral(
                        "Document activated. Call get_editor_state to bind "
                        "this session to the newly active document.");
                } else {
                    toolResult["success"] = false;
                    toolResult["error"] =
                        QStringLiteral("Invalid document index - call "
                                       "list_documents for the current list.");
                }
            }
        };
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
            runDocTool();
        } else {
            QMetaObject::invokeMethod(this, [&]() { runDocTool(); },
                                      Qt::BlockingQueuedConnection);
        }
        session.toolCallCount++;
        emit toolCalled(session.id, toolName);

        QJsonObject result;
        QJsonArray content;
        QJsonObject textContent;
        textContent["type"] = QString("text");
        textContent["text"] = QString::fromUtf8(
            QJsonDocument(toolResult).toJson(QJsonDocument::Compact));
        content.append(textContent);
        result["content"] = content;
        if (!toolResult.value(QStringLiteral("success")).toBool()) {
            result["isError"] = true;
        }
        return result;
    }

    // Phase 28 (editor groups): resolve which document this call acts on. The
    // session binds to a document so a read (get_selection) and a later write
    // (delete_events_by_index) stay on the SAME document even if the user
    // switches tabs in between - mirroring MidiPilot's run-origin behaviour.
    // get_editor_state is the explicit resync point: it re-binds to whatever is
    // active now (so the client can intentionally move to another document);
    // every other tool acts on the bound document (bound to active on first use).
    //
    // If the bound document was CLOSED mid-conversation (forgetFile set the flag),
    // refuse a stateful tool call rather than silently retargeting it onto whatever
    // is now active - the client's indices/payload were computed for the old doc.
    // The client must call get_editor_state to re-read state and re-bind.
    if (session.boundFileClosed && toolName != QStringLiteral("get_editor_state")) {
        QJsonObject result;
        QJsonArray content;
        QJsonObject textContent;
        textContent["type"] = QString("text");
        textContent["text"] = QStringLiteral(
            "Error: the document this session was working on was closed. Call "
            "get_editor_state to re-read the editor state and re-bind before editing.");
        content.append(textContent);
        result["content"] = content;
        result["isError"] = true;
        return result;
    }
    if (toolName == QStringLiteral("get_editor_state") || !session.boundFile) {
        session.boundFile = _file;
        session.boundFileClosed = false; // resynced to the active document
    }
    MidiFile *targetFile = session.boundFile;

    // Execute the tool on the main thread (thread safety for MIDI data)
    QJsonObject toolResult;
    bool executed = false;

    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        // Already on main thread
        toolResult = ToolDefinitions::executeTool(toolName, args, targetFile, _widget, source);
        executed = true;
    } else {
        // Cross-thread invocation
        QMetaObject::invokeMethod(this, [&]() {
            toolResult = ToolDefinitions::executeTool(toolName, args, targetFile, _widget, source);
            executed = true;
        }, Qt::BlockingQueuedConnection);
    }

    session.toolCallCount++;
    emit toolCalled(session.id, toolName);

    // Convert tool result to MCP content format
    QJsonObject result;
    QJsonArray content;
    QJsonObject textContent;
    textContent["type"] = QString("text");
    textContent["text"] = QString::fromUtf8(QJsonDocument(toolResult).toJson(QJsonDocument::Compact));
    content.append(textContent);
    result["content"] = content;

    if (toolResult.contains("success") && !toolResult["success"].toBool()) {
        result["isError"] = true;
    }

    return result;
}

// ---------------------------------------------------------------------------
// MCP method: resources/list (Phase 23.5b)
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleResourcesList(const QJsonObject &params) {
    Q_UNUSED(params)

    QJsonArray resources;

    // midi://state - current editor state
    {
        QJsonObject res;
        res["uri"] = QString("midi://state");
        res["name"] = QString("Editor State");
        res["description"] = QString("Current editor state including file info, tracks, cursor, tempo, and time signature");
        res["mimeType"] = QString("application/json");
        resources.append(res);
    }

    // midi://tracks - track list
    {
        QJsonObject res;
        res["uri"] = QString("midi://tracks");
        res["name"] = QString("Track List");
        res["description"] = QString("All tracks with names, channels, and event counts");
        res["mimeType"] = QString("application/json");
        resources.append(res);
    }

    // midi://config - configuration
    {
        QJsonObject res;
        res["uri"] = QString("midi://config");
        res["name"] = QString("Configuration");
        res["description"] = QString("FFXIV mode status, file path, ticks per beat, and tempo");
        res["mimeType"] = QString("application/json");
        resources.append(res);
    }

    // midi://ffxiv-guide - the FFXIV arrangement guide (Phase 46). External
    // clients never see MidiPilot's system prompt, so without this resource
    // they arrange blind: the octet experiment produced single-variant
    // guitars and upward folds because nothing told it otherwise. Same text
    // the built-in agent gets.
    {
        QJsonObject res;
        res["uri"] = QString("midi://ffxiv-guide");
        res["name"] = QString("FFXIV Arrangement Guide");
        res["description"] = QString(
            "Rules AND craft for FFXIV bard arrangements: hard constraints "
            "(8 tracks, monophonic, C3-C6, exact instrument names), drums, "
            "guitar variant switches, register/density guidance, tool order. "
            "READ THIS before arranging for FFXIV.");
        res["mimeType"] = QString("text/markdown");
        resources.append(res);
    }

    QJsonObject result;
    result["resources"] = resources;
    return result;
}

// ---------------------------------------------------------------------------
// MCP method: resources/read (Phase 23.5b)
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleResourcesRead(const QJsonObject &params, Session &session) {
    QString uri = params["uri"].toString();

    QJsonObject result;
    QJsonArray contents;

    // Read the SAME document the session's tools act on, so a client mixing
    // resources/read with tool calls stays coherent after a tab switch. Tool
    // calls bind to session.boundFile (bound to the active document on first
    // use); a plain read before any tool call - or after the bound document was
    // closed - falls back to whatever is active now.
    MidiFile *target = session.boundFile ? session.boundFile : _file;

    if (uri == "midi://state") {
        QJsonObject content;
        content["uri"] = uri;
        content["mimeType"] = QString("application/json");

        if (target) {
            QJsonObject state = EditorContext::captureState(target);
            content["text"] = QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
        } else {
            content["text"] = QString("{}");
        }
        contents.append(content);

    } else if (uri == "midi://tracks") {
        QJsonObject content;
        content["uri"] = uri;
        content["mimeType"] = QString("application/json");

        QJsonArray tracks;
        if (target) {
            for (int i = 0; i < target->tracks()->size(); ++i) {
                MidiTrack *track = target->tracks()->at(i);
                QJsonObject t;
                t["index"] = i;
                t["name"] = track->name();
                // Count events on this track
                int eventCount = 0;
                for (int ch = 0; ch < 17; ++ch) {
                    auto *events = target->channelEvents(ch);
                    if (!events) continue;
                    for (auto it = events->begin(); it != events->end(); ++it) {
                        if (it.value()->track() == track)
                            eventCount++;
                    }
                }
                t["eventCount"] = eventCount;
                tracks.append(t);
            }
        }
        content["text"] = QString::fromUtf8(QJsonDocument(tracks).toJson(QJsonDocument::Compact));
        contents.append(content);

    } else if (uri == "midi://config") {
        QJsonObject content;
        content["uri"] = uri;
        content["mimeType"] = QString("application/json");

        QJsonObject config;
        config["ffxivMode"] = AppPaths::settings()->value("AI/ffxiv_mode", false).toBool();
        config["filePath"] = target ? target->path() : QString();
        config["ticksPerBeat"] = target ? target->ticksPerQuarter() : 480;
        // Tempo is part of the state, but provide a quick reference
        if (target) {
            config["tempo"] = EditorContext::captureState(target)["tempo"];
        }
        content["text"] = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
        contents.append(content);

    } else if (uri == "midi://ffxiv-guide") {
        // Phase 46: the same rules-and-craft text the built-in agent gets in
        // its system prompt - external clients otherwise arrange blind.
        QJsonObject content;
        content["uri"] = uri;
        content["mimeType"] = QString("text/markdown");
        content["text"] = EditorContext::ffxivContext();
        contents.append(content);

    } else {
        QJsonObject content;
        content["uri"] = uri;
        content["mimeType"] = QString("text/plain");
        content["text"] = QString("Unknown resource: %1").arg(uri);
        contents.append(content);
    }

    result["contents"] = contents;
    return result;
}

// ---------------------------------------------------------------------------
// Security (Phase 23.5d)
// ---------------------------------------------------------------------------

bool McpServer::validateOrigin(const HttpRequest &req) {
    // MCP spec: validate Origin header to prevent DNS rebinding attacks
    QString origin = req.headers.value("origin");

    // No Origin header = direct tool call (curl, etc.) - allow
    if (origin.isEmpty())
        return true;

    // WHY: browsers send the opaque origin "null" for sandboxed iframes, data:
    // documents AND file:// pages, so allowing "null" (or the "file://" prefix)
    // let any web page or downloaded .html reach the whole tool surface. Only a
    // real local origin may pass; native clients send no Origin at all.
    if (origin == "null" || origin.startsWith("file://"))
        return false;

    // Allow vscode-webview and other IDE origins
    if (origin.startsWith("vscode-webview://")) {
        return true;
    }

    // Parse as URL and check host to prevent DNS rebinding (MCP-001)
    // startsWith("http://localhost") would also match localhost.evil.com
    QUrl url(origin);
    QString host = url.host();
    if (host == "localhost" || host == "127.0.0.1" || host == "::1")
        return true;

    return false;
}

bool McpServer::validateAuth(const HttpRequest &req) {
    // No token configured = no auth required
    if (_authToken.isEmpty())
        return true;

    QString auth = req.headers.value("authorization");
    if (auth.isEmpty())
        return false;

    // Expect "Bearer <token>"
    if (!auth.startsWith("Bearer ", Qt::CaseInsensitive))
        return false;

    QString token = auth.mid(7).trimmed();

    // Constant-time comparison to prevent timing attacks (MCP-005)
    // Pad to same length to avoid leaking token length via early return
    int maxLen = qMax(token.length(), _authToken.length());
    int diff = token.length() ^ _authToken.length();  // differ if lengths mismatch
    for (int i = 0; i < maxLen; ++i) {
        ushort a = (i < token.length()) ? token[i].unicode() : 0;
        ushort b = (i < _authToken.length()) ? _authToken[i].unicode() : 0;
        diff |= (a ^ b);
    }
    return diff == 0;
}

bool McpServer::checkRateLimit(Session &session) {
    QDateTime now = QDateTime::currentDateTime();

    // Reset window if more than 60 seconds have passed
    if (session.rateLimitWindow.secsTo(now) >= 60) {
        session.toolCallCount = 0;
        session.rateLimitWindow = now;
    }

    return session.toolCallCount < MAX_RATE_PER_MINUTE;
}

// ---------------------------------------------------------------------------
// Tool schema conversion (OpenAI format -> MCP format)
// ---------------------------------------------------------------------------

QJsonArray McpServer::convertToolSchemas() {
    QJsonArray openAiTools = ToolDefinitions::toolSchemas();
    QJsonArray mcpTools;

    for (const QJsonValue &val : openAiTools) {
        QJsonObject tool = val.toObject();
        QJsonObject func = tool["function"].toObject();

        QJsonObject mcpTool;
        mcpTool["name"] = func["name"];
        mcpTool["description"] = func["description"];

        // inputSchema = the parameters object (already JSON Schema)
        QJsonObject inputSchema = func["parameters"].toObject();
        // Remove OpenAI-specific "strict" field
        inputSchema.remove("strict");
        mcpTool["inputSchema"] = inputSchema;

        mcpTools.append(mcpTool);
    }

    // v2.4.0: list_documents is a CORE tool now (promoted for MidiPilot's
    // cross-tab abilities) and flows through the conversion above with the
    // SAME description it had as an MCP-only append, so clients see one
    // identical tool instead of two. The pre-dispatch intercept in
    // handleToolCall (before bound-file resolution) still answers it - the
    // core executor is byte-equivalent ({success, documents}) but the
    // intercept keeps list_documents working even while the bound document
    // is closed. Only switch_document stays MCP-appended: MCP's contract
    // (activate the tab in the UI, then get_editor_state re-binds) is
    // deliberately different from the MidiPilot runner's rebind-only
    // switch_document, which is gated off in the default schema options so
    // the two definitions never shadow each other here.
    {
        QJsonObject t;
        t["name"] = QStringLiteral("switch_document");
        t["description"] = QStringLiteral(
            "Activate the open document (tab) with the given index from "
            "list_documents. IMPORTANT: afterwards call get_editor_state to "
            "bind this session to the newly active document - stateful tools "
            "keep acting on the previously bound document until then.");
        QJsonObject props;
        props["index"] = QJsonObject{
            {"type", "integer"},
            {"description", "Document index from list_documents."}};
        QJsonObject schema;
        schema["type"] = QStringLiteral("object");
        schema["properties"] = props;
        QJsonArray required;
        required.append(QStringLiteral("index"));
        schema["required"] = required;
        t["inputSchema"] = schema;
        mcpTools.append(t);
    }

    return mcpTools;
}

// ---------------------------------------------------------------------------
// SSE notification: tools/list changed (FFXIV mode toggle)
// ---------------------------------------------------------------------------

void McpServer::broadcastToolsChanged() {
    QJsonObject notification;
    notification["jsonrpc"] = QString("2.0");
    notification["method"] = QString("notifications/tools/list_changed");

    // Snapshot first, write outside the table's lock: a write to a stream
    // whose peer is gone can fail and close the socket synchronously, and its
    // disconnected handler needs the table (SP-06). sendSseEvent() skips
    // streams that are no longer open.
    const QList<QTcpSocket *> streams = _sessions.sseSockets();
    for (QTcpSocket *stream : streams) {
        sendSseEvent(stream, notification);
    }
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

QString McpServer::createSession() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

McpServer::Session *McpServer::findSession(const QString &id) {
    return _sessions.find(id);
}

void McpServer::removeSession(const QString &id) {
    // The table hands the SSE stream back; it is closed here, after the lock
    // was released (SP-06).
    McpSessionTable::Removed removed;
    if (!_sessions.remove(id, removed)) {
        return;
    }
    if (removed.sseSocket && removed.sseSocket->isOpen()) {
        removed.sseSocket->close();
    }
    emit clientDisconnected(id);
    emit logMessage(QString("Session removed: %1").arg(id));
}

void McpServer::cleanupStaleSessions() {
    // Same discipline as removeSession(): expire under the lock, close after.
    const QList<McpSessionTable::Removed> expired =
        _sessions.expire(QDateTime::currentDateTime(), SESSION_TIMEOUT_SECS);
    for (const McpSessionTable::Removed &session : expired) {
        if (session.sseSocket && session.sseSocket->isOpen()) {
            session.sseSocket->close();
        }
        emit logMessage(QString("Session expired: %1").arg(session.id));
    }
}
