/*
 * MidiEditor AI - CollabChatWidget implementation (Phase 9.11 §15.3).
 */

#include "CollabChatWidget.h"

#include <QDateTime>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "../../collab/CollabIdentity.h"
#include "../../collab/LanLiveSession.h"

CollabChatWidget::CollabChatWidget(QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    _scrollback = new QTextEdit(this);
    _scrollback->setReadOnly(true);
    _scrollback->setLineWrapMode(QTextEdit::WidgetWidth);
    _scrollback->setPlaceholderText(
        tr("No chat yet. Type a message to start the conversation."));
    layout->addWidget(_scrollback, 1);

    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(4);
    _input = new QLineEdit(this);
    _input->setPlaceholderText(tr("Type a message — Enter to send"));
    inputRow->addWidget(_input, 1);
    _sendButton = new QPushButton(tr("Send"), this);
    inputRow->addWidget(_sendButton);
    layout->addLayout(inputRow);

    connect(_sendButton, &QPushButton::clicked,
            this, &CollabChatWidget::onSendClicked);
    connect(_input, &QLineEdit::returnPressed,
            this, &CollabChatWidget::onInputReturnPressed);

    // Default to disabled — MainWindow turns input on/off based on
    // whether a live session is active.
    setInputEnabled(false);
}

void CollabChatWidget::appendMessage(const QString &senderMachineId,
                                     const QString &displayName,
                                     const QString &text,
                                     qint64 timestampMs) {
    // Format: [HH:mm] <displayName>: <text>
    // — local timezone, no date (chat is session-scoped per §15.3).
    QDateTime ts = QDateTime::fromMSecsSinceEpoch(timestampMs);
    QString timeStr = ts.toLocalTime().toString(QStringLiteral("HH:mm"));

    QString senderColor;
    bool isOwn = (!senderMachineId.isEmpty()
                  && senderMachineId == CollabIdentity::machineId());
    if (senderMachineId.isEmpty()) {
        senderColor = QStringLiteral("#888");          // system / muted
    } else if (isOwn) {
        senderColor = QStringLiteral("#2a7ae2");       // own — accent blue
    } else {
        senderColor = QStringLiteral("#444");          // others — neutral
    }

    // Plain-text body via toHtmlEscaped so a chat message containing
    // markup can't inject formatting into the scrollback. Newlines
    // become <br> for in-message line breaks.
    QString safeText = text.toHtmlEscaped().replace(QLatin1Char('\n'),
                                                     QStringLiteral("<br>"));
    QString safeName = displayName.toHtmlEscaped();

    QString html = QStringLiteral(
        "<div style='margin: 1px 0;'>"
        "<span style='color: #888;'>[%1]</span> "
        "<span style='color: %2; font-weight: bold;'>%3:</span> "
        "%4"
        "</div>")
        .arg(timeStr, senderColor, safeName, safeText);

    appendFormattedLine(html);
    emit newMessageArrived();
}

void CollabChatWidget::appendFormattedLine(const QString &htmlBlock) {
    // Append at the bottom and scroll into view. Cursor save/restore so
    // a user manually scrolling up to re-read isn't snapped to bottom
    // on every new message (their explicit scroll position wins).
    QTextCursor cursor = _scrollback->textCursor();
    bool atBottom = (_scrollback->verticalScrollBar()->value()
                     >= _scrollback->verticalScrollBar()->maximum() - 4);
    cursor.movePosition(QTextCursor::End);
    cursor.insertHtml(htmlBlock);
    cursor.insertHtml(QStringLiteral("<br>"));
    if (atBottom) {
        _scrollback->verticalScrollBar()->setValue(
            _scrollback->verticalScrollBar()->maximum());
    }

    // Trim the oldest entries when we exceed the cap. QTextEdit doesn't
    // expose a line count cheaply, so we track the message count
    // separately and drop the topmost block once we're over.
    _messageCount++;
    while (_messageCount > kMaxScrollback) {
        QTextCursor c = _scrollback->textCursor();
        c.movePosition(QTextCursor::Start);
        c.select(QTextCursor::BlockUnderCursor);
        c.removeSelectedText();
        c.deleteChar();   // remove the trailing newline that selection left
        _messageCount--;
    }
}

void CollabChatWidget::clearChat() {
    _scrollback->clear();
    _messageCount = 0;
}

void CollabChatWidget::setInputEnabled(bool enabled) {
    _input->setEnabled(enabled);
    _sendButton->setEnabled(enabled);
    _input->setPlaceholderText(enabled
        ? tr("Type a message — Enter to send")
        : tr("No live session. Start or join one to chat."));
}

void CollabChatWidget::onSendClicked() {
    QString text = _input->text().trimmed();
    if (text.isEmpty()) return;
    if (!LanLiveSession::instance()) return;
    if (LanLiveSession::instance()->role() == LanLiveSession::Role::Idle)
        return;

    // Optimistic append — the host's broadcastExcept skips us, and the
    // pre-9.11 wire never echoed local frames back anyway. So appending
    // here is the only thing that puts the message in the local view.
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    appendMessage(CollabIdentity::machineId(),
                  CollabIdentity::displayName(),
                  text,
                  nowMs);

    LanLiveSession::instance()->sendChatMessage(text);
    _input->clear();
}

void CollabChatWidget::onInputReturnPressed() {
    // QLineEdit::returnPressed fires whether Shift is held or not.
    // Multi-line input via Shift+Enter is documented in the design but
    // would require swapping to QPlainTextEdit; deferred to follow-up.
    // For now, Enter = send.
    onSendClicked();
}
