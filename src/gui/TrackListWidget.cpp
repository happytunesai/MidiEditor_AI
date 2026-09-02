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

#include "TrackListWidget.h"
#include "ColoredWidget.h"
#include "Appearance.h"
#include "MainWindow.h"

#include "../midi/MidiFile.h"
#include "../midi/MidiTrack.h"
#include "../protocol/Protocol.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QToolBar>
#include <QWidget>

#define ROW_HEIGHT 85

TrackListItem::TrackListItem(MidiTrack *track, TrackListWidget *parent)
    : QWidget(parent) {
    trackList = parent;
    this->track = track;

    setContentsMargins(0, 0, 0, 0);
    QGridLayout *layout = new QGridLayout(this);
    setLayout(layout);
    layout->setVerticalSpacing(1);

    colored = new ColoredWidget(*(track->color()), this);
    layout->addWidget(colored, 0, 0, 2, 1);
    QString text = tr("Track ") + QString::number(track->number());
    QLabel *text1 = new QLabel(text, this);
    text1->setFixedHeight(15);
    layout->addWidget(text1, 0, 1, 1, 1);

    trackNameLabel = new QLabel(tr("New Track"), this);
    trackNameLabel->setFixedHeight(15);
    layout->addWidget(trackNameLabel, 1, 1, 1, 1);

    QToolBar *toolBar = new QToolBar(this);
    _toolBar = toolBar;
    toolBar->setIconSize(QSize(12, 12));
    QPalette palette = toolBar->palette();
    palette.setColor(QPalette::Window, Appearance::toolbarBackgroundColor());
    toolBar->setPalette(palette);
    // visibility
    visibleAction = new QAction(tr("Track visible"), toolBar);
    Appearance::setActionIcon(visibleAction, ":/run_environment/graphics/trackwidget/visible.png");
    visibleAction->setCheckable(true);
    visibleAction->setChecked(true);
    toolBar->addAction(visibleAction);
    connect(visibleAction, SIGNAL(toggled(bool)), this, SLOT(toggleVisibility(bool)));

    // audibility
    loudAction = new QAction(tr("Track audible"), toolBar);
    Appearance::setActionIcon(loudAction, ":/run_environment/graphics/trackwidget/loud.png");
    loudAction->setCheckable(true);
    loudAction->setChecked(true);
    toolBar->addAction(loudAction);
    connect(loudAction, SIGNAL(toggled(bool)), this, SLOT(toggleAudibility(bool)));

    toolBar->addSeparator();

    // name
    QAction *renameAction = new QAction(tr("Rename track"), toolBar);
    Appearance::setActionIcon(renameAction, ":/run_environment/graphics/trackwidget/rename.png");
    toolBar->addAction(renameAction);
    connect(renameAction, SIGNAL(triggered()), this, SLOT(renameTrack()));

    // remove
    QAction *removeAction = new QAction(tr("Remove track"), toolBar);
    Appearance::setActionIcon(removeAction, ":/run_environment/graphics/trackwidget/remove.png");
    toolBar->addAction(removeAction);
    connect(removeAction, SIGNAL(triggered()), this, SLOT(removeTrack()));

    layout->addWidget(toolBar, 2, 1, 1, 1);

    layout->setRowStretch(2, 1);
    setContentsMargins(5, 1, 5, 0);
    setFixedHeight(ROW_HEIGHT);
}

void TrackListItem::toggleVisibility(bool visible) {
    // FOCUS-DEADEYE-001: the eye mirrors the EFFECTIVE visibility, so it also
    // reads "hidden" for a track that only the playability workbench's focus
    // overlay dims. Showing such a track again is pure VIEW state - _hidden is
    // already false - so no protocol action is opened: an action with nothing
    // in it would still wipe the redo stack and mark the document modified,
    // which is exactly what made this control feel dead. Every other case
    // (hiding, or showing a track the user really hid) keeps the undoable path,
    // where setHidden() ends the overlay for that track as well.
    if (visible && !track->hiddenByUser()) {
        track->setFocusHidden(false);
        // Up the parent chain rather than window(): the panel is a tab page
        // today, but a floating dock's window() would be the dock, not us.
        MainWindow *mw = nullptr;
        for (QObject *o = trackList->parent(); o && !mw; o = o->parent()) {
            mw = qobject_cast<MainWindow *>(o);
        }
        if (mw) {
            mw->updateAll(); // repaints piano roll + this panel, no undo step
        } else {
            trackList->update();
        }
        return;
    }
    QString text = tr("Hide track");
    if (visible) {
        text = tr("Show track");
    }
    trackList->midiFile()->protocol()->startNewAction(text);
    track->setHidden(!visible); // also ends the focus overlay for this track
    trackList->midiFile()->protocol()->endAction();
}

void TrackListItem::toggleAudibility(bool audible) {
    QString text = tr("Mute track");
    if (audible) {
        text = tr("Track audible");
    }
    trackList->midiFile()->protocol()->startNewAction(text);
    track->setMuted(!audible);
    trackList->midiFile()->protocol()->endAction();
}

void TrackListItem::renameTrack() {
    emit trackRenameClicked(track->number());
}

void TrackListItem::removeTrack() {
    emit trackRemoveClicked(track->number());
}

void TrackListItem::onBeforeUpdate() {
    trackNameLabel->setText(track->name());

    // FOCUS-DEADEYE-001: the eye shows what the user SEES - hidden(), the
    // workbench's focus overlay included - so a dimmed track reads as hidden
    // and ONE click brings it back. The click itself is what must be smart, not
    // the display: see toggleVisibility(), which skips the protocol action when
    // there is no document state to change.
    if (visibleAction->isChecked() == track->hidden()) {
        disconnect(visibleAction, SIGNAL(toggled(bool)), this, SLOT(toggleVisibility(bool)));
        visibleAction->setChecked(!track->hidden());
        connect(visibleAction, SIGNAL(toggled(bool)), this, SLOT(toggleVisibility(bool)));
    }

    if (loudAction->isChecked() == track->muted()) {
        disconnect(loudAction, SIGNAL(toggled(bool)), this, SLOT(toggleAudibility(bool)));
        loudAction->setChecked(!track->muted());
        connect(loudAction, SIGNAL(toggled(bool)), this, SLOT(toggleAudibility(bool)));
    }
    colored->setColor(*(track->color()));
}

TrackListWidget::TrackListWidget(QWidget *parent)
    : QListWidget(parent) {
    setSelectionMode(QAbstractItemView::SingleSelection);
    setStyleSheet(Appearance::listBorderStyle());
    file = 0;
    connect(this, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(chooseTrack(QListWidgetItem*)));
    
    // Enable drag-and-drop for track reordering
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
}

void TrackListWidget::setFile(MidiFile *f) {
    // Editor groups (Phase 28): this dock is shared across documents and is
    // re-bound on every pane/tab switch. Drop the previous file's protocol
    // connection and use UniqueConnection so we don't stack a fresh
    // actionFinished->update() on each switch (which would leak connections and
    // refresh the dock in response to the OTHER document's edits). Mirrors the
    // disconnect+UniqueConnection treatment in MatrixWidget::setFile.
    if (file && file != f && file->protocol()) {
        disconnect(file->protocol(), SIGNAL(actionFinished()), this, SLOT(update()));
    }
    file = f;
    if (file && file->protocol()) {
        connect(file->protocol(), SIGNAL(actionFinished()), this, SLOT(update()),
                Qt::UniqueConnection);
    }
    update();
}

void TrackListWidget::chooseTrack(QListWidgetItem *item) {
    // Use row index to get track from trackorder, not Qt::UserRole
    int row = this->row(item);
    if (row >= 0 && row < trackorder.size()) {
        MidiTrack *track = trackorder.at(row);
        emit trackClicked(track);
    }
}

void TrackListWidget::update() {
    if (!file) {
        clear();
        items.clear();
        trackorder.clear();
        QListWidget::update();
        return;
    }

    bool rebuild = false;
    QList<MidiTrack *> oldTracks = trackorder;
    QList<MidiTrack *> realTracks = *file->tracks();

    if (oldTracks.size() != realTracks.size()) {
        rebuild = true;
    } else {
        for (int i = 0; i < oldTracks.size(); i++) {
            if (oldTracks.at(i) != realTracks.at(i)) {
                rebuild = true;
                break;
            }
        }
    }

    if (rebuild) {
        clear();
        items.clear();
        trackorder.clear();

        foreach(MidiTrack* track, realTracks) {
            TrackListItem *widget = new TrackListItem(track, this);
            QListWidgetItem *item = new QListWidgetItem();
            item->setSizeHint(QSize(0, ROW_HEIGHT));
            item->setData(Qt::UserRole, track->number());
            addItem(item);
            setItemWidget(item, widget);
            items.insert(track, widget);
            trackorder.append(track);
            connect(widget, SIGNAL(trackRenameClicked(int)), this, SIGNAL(trackRenameClicked(int)));
            connect(widget, SIGNAL(trackRemoveClicked(int)), this, SIGNAL(trackRemoveClicked(int)));
        }
    }

    foreach(TrackListItem* item, items.values()) {
        item->onBeforeUpdate();
    }

    QListWidget::update();
}

MidiFile *TrackListWidget::midiFile() {
    return file;
}

void TrackListWidget::dropEvent(QDropEvent *event) {
    if (event->source() == this && (event->dropAction() == Qt::MoveAction ||
                                    dragDropMode() == QAbstractItemView::InternalMove)) {
        
        // Get selected items
        QList<QListWidgetItem*> selected = selectedItems();
        if (selected.isEmpty()) {
            event->ignore();
            return;
        }
        
        // Get source position BEFORE Qt processes the drop
        int from = row(selected.first());
        
        // Get the item at the drop position
        QListWidgetItem *dropItem = itemAt(event->position().toPoint());
        if (!dropItem) {
            event->ignore();
            return;
        }
        
        int to = row(dropItem);
        
        // from/to == 0 is refused by reorderTracks() (the tempo/meta track
        // stays in slot 0) - bail out here too so the drop does not move the
        // selection to a row that never changed.
        if (from == to || from <= 0 || to <= 0) {
            event->ignore();
            return;
        }
        
        // IMPORTANT: Block Qt's default move behavior
        event->setDropAction(Qt::IgnoreAction);
        
        // Perform our custom reordering
        reorderTracks(from, to);
        
        // Update the selection to the new position
        setCurrentRow(to);
        
        // Accept but with IgnoreAction to prevent Qt from moving items
        event->accept();
    } else {
        QListWidget::dropEvent(event);
    }
}

bool TrackListWidget::dropMimeData(int index, const QMimeData *data, Qt::DropAction action) {
    Q_UNUSED(index)
    Q_UNUSED(data)
    Q_UNUSED(action)
    // We handle drops in dropEvent, so just return true to indicate we can handle it
    return true;
}

void TrackListWidget::reorderTracks(int fromIndex, int toIndex) {
    if (!file || fromIndex == toIndex || fromIndex < 0 || toIndex < 0 ||
        fromIndex >= trackorder.size() || toIndex >= trackorder.size()) {
        return;
    }

    // Slot 0 is the conventional tempo/meta track and the destructive context
    // menu ops guard BY POSITION (number() == 0), so it has to stay in slot 0 -
    // the same rule Move Up/Down already enforces. A drag must not be the one
    // path that can push another track into slot 0.
    if (fromIndex == 0 || toIndex == 0) {
        return;
    }

    // Get the track being moved
    MidiTrack *track = trackorder.at(fromIndex);
    if (!track) {
        return;
    }

    // Start protocol action for undo/redo support
    file->protocol()->startNewAction(tr("Reorder tracks"));

    // Move through the protocolled primitive instead of rewriting _tracks by
    // hand: MidiFile::moveTrack snapshots the FILE, so undo restores the list
    // ORDER and not just the numbers. Renumbering alone (what this did before)
    // left number() and position permanently out of sync after Ctrl+Z, which
    // sends every positional track(int) lookup - rename/remove, lyrics, AI
    // writes, save order - to the wrong track.
    const int delta = (toIndex > fromIndex) ? 1 : -1;
    for (int i = fromIndex; i != toIndex; i += delta) {
        if (!file->moveTrack(track, delta)) {
            break;
        }
    }

    // End protocol action
    file->protocol()->endAction();

    // Force rebuild by clearing trackorder so update() detects a change
    trackorder.clear();
    update();

    // The order-dependent UI (edit-track combo, move/copy/select-by-track
    // menus) has no receiver for trackOrderChanged() and setNumber() emits no
    // trackChanged(), so it would keep the pre-drag order until something else
    // rebuilt it. Walk the parent chain rather than window(): a floating
    // dock's window() would be the dock, not the main window.
    MainWindow *mw = nullptr;
    for (QObject *o = parent(); o && !mw; o = o->parent()) {
        mw = qobject_cast<MainWindow *>(o);
    }
    if (mw) {
        mw->updateTrackMenu();
    }

    // Emit signal to notify other components
    emit trackOrderChanged();
}

void TrackListItem::refreshColors() {
    if (_toolBar) {
        QPalette palette = _toolBar->palette();
        palette.setColor(QPalette::Window, Appearance::toolbarBackgroundColor());
        _toolBar->setPalette(palette);
    }
}

void TrackListWidget::refreshColors() {
    foreach(TrackListItem* item, items.values()) {
        item->refreshColors();
    }
    QListWidget::update();
}

void TrackListWidget::contextMenuEvent(QContextMenuEvent *event) {
    if (!file) {
        QListWidget::contextMenuEvent(event);
        return;
    }
    QListWidgetItem *item = itemAt(event->pos());
    if (!item) {
        QListWidget::contextMenuEvent(event);
        return;
    }
    const int row = this->row(item);
    if (row < 0 || row >= trackorder.size()) {
        QListWidget::contextMenuEvent(event);
        return;
    }
    MidiTrack *track = trackorder.at(row);
    if (!track) {
        QListWidget::contextMenuEvent(event);
        return;
    }
    MainWindow *mw = qobject_cast<MainWindow *>(window());
    if (!mw) {
        QListWidget::contextMenuEvent(event);
        return;
    }
    QMenu menu(this);
    const int trackNumber = track->number();
    // Track 0 is the conventional tempo/meta track - guard the destructive ops
    // so its tempo / time-signature data can't be merged away or wiped.
    const bool isTempoTrack = (trackNumber == 0);
    const int trackCount = file->tracks() ? file->tracks()->size() : 0;

    // --- structural ---
    QAction *cloneAct = menu.addAction(tr("Clone Track"));
    connect(cloneAct, &QAction::triggered, mw, [mw, track]() { mw->cloneTrack(track); });

    QMenu *mergeMenu = menu.addMenu(tr("Merge Track Into"));
    if (file->tracks()) {
        for (MidiTrack *other : *file->tracks()) {
            if (!other || other == track) continue;
            QAction *a = mergeMenu->addAction(tr("Track %1: %2").arg(other->number()).arg(other->name()));
            connect(a, &QAction::triggered, mw, [mw, track, other]() { mw->mergeTrack(track, other); });
        }
    }
    mergeMenu->setEnabled(!isTempoTrack && !mergeMenu->actions().isEmpty());

    QMenu *moveMenu = menu.addMenu(tr("Move Track"));
    QAction *upAct = moveMenu->addAction(tr("Up"));
    // Track 1 can't move up and track 0 can't move down: the tempo/meta
    // track stays in slot 0 (the number()==0 guards protect BY POSITION).
    upAct->setEnabled(trackNumber > 1);
    connect(upAct, &QAction::triggered, mw, [mw, track]() { mw->moveTrackUp(track); });
    QAction *downAct = moveMenu->addAction(tr("Down"));
    downAct->setEnabled(trackNumber > 0 && trackNumber < trackCount - 1);
    connect(downAct, &QAction::triggered, mw, [mw, track]() { mw->moveTrackDown(track); });

    menu.addSeparator();

    // --- transforms ---
    QAction *quantizeAct = menu.addAction(tr("Quantize Track"));
    connect(quantizeAct, &QAction::triggered, mw, [mw, track]() { mw->quantizeTrack(track); });

    QMenu *transposeMenu = menu.addMenu(tr("Transpose Track"));
    QAction *transAct = transposeMenu->addAction(tr("Transpose..."));
    connect(transAct, &QAction::triggered, mw, [mw, track]() { mw->transposeTrack(track); });
    QAction *octUpAct = transposeMenu->addAction(tr("Octave Up"));
    connect(octUpAct, &QAction::triggered, mw, [mw, track]() { mw->transposeTrackOctaveUp(track); });
    QAction *octDownAct = transposeMenu->addAction(tr("Octave Down"));
    connect(octDownAct, &QAction::triggered, mw, [mw, track]() { mw->transposeTrackOctaveDown(track); });

    QAction *explodeAct = menu.addAction(tr("Explode Chords to Tracks"));
    connect(explodeAct, &QAction::triggered, mw, [mw, track]() { mw->explodeChordsToTracks(track); });
    QAction *splitAct = menu.addAction(tr("Split Channels to Tracks"));
    connect(splitAct, &QAction::triggered, mw, [mw, track]() { mw->splitChannelsToTracks(track); });

    menu.addSeparator();

    // --- selection / events ---
    QAction *selectAct = menu.addAction(tr("Select All Events"));
    connect(selectAct, &QAction::triggered, mw, [mw, track]() { mw->selectTrackEvents(track); });

    QMenu *moveChanMenu = menu.addMenu(tr("Move Events to Channel"));
    moveChanMenu->setEnabled(!isTempoTrack);
    for (int ch = 0; ch < 16; ++ch) {
        // 0-based to match the Channels panel and the Tools menu.
        QString label = (ch == 9) ? tr("Channel %1 (Drums)").arg(ch)
                                   : tr("Channel %1").arg(ch);
        QAction *a = moveChanMenu->addAction(label);
        connect(a, &QAction::triggered, mw, [mw, track, ch]() { mw->moveTrackEventsToChannel(track, ch); });
    }

    QAction *removeAct = menu.addAction(tr("Remove Events"));
    removeAct->setEnabled(!isTempoTrack);
    connect(removeAct, &QAction::triggered, mw, [mw, track]() { mw->clearTrackEvents(track); });

    menu.addSeparator();

    // --- existing tempo tool ---
    QAction *convertTempoAct = menu.addAction(tr("Convert Tempo, Preserve Duration..."));
    connect(convertTempoAct, &QAction::triggered, mw, [mw, trackNumber]() {
        mw->convertTempoForTrack(trackNumber);
    });

    menu.exec(event->globalPos());
}
