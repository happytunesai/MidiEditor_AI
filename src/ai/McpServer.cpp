#include "McpServer.h"
#include "ToolDefinitions.h"
#include "EditorContext.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../midi/MidiChannel.h"
#include "../gui/MidiPilotWidget.h"

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

    // Close all SSE connections
    QMutexLocker lock(&_sessionMutex);
    for (auto &session : _sessions) {
        if (session.sseSocket && session.sseSocket->isOpen()) {
            session.sseSocket->close();
        }
    }
    _sessions.clear();
    lock.unlock();

    _server->close();
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
    QMutexLocker lock(&_sessionMutex);
    return _sessions.size();
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
            // Remove any SSE session associated with this socket
            QMutexLocker lock(&_sessionMutex);
            for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
                if (it->sseSocket == socket) {
                    QString id = it.key();
                    it->sseSocket = nullptr;
                    emit clientDisconnected(id);
                    emit logMessage(QString("SSE connection closed for session %1").arg(id));
                    break;
                }
            }
            lock.unlock();
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

    if (req.method == "POST") {
        // JSON-RPC 2.0 request
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(req.body, &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject err = makeJsonRpcError(QJsonValue::Null, JSONRPC_PARSE_ERROR,
                                               "Parse error: " + parseErr.errorString());
            sendJsonResponse(socket, 200, err);
            return;
        }

        QJsonObject rpcRequest = doc.object();

        // Validate JSON-RPC structure
        if (rpcRequest["jsonrpc"].toString() != "2.0" || !rpcRequest.contains("method")) {
            QJsonObject err = makeJsonRpcError(rpcRequest["id"], JSONRPC_INVALID_REQUEST,
                                               "Invalid JSON-RPC 2.0 request");
            sendJsonResponse(socket, 200, err);
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

            QMutexLocker lock(&_sessionMutex);
            _sessions[newSession.id] = newSession;
            lock.unlock();

            sendJsonResponse(socket, 200, response, newSession.id);
            emit clientConnected(newSession.id);
            emit logMessage(QString("New MCP session: %1").arg(newSession.id));
            return;
        }

        // All other methods require a valid session
        if (sessionId.isEmpty()) {
            QJsonObject err = makeJsonRpcError(rpcRequest["id"], JSONRPC_INVALID_REQUEST,
                                               "Missing Mcp-Session-Id header. Call initialize first.");
            sendJsonResponse(socket, 200, err);
            return;
        }

        Session *session = findSession(sessionId);
        if (!session) {
            QJsonObject err = makeJsonRpcError(rpcRequest["id"], JSONRPC_INVALID_REQUEST,
                                               "Invalid or expired session. Call initialize again.");
            sendJsonResponse(socket, 200, err);
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
        sendJsonResponse(socket, 200, rpcResponse, sessionId);

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

        // Close previous SSE socket if any (MCP-004)
        if (session->sseSocket && session->sseSocket != socket
            && session->sseSocket->isOpen()) {
            session->sseSocket->close();
        }

        // Set up SSE connection
        session->sseSocket = socket;
        session->lastActivity = QDateTime::currentDateTime();

        // Send SSE headers (keep-alive connection)
        QByteArray headers = "HTTP/1.1 200 OK\r\n"
                             "Content-Type: text/event-stream\r\n"
                             "Cache-Control: no-cache\r\n"
                             "Connection: keep-alive\r\n"
                             "Access-Control-Allow-Origin: *\r\n"
                             "\r\n";
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
        // CORS preflight
        QByteArray resp = "HTTP/1.1 204 No Content\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Content-Type, Mcp-Session-Id, Authorization\r\n"
                          "Access-Control-Max-Age: 86400\r\n"
                          "\r\n";
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
                                  const QString &sessionId) {
    QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QByteArray resp;
    resp.append(QString("HTTP/1.1 %1 OK\r\n").arg(statusCode).toUtf8());
    resp.append("Content-Type: application/json\r\n");
    resp.append(QString("Content-Length: %1\r\n").arg(json.size()).toUtf8());
    if (!sessionId.isEmpty()) {
        resp.append(QString("Mcp-Session-Id: %1\r\n").arg(sessionId).toUtf8());
    }
    resp.append("Access-Control-Allow-Origin: *\r\n");
    resp.append("Access-Control-Expose-Headers: Mcp-Session-Id\r\n");
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
        return makeJsonRpcResult(id, handleResourcesRead(params));
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

    // Build source string: "mcp" or "mcp:ClientName Version"
    QString source = session.clientName.isEmpty()
                         ? QStringLiteral("mcp")
                         : QStringLiteral("mcp:") + session.clientName;

    // Execute the tool on the main thread (thread safety for MIDI data)
    QJsonObject toolResult;
    bool executed = false;

    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        // Already on main thread
        toolResult = ToolDefinitions::executeTool(toolName, args, _file, _widget, source);
        executed = true;
    } else {
        // Cross-thread invocation
        QMetaObject::invokeMethod(this, [&]() {
            toolResult = ToolDefinitions::executeTool(toolName, args, _file, _widget, source);
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

    QJsonObject result;
    result["resources"] = resources;
    return result;
}

// ---------------------------------------------------------------------------
// MCP method: resources/read (Phase 23.5b)
// ---------------------------------------------------------------------------

QJsonObject McpServer::handleResourcesRead(const QJsonObject &params) {
    QString uri = params["uri"].toString();

    QJsonObject result;
    QJsonArray contents;

    if (uri == "midi://state") {
        QJsonObject content;
        content["uri"] = uri;
        content["mimeType"] = QString("application/json");

        if (_file) {
            QJsonObject state = EditorContext::captureState(_file);
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
        if (_file) {
            for (int i = 0; i < _file->tracks()->size(); ++i) {
                MidiTrack *track = _file->tracks()->at(i);
                QJsonObject t;
                t["index"] = i;
                t["name"] = track->name();
                // Count events on this track
                int eventCount = 0;
                for (int ch = 0; ch < 17; ++ch) {
                    auto *events = _file->channelEvents(ch);
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
        QSettings settings("MidiEditor", "NONE");
        config["ffxivMode"] = settings.value("AI/ffxiv_mode", false).toBool();
        config["filePath"] = _file ? _file->path() : QString();
        config["ticksPerBeat"] = _file ? _file->ticksPerQuarter() : 480;
        // Tempo is part of the state, but provide a quick reference
        if (_file) {
            config["tempo"] = EditorContext::captureState(_file)["tempo"];
        }
        content["text"] = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
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

    // "null" origin = sandboxed context - allow
    if (origin == "null")
        return true;

    // Allow vscode-webview and other IDE origins
    if (origin.startsWith("vscode-webview://") ||
        origin.startsWith("file://")) {
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

    return mcpTools;
}

// ---------------------------------------------------------------------------
// SSE notification: tools/list changed (FFXIV mode toggle)
// ---------------------------------------------------------------------------

void McpServer::broadcastToolsChanged() {
    QJsonObject notification;
    notification["jsonrpc"] = QString("2.0");
    notification["method"] = QString("notifications/tools/list_changed");

    QMutexLocker lock(&_sessionMutex);
    for (auto &session : _sessions) {
        if (session.sseSocket && session.sseSocket->isOpen()) {
            sendSseEvent(session.sseSocket, notification);
        }
    }
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

QString McpServer::createSession() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

McpServer::Session *McpServer::findSession(const QString &id) {
    QMutexLocker lock(&_sessionMutex);
    auto it = _sessions.find(id);
    if (it == _sessions.end())
        return nullptr;
    return &it.value();
}

void McpServer::removeSession(const QString &id) {
    QMutexLocker lock(&_sessionMutex);
    auto it = _sessions.find(id);
    if (it != _sessions.end()) {
        if (it->sseSocket && it->sseSocket->isOpen()) {
            it->sseSocket->close();
        }
        _sessions.erase(it);
        emit clientDisconnected(id);
        emit logMessage(QString("Session removed: %1").arg(id));
    }
}

void McpServer::cleanupStaleSessions() {
    QDateTime now = QDateTime::currentDateTime();
    QMutexLocker lock(&_sessionMutex);
    QStringList toRemove;

    for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
        if (it->lastActivity.secsTo(now) > SESSION_TIMEOUT_SECS) {
            toRemove.append(it.key());
        }
    }

    for (const QString &id : toRemove) {
        auto it = _sessions.find(id);
        if (it != _sessions.end()) {
            if (it->sseSocket && it->sseSocket->isOpen()) {
                it->sseSocket->close();
            }
            _sessions.erase(it);
            emit logMessage(QString("Session expired: %1").arg(id));
        }
    }
}
