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

#ifndef DOCUMENTMANAGER_H_
#define DOCUMENTMANAGER_H_

// Phase 28 (multi-document tabs). DocumentManager + Document are deliberately
// pure, GUI-free bookkeeping so they can be unit-tested headlessly: they track
// the set of open documents and which one is active. They do NOT own or delete
// the MidiFile (MainWindow keeps the heavy create/teardown logic that wires
// ~20 panels + globals to a file); DocumentManager only manages the list and
// the active index, handing the detached MidiFile back to the caller on close.

#include <QList>
#include <QString>

class MidiFile;

/**
 * \class Document
 * \brief A single open note-roll: its MidiFile plus per-document UI metadata.
 *
 * Per-document Selection is already keyed on the MidiFile (see Selection);
 * per-document viewport / channel-visibility state will move onto Document in
 * later Phase-28 sub-phases. For now it is a light handle around the file.
 */
class Document {
public:
    explicit Document(MidiFile *file, const QString &title = QString())
        : _file(file), _title(title) {}

    MidiFile *file() const { return _file; }
    void setFile(MidiFile *file) { _file = file; }

    QString title() const { return _title; }
    void setTitle(const QString &title) { _title = title; }

private:
    MidiFile *_file;
    QString _title;
};

/**
 * \class DocumentManager
 * \brief Owns the list of open Documents and tracks the active one.
 *
 * Ownership: the manager owns the Document* handles (deletes them in its dtor
 * and on close), but NOT the underlying MidiFile - removeAt()/closeActive()
 * detach and return the MidiFile* so the caller (MainWindow) can run the full
 * teardown (Selection::forgetFile, analyzer.forgetFile, disconnect, delete).
 */
class DocumentManager {
public:
    DocumentManager() = default;
    ~DocumentManager();

    DocumentManager(const DocumentManager &) = delete;
    DocumentManager &operator=(const DocumentManager &) = delete;

    /** Append a new document for `file` and return it. Does NOT change active. */
    Document *open(MidiFile *file, const QString &title = QString());

    /** Append a new document for `file`, make it active, and return it. */
    Document *openAndActivate(MidiFile *file, const QString &title = QString());

    /** The active document, or nullptr if none are open. */
    Document *active() const;

    /** Index of the active document, or -1 if none. */
    int activeIndex() const { return _activeIndex; }

    /** Make the document at `index` active (no-op if out of range). */
    void setActiveIndex(int index);

    /** Make `doc` active if it is managed (no-op otherwise). */
    void setActive(Document *doc);

    int count() const { return _documents.size(); }
    bool isEmpty() const { return _documents.isEmpty(); }
    Document *at(int index) const;
    int indexOf(Document *doc) const { return _documents.indexOf(doc); }
    int indexOfFile(MidiFile *file) const;
    QList<Document *> documents() const { return _documents; }

    /**
     * Detach and delete the Document at `index`, returning its MidiFile* for
     * the caller to tear down. The active index is adjusted to stay valid
     * (clamps toward the previous neighbour). Returns nullptr if out of range.
     */
    MidiFile *removeAt(int index);

    /** Convenience: removeAt(activeIndex()). */
    MidiFile *closeActive();

private:
    QList<Document *> _documents;
    int _activeIndex = -1;
};

#endif // DOCUMENTMANAGER_H_
