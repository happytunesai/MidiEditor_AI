#ifndef MCPSERVER_H
#define MCPSERVER_H

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QDateTime>
#include <QTimer>
#include <QMutex>

class MidiFile;
class MidiPilotWidget;

/**
 * \class McpServer
 *
 * \brief MCP (Model Context Protocol) server for MidiEditor AI.
 *
 * Exposes MidiEditor's MIDI editing tools via the MCP 2025-03-26 protocol
 * so that any MCP-compatible client (Claude Desktop, VS Code Copilot, Cursor,
 * etc.) can discover and invoke them.
 *
 * Transport: Streamable HTTP over a single /mcp endpoint.
 * - POST /mcp -> JSON-RPC 2.0 request/response
 * - GET  /mcp -> SSE stream for server-initiated notifications
 *
 * Security: localhost-only, Origin header validation, optional Bearer token,
 * rate limiting (100 calls/min per session).
 */
class McpServer : public QObject {
    Q_OBJECT

public:
    explicit McpServer(QObject *parent = nullptr);
    ~McpServer() override;

    /** Start listening on the configured port. Returns true on success. */
    bool start(quint16 port = 9420);

    /** Stop the server and disconnect all clients. */
    void stop();

    /** Whether the server is currently listening. */
    bool isRunning() const;

    /** The port the server is listening on (0 if not running). */
    quint16 port() const;

    /** Set the current MIDI file reference (updated on file load). */
    void setFile(MidiFile *file);

    /** Set the MidiPilot widget reference (for tool execution). */
    void setWidget(MidiPilotWidget *widget);

    /** Set the authentication token. Empty = no auth required. */
    void setAuthToken(const QString &token);
    QString authToken() const;

    /** Generate a random auth token. */
    static QString generateToken();

    /** Get the MCP client config JSON snippet for user convenience. */
    QString clientConfigSnippet() const;

    /** Number of active sessions. */
    int sessionCount() const;

signals:
    void started(quint16 port);
    void stopped();
    void clientConnected(const QString &sessionId);
    void clientDisconnected(const QString &sessionId);
    void toolCalled(const QString &sessionId, const QString &toolName);
    void logMessage(const QString &message);

private slots:
    void onNewConnection();

private:
    // HTTP parsing
    struct HttpRequest {
        QString method;        // GET, POST, DELETE
        QString path;          // /mcp
        QMap<QString, QString> headers;
        QByteArray body;
        bool valid = false;
    };

    struct Session {
        QString id;
        QString clientName;  // e.g. "VS Code Copilot 1.0"
        QTcpSocket *sseSocket = nullptr;  // SSE connection (GET /mcp)
        QDateTime created;
        QDateTime lastActivity;
        int toolCallCount = 0;
        QDateTime rateLimitWindow;
    };

    HttpRequest parseHttpRequest(const QByteArray &data);
    void handleClient(QTcpSocket *socket);
    void processHttpRequest(QTcpSocket *socket, const HttpRequest &req);

    // HTTP responses
    void sendJsonResponse(QTcpSocket *socket, int statusCode,
                          const QJsonObject &body,
                          const QString &sessionId = QString());
    void sendErrorResponse(QTcpSocket *socket, int httpStatus,
                           const QString &message);
    void sendSseEvent(QTcpSocket *socket, const QJsonObject &data);

    // JSON-RPC 2.0
    QJsonObject handleJsonRpc(const QJsonObject &request, Session &session);
    QJsonObject makeJsonRpcError(const QJsonValue &id, int code, const QString &message);
    QJsonObject makeJsonRpcResult(const QJsonValue &id, const QJsonObject &result);

    // MCP method handlers
    QJsonObject handleInitialize(const QJsonObject &params, Session &session);
    QJsonObject handleToolsList(const QJsonObject &params);
    QJsonObject handleToolsCall(const QJsonObject &params, Session &session);
    QJsonObject handleResourcesList(const QJsonObject &params);
    QJsonObject handleResourcesRead(const QJsonObject &params);

    // Security
    bool validateOrigin(const HttpRequest &req);
    bool validateAuth(const HttpRequest &req);
    bool checkRateLimit(Session &session);

    // Tool schema conversion (OpenAI -> MCP format)
    QJsonArray convertToolSchemas();

    // SSE notification broadcasting
    void broadcastToolsChanged();

    // Session management
    QString createSession();
    Session *findSession(const QString &id);
    void removeSession(const QString &id);
    void cleanupStaleSessions();

    QTcpServer *_server = nullptr;
    MidiFile *_file = nullptr;
    MidiPilotWidget *_widget = nullptr;
    QString _authToken;
    quint16 _port = 0;

    QMap<QString, Session> _sessions;
    QMap<QTcpSocket *, QByteArray> _pendingData;

    QTimer _cleanupTimer;
    mutable QMutex _sessionMutex;

    static const int MAX_RATE_PER_MINUTE = 100;
    static const int SESSION_TIMEOUT_SECS = 3600;  // 1 hour
};

#endif // MCPSERVER_H
