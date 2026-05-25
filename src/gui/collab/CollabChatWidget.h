/*
 * MidiEditor AI
 *
 * Lightweight in-session chat side-channel (Phase 9.11 §15.3).
 *
 * Sits in the lower tab bar next to the Collaboration history tab. UI is
 * a vertical split: scrollback (QTextEdit, read-only) + input row
 * (single-line QLineEdit + Send button).
 *
 * Behaviour:
 *   - Single-line input by default; Shift+Enter inserts a newline.
 *   - Optimistic append on send — the sender's own message lands in
 *     the scrollback before the network roundtrip, and the host
 *     re-broadcast deliberately excludes the original sender so they
 *     don't see it twice.
 *   - In-memory scrollback only, capped at kMaxScrollback messages
 *     (oldest roll off the top). No persistence to disk.
 *   - Time formatted in the local timezone — no clock-sync attempt.
 *
 * Not in MVP (§15.3): threads, @-mentions, reactions, attachments,
 * read receipts, search, persistent history, DMs.
 */

#ifndef COLLABCHATWIDGET_H
#define COLLABCHATWIDGET_H

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTextEdit;

class CollabChatWidget : public QWidget {
    Q_OBJECT

public:
    explicit CollabChatWidget(QWidget *parent = nullptr);

    /** \brief Hard cap on retained messages. Older messages roll off
     *  the top when this is exceeded. Per §15.3 design. */
    static constexpr int kMaxScrollback = 500;

signals:
    /** \brief Emitted when a new message lands in the scrollback —
     *  parent uses it to drive the unread-badge on the tab title. */
    void newMessageArrived();

public slots:
    /**
     * \brief Append an incoming chat message to the scrollback. Called
     *        by MainWindow when LanLiveSession::chatMessageReceived
     *        fires. The widget itself doesn't know about the live
     *        session — it's a dumb display.
     *
     *        Pass an empty senderMachineId to mark a system-style
     *        message (rendered in a muted color). The optimistic-self
     *        path uses the local CollabIdentity::machineId so styling
     *        flips to right-aligned automatically.
     */
    void appendMessage(const QString &senderMachineId,
                       const QString &displayName,
                       const QString &text,
                       qint64 timestampMs);

    /** \brief Clear the scrollback. Called when a session ends so the
     *  next session starts with an empty chat — per §15.3 "no
     *  persistence to disk". */
    void clearChat();

    /** \brief Enable/disable input + send button. Disabled outside a
     *  live session; the placeholder explains why. */
    void setInputEnabled(bool enabled);

private slots:
    void onSendClicked();
    void onInputReturnPressed();

private:
    void appendFormattedLine(const QString &htmlBlock);

    QTextEdit *_scrollback = nullptr;
    QLineEdit *_input = nullptr;
    QPushButton *_sendButton = nullptr;

    int _messageCount = 0;
};

#endif // COLLABCHATWIDGET_H
