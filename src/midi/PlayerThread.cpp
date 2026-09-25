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

#include "PlayerThread.h"
#include "../MidiEvent/KeySignatureEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../MidiEvent/TimeSignatureEvent.h"
#include "MidiFile.h"
#include "MidiInput.h"
#include "MidiOutput.h"
#include "MidiPlayer.h"
#include "MidiTrack.h"
#ifdef FLUIDSYNTH_SUPPORT
#include "FluidSynthEngine.h"
#endif
#include <QMultiMap>
#include <QElapsedTimer>
#include <QMutexLocker>

#define INTERVAL_TIME 15
#define TIMEOUTS_PER_SIGNAL 1

PlayerThread::PlayerThread()
    : QThread() {
    file = 0;
    timer = 0;
    timeoutSinceLastSignal = 0;
    time = 0;
    measureEvents = 0;
}

PlayerThread::~PlayerThread() {
    delete timer;
    delete time;
    // MidiFile::measure() hands the list over to the caller (it deletes the one
    // it is given and returns a fresh one), so the last one is ours to free.
    // The TimeSignatureEvents in it belong to the file and are not touched.
    delete measureEvents;
}

void PlayerThread::start(Priority priority) {
    // Clear the stop request on the CALLER's thread, before the worker starts.
    // Doing it in run() overwrote a stop() issued during run()'s prologue, so
    // the timer loop never saw it and MidiPlayer::stop()'s wait() froze the GUI
    // until the song ended.
    stopped = false;
    QThread::start(priority);
}

void PlayerThread::setFile(MidiFile *f) {
    file = f;
    // Resolve every track's FFXIV drum program HERE, on the GUI thread, once
    // per start: MidiTrack::name() copies an unsynchronised QString that a
    // rename/undo on the GUI thread rewrites, so the playback loop must not
    // call it. Same lookup as before, only taken at start instead of per note.
    trackDrumPrograms.clear();
#ifdef FLUIDSYNTH_SUPPORT
    if (file) {
        FluidSynthEngine *engine = FluidSynthEngine::instance();
        foreach (MidiTrack *track, *file->tracks()) {
            trackDrumPrograms.insert(track, engine->drumProgramForTrackName(track->name()));
        }
    }
#endif
}

void PlayerThread::stop() {
    stopped = true;
}

void PlayerThread::setInterval(int i) {
    interval = i;
}

void PlayerThread::run() {
    if (!timer) {
        timer = new QTimer();
    }
    if (time) {
        delete time;
        time = 0;
    }

    events = file->playerData();

    // Everything this thread asks MidiFile for from here on is limited to the
    // two conversions the file documents as thread-safe (msOfTick(tick) and
    // tick(ms)). Both answer from an immutable tempo snapshot the DOCUMENT
    // thread publishes, so nothing below ever iterates a channel map or
    // rebuilds the cache - which is what makes editing during playback safe.
    // The price is that a conversion made between an edit and the GUI's next
    // timing query is one edit stale; a frame of stale tempo is inaudible and
    // self-corrects. See MidiFile's "Thread safety" note.
    if (file->pauseTick() >= 0) {
        position = file->msOfTick(file->pauseTick());
    } else {
        position = file->msOfTick(file->cursorTick());
    }

    emit playerStarted();

    // Reset all Controllers
    for (int i = 0; i < 16; i++) {
        QByteArray array;
        array.append(0xB0 | i);
        array.append(121);
        array.append(char(0));
        MidiOutput::sendCommand(array);
    }
    MidiOutput::playedNotesMutex.lock();
    MidiOutput::playedNotes.clear();
    MidiOutput::playedNotesMutex.unlock();

    // All Events before position should be sent, progChanges and ControlChanges
    QMultiMap<int, MidiEvent *>::iterator it = events->begin();
    while (it != events->end()) {
        if (it.key() >= position) {
            break;
        }
        // Pre-resolved drum program: no track-name read on this thread.
        MidiOutput::sendCommand(it.value(), trackDrumPrograms.value(it.value()->track(), -1));
        it++;
    }

    setInterval(INTERVAL_TIME);

    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()), Qt::DirectConnection);
    timer->start(INTERVAL_TIME);

    // measureEvents is a member and is passed back in on every call: measure()
    // deletes the list it is given before allocating the next one, so reusing
    // the same pointer is what keeps this from leaking a QList per 15 ms tick.
    int tickInMeasure = 0;
    measure = file->measure(file->cursorTick(), file->cursorTick(), &measureEvents, &tickInMeasure);
    emit(measureChanged(measure, tickInMeasure));

    if (exec() == 0) {
        timer->stop();
        emit playerStopped();
    }
}

void PlayerThread::timeout() {
    if (!time) {
        time = new QElapsedTimer();
        time->start();
    }

    disconnect(timer, SIGNAL(timeout()), this, SLOT(timeout()));
    if (stopped) {
        disconnect(timer, SIGNAL(timeout()), this, SLOT(timeout()));

        // AllNotesOff // All SoundsOff
        for (int i = 0; i < 16; i++) {
            // value (third number) should be 0, but doesnt work
            QByteArray array;
            array.append(0xB0 | i);
            array.append(char(123));
            array.append(char(127));
            MidiOutput::sendCommand(array);
        }
        if (MidiOutput::isAlternativePlayer) {
            QMutexLocker locker(&MidiOutput::playedNotesMutex);
            foreach(int channel, MidiOutput::playedNotes.keys()) {
                foreach(int note, MidiOutput::playedNotes.value(channel)) {
                    QByteArray array;
                    array.append(0x80 | channel);
                    array.append(char(note));
                    array.append(char(0));
                    MidiOutput::sendCommand(array);
                }
            }
        }
        quit();
    } else {
        int newPos = position + time->elapsed() * MidiPlayer::speedScale();
        // Snapshot-backed and safe to call from here (see run()).
        int tick = file->tick(newPos);
        int ickInMeasure = 0;

        // NOT covered by that guarantee: measure() walks channel 18's live
        // QMultiMap on this thread. It predates the tempo cache and is a known
        // pre-existing hazard (a meter edit during playback can race it), not
        // something the tempo snapshot fixes. Do not add more calls like it.
        int new_measure = file->measure(tick, tick, &measureEvents, &ickInMeasure);

        // compute current pos

        if (new_measure > measure) {
            emit measureChanged(new_measure, ickInMeasure);
            measure = new_measure;
        }
        time->restart();
        QMultiMap<int, MidiEvent *>::iterator it = events->lowerBound(position);

        while (it != events->end() && it.key() < newPos) {
            // save events for the given tick
            QList<MidiEvent *> onEv, offEv;
            int sendPosition = it.key();

            do {
                if (it.value()->isOnEvent()) {
                    onEv.append(it.value());
                } else {
                    offEv.append(it.value());
                }
                it++;
            } while (it != events->end() && it.key() == sendPosition);

            // A program change reaches the synth before the notes of its
            // channel that start at the same tick - the same order save()
            // writes (fixer review CF-08).
            MidiFile::programChangesBeforeNotes(onEv);

            foreach(MidiEvent* ev, offEv) {
                MidiOutput::sendCommand(ev, trackDrumPrograms.value(ev->track(), -1));
            }
            foreach(MidiEvent* ev, onEv) {
                if (ev->line() == MidiEvent::KEY_SIGNATURE_EVENT_LINE) {
                    KeySignatureEvent *keySig = dynamic_cast<KeySignatureEvent *>(ev);
                    if (keySig) {
                        emit tonalityChanged(keySig->tonality());
                    }
                }
                if (ev->line() == MidiEvent::TIME_SIGNATURE_EVENT_LINE) {
                    TimeSignatureEvent *timeSig = dynamic_cast<TimeSignatureEvent *>(ev);
                    if (timeSig) {
                        emit meterChanged(timeSig->num(), timeSig->denom());
                    }
                }
                // Pre-resolved drum program: no track-name read on this thread.
                MidiOutput::sendCommand(ev, trackDrumPrograms.value(ev->track(), -1));
            }

            //MidiOutput::sendCommand(it.value());
            //it++;
        }

        // end if it was last event, but only if not recording
        if (it == events->end() && !MidiInput::recording()) {
            stop();
        }
        position = newPos;
        timeoutSinceLastSignal++;
        MidiInput::setTime(position);
        if (timeoutSinceLastSignal == TIMEOUTS_PER_SIGNAL) {
            emit timeMsChanged(position);
            emit measureUpdate(measure, ickInMeasure);
            timeoutSinceLastSignal = 0;
        }
    }
    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()), Qt::DirectConnection);
}

int PlayerThread::timeMs() {
    return position;
}
