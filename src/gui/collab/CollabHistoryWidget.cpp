/*
 * MidiEditor AI - CollabHistoryWidget implementation.
 */

#include "CollabHistoryWidget.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "../../MidiEvent/MidiEvent.h"
#include "../../collab/CollabIdentity.h"
#include "../../collab/CollabService.h"
#include "../../collab/PrApply.h"
#include "../../midi/MidiFile.h"
#include "../../tool/Selection.h"

namespace {

QString formatRelative(qint64 epochSeconds) {
    if (epochSeconds <= 0) return CollabHistoryWidget::tr("just now");
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 delta = now - epochSeconds;
    if (delta < 0) delta = 0;
    if (delta < 60) return CollabHistoryWidget::tr("just now");
    if (delta < 3600) return CollabHistoryWidget::tr("%1 min ago").arg(delta / 60);
    if (delta < 86400) return CollabHistoryWidget::tr("%1 h ago").arg(delta / 3600);
    if (delta < 7 * 86400) return CollabHistoryWidget::tr("%1 d ago").arg(delta / 86400);
    return QDateTime::fromSecsSinceEpoch(epochSeconds).toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd"));
}

QString formatAbsolute(qint64 epochSeconds) {
    if (epochSeconds <= 0) return QString();
    return QDateTime::fromSecsSinceEpoch(epochSeconds).toLocalTime()
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

}

CollabHistoryWidget::CollabHistoryWidget(QWidget *parent)
    : QWidget(parent) {

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    _filterMineOnly = new QCheckBox(tr("Show only my commits"), this);
    _filterMineOnly->setToolTip(tr("Hide commits from other authors. Useful in busy sessions "
                                   "to focus on your own contributions."));
    _filterMineOnly->setStyleSheet("padding: 4px 8px;");
    connect(_filterMineOnly, &QCheckBox::toggled, this, &CollabHistoryWidget::refresh);
    layout->addWidget(_filterMineOnly);

    _stack = new QStackedWidget(this);
    layout->addWidget(_stack);

    // Tree view with the commit list.
    _tree = new QTreeWidget(_stack);
    // Show the disclosure chevron only on rows that actually have children
    // (= commits with non-empty hunks). The initial commit and old entries
    // imported without hunk data render flat.
    _tree->setRootIsDecorated(true);
    _tree->setUniformRowHeights(false);
    _tree->setAlternatingRowColors(true);
    _tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    _tree->setSelectionMode(QAbstractItemView::SingleSelection);
    _tree->setHeaderLabels({tr("Author"), tr("When"), tr("Message"), tr("Hash")});
    _tree->header()->setStretchLastSection(false);
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    _tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(_tree, &QTreeWidget::itemActivated, this, &CollabHistoryWidget::onItemActivated);
    connect(_tree, &QTreeWidget::itemClicked, this, &CollabHistoryWidget::onItemClicked);
    _stack->addWidget(_tree);

    // Empty / not-initialized state.
    _emptyLabel = new QLabel(_stack);
    _emptyLabel->setAlignment(Qt::AlignCenter);
    _emptyLabel->setWordWrap(true);
    _emptyLabel->setMargin(20);
    _emptyLabel->setStyleSheet("color: gray;");
    _stack->addWidget(_emptyLabel);

    connect(CollabService::instance(), &CollabService::currentFileStateChanged,
            this, &CollabHistoryWidget::refresh);
    connect(CollabService::instance(), &CollabService::enabledChanged,
            this, [this](bool) { refresh(); });
    connect(CollabService::instance(), &CollabService::activeFileChanged,
            this, &CollabHistoryWidget::setFile);

    refresh();
}

void CollabHistoryWidget::setFile(MidiFile *file) {
    _file = file;
}

void CollabHistoryWidget::refresh() {
    CollabService *svc = CollabService::instance();

    if (!svc->isEnabled()) {
        _emptyLabel->setText(tr("Collaboration features are disabled.\n"
                                "Enable them in Settings → Collaboration."));
        _stack->setCurrentWidget(_emptyLabel);
        return;
    }

    if (!svc->hasCurrentFile()) {
        _emptyLabel->setText(tr("No file loaded."));
        _stack->setCurrentWidget(_emptyLabel);
        return;
    }

    if (!svc->isCurrentFileInitialized()) {
        _emptyLabel->setText(tr("This file is not initialized for collaboration yet.\n\n"
                                "Use File → Collaboration → Initialize for this file…\n"
                                "to start tracking changes here."));
        _stack->setCurrentWidget(_emptyLabel);
        return;
    }

    QJsonArray hist = svc->history();
    _tree->clear();
    if (hist.isEmpty()) {
        _emptyLabel->setText(tr("No commits yet."));
        _stack->setCurrentWidget(_emptyLabel);
        return;
    }

    // Filter to local user's commits when the toggle is on. Compare by
    // displayName — same string the local CollabIdentity uses when
    // attributing new commits.
    bool mineOnly = _filterMineOnly && _filterMineOnly->isChecked();
    QString myName = mineOnly ? CollabIdentity::displayName() : QString();
    int renderedCount = 0;

    // Render newest first.
    for (int i = hist.size() - 1; i >= 0; --i) {
        QJsonObject e = hist.at(i).toObject();
        if (mineOnly && e.value(QStringLiteral("author")).toString() != myName) {
            continue;
        }
        ++renderedCount;
        QString author = e.value(QStringLiteral("author")).toString();
        QString machineId = e.value(QStringLiteral("machineId")).toString();
        qint64 ts = static_cast<qint64>(e.value(QStringLiteral("ts")).toDouble());
        QString message = e.value(QStringLiteral("message")).toString();
        QString hash = e.value(QStringLiteral("hash")).toString();
        QString hashShort = hash.left(8);

        QTreeWidgetItem *item = new QTreeWidgetItem(_tree);
        item->setText(0, author);
        if (!machineId.isEmpty()) {
            item->setToolTip(0, tr("Machine ID: %1").arg(machineId));
        }
        item->setText(1, formatRelative(ts));
        item->setToolTip(1, formatAbsolute(ts));
        item->setText(2, message);
        item->setToolTip(2, message);
        item->setText(3, hashShort);
        item->setToolTip(3, tr("Click to copy full hash:\n%1").arg(hash));
        item->setData(3, Qt::UserRole, hash);
        item->setData(0, Qt::UserRole + 1, ts);  // for future sort

        // Per-hunk summary children (Phase 9.1c₃). Renders one indented
        // row per hunk with scope label + add/remove/modify counts.
        // Spans all four columns so the row reads as a single line.
        QJsonArray hunks = e.value(QStringLiteral("hunks")).toArray();
        for (const QJsonValue &hv : hunks) {
            QJsonObject hunk = hv.toObject();
            QJsonObject scope = hunk.value(QStringLiteral("scope")).toObject();
            int ch = scope.value(QStringLiteral("channel")).toInt();
            int trk = scope.value(QStringLiteral("track")).toInt();
            int mStart = scope.value(QStringLiteral("measureStart")).toInt();
            int mEnd = scope.value(QStringLiteral("measureEnd")).toInt();
            int addedN = hunk.value(QStringLiteral("added")).toArray().size();
            int removedN = hunk.value(QStringLiteral("removed")).toArray().size();
            int modifiedN = hunk.value(QStringLiteral("modified")).toArray().size();

            QString measureLabel = (mStart == mEnd)
                ? tr("m. %1").arg(mStart + 1)
                : tr("m. %1–%2").arg(mStart + 1).arg(mEnd + 1);

            // Use figure-space for column-like alignment of counts.
            QString summary = QStringLiteral("ch %1  trk %2  %3   +%4  ⋅%5  ~%6")
                                  .arg(ch).arg(trk).arg(measureLabel)
                                  .arg(addedN).arg(removedN).arg(modifiedN);

            QTreeWidgetItem *child = new QTreeWidgetItem(item);
            child->setFirstColumnSpanned(true);
            child->setText(0, summary);
            child->setForeground(0, QBrush(QColor(0x99, 0x99, 0x99)));
            child->setToolTip(0,
                tr("Tick range: %1–%2\nAdded: %3   Removed: %4   Modified: %5\n\nClick to select these events on the piano roll.")
                    .arg(scope.value(QStringLiteral("tickStart")).toInt())
                    .arg(scope.value(QStringLiteral("tickEnd")).toInt())
                    .arg(addedN).arg(removedN).arg(modifiedN));
            // Stash the full hunk so the click handler can resolve it back
            // to MidiEvent pointers without re-walking the history.
            child->setData(0, Qt::UserRole, QVariant::fromValue(hunk));
            // Slightly smaller font so the children read as secondary content.
            QFont f = child->font(0);
            f.setPointSizeF(f.pointSizeF() * 0.92);
            child->setFont(0, f);
        }
    }

    if (renderedCount == 0) {
        // Filter is on and nothing matched — show a friendly hint instead
        // of an empty tree.
        _emptyLabel->setText(tr("No commits by %1 in this file's history.\n\n"
                                "Uncheck \"Show only my commits\" to see contributions from other peers.")
                                  .arg(CollabIdentity::displayName()));
        _stack->setCurrentWidget(_emptyLabel);
        return;
    }
    _stack->setCurrentWidget(_tree);
}

void CollabHistoryWidget::onItemActivated(QTreeWidgetItem *item, int column) {
    if (!item) return;
    if (column == 3) {
        QString fullHash = item->data(3, Qt::UserRole).toString();
        if (!fullHash.isEmpty()) {
            QApplication::clipboard()->setText(fullHash);
        }
    }
}

void CollabHistoryWidget::onItemClicked(QTreeWidgetItem *item, int column) {
    Q_UNUSED(column);
    if (!item) return;
    if (!item->parent()) return;  // top-level commit row — leave selection alone
    if (!_file) return;

    QVariant data = item->data(0, Qt::UserRole);
    QJsonObject hunk = data.toJsonObject();
    if (hunk.isEmpty()) return;

    // Resolve `added` and `modified.after` events to live MidiEvent pointers.
    // `removed` events are intentionally skipped — by definition they no
    // longer exist in the current state.
    QList<MidiEvent *> matches;
    QJsonArray added = hunk.value(QStringLiteral("added")).toArray();
    for (const QJsonValue &v : added) {
        MidiEvent *ev = PrApply::findMatchingEvent(_file, v.toObject());
        if (ev) matches.append(ev);
    }
    QJsonArray modified = hunk.value(QStringLiteral("modified")).toArray();
    for (const QJsonValue &v : modified) {
        QJsonObject pair = v.toObject();
        MidiEvent *ev = PrApply::findMatchingEvent(_file, pair.value(QStringLiteral("after")).toObject());
        if (ev) matches.append(ev);
    }

    Selection::instance()->setSelection(matches);
    emit selectionApplied();
}
