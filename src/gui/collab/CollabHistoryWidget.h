/*
 * MidiEditor AI
 *
 * Read-only commit log for the currently active MIDI file.
 *
 * Renders the contents of the sidecar history file (managed by
 * CollabService / CollabHistoryFile) as a chronological list of commits,
 * newest first. Live-updates on every CollabService::currentFileStateChanged
 * emission — no polling, no file watcher.
 *
 * Visibility is managed by the parent (MainWindow) — this widget just
 * shows whatever data is in the service at any given moment, and shows a
 * neutral empty state when there is nothing to show.
 */

#ifndef COLLABHISTORYWIDGET_H
#define COLLABHISTORYWIDGET_H

#include <QWidget>

class MidiFile;
class QCheckBox;
class QLabel;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

class CollabHistoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit CollabHistoryWidget(QWidget *parent = nullptr);

public slots:
    /**
     * \brief Track the currently active MidiFile.
     *
     * Connected to CollabService::activeFileChanged. Used for the
     * hunk-click handler to look up matching MidiEvent pointers.
     */
    void setFile(MidiFile *file);

signals:
    /**
     * \brief Emitted after the widget changed the global Selection in
     *        response to a hunk-row click. MainWindow connects this to
     *        a piano-roll repaint slot — keeps the widget free of any
     *        direct MatrixWidget dependency.
     */
    void selectionApplied();

private slots:
    void refresh();
    void onItemActivated(QTreeWidgetItem *item, int column);
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    QStackedWidget *_stack;
    QTreeWidget *_tree;
    QLabel *_emptyLabel;
    QCheckBox *_filterMineOnly;

    MidiFile *_file = nullptr;
};

#endif // COLLABHISTORYWIDGET_H
