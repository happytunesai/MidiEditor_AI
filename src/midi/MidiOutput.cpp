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

#include "MidiOutput.h"

#include "../MidiEvent/MidiEvent.h"

#include <QByteArray>
#include <QFile>
#include <QMutexLocker>
#include <QReadWriteLock>

#include <vector>

#include <atomic>

#include "rtmidi/RtMidi.h"

#ifdef FLUIDSYNTH_SUPPORT
#include "FluidSynthEngine.h"
#endif

using namespace rt::midi;

#include "../MidiEvent/NoteOnEvent.h"
#include "../MidiEvent/SysExEvent.h"
#include "../MidiEvent/OffEvent.h"
#include "../midi/MidiTrack.h"

RtMidiOut *MidiOutput::_midiOut = 0;
QString MidiOutput::_outPort = "";
QMap<int, QList<int> > MidiOutput::playedNotes = QMap<int, QList<int> >();
QMutex MidiOutput::playedNotesMutex;
bool MidiOutput::isAlternativePlayer = false;
QReadWriteLock MidiOutput::_outputLock;
std::atomic<int> MidiOutput::channelActivity[16] = {};

void MidiOutput::resetChannelActivity() {
    for (int i = 0; i < 16; i++) {
        channelActivity[i].store(0, std::memory_order_relaxed);
    }
}

#ifdef FLUIDSYNTH_SUPPORT
const QString MidiOutput::FLUIDSYNTH_PORT_NAME = QStringLiteral("FluidSynth (Built-in Synthesizer)");
#endif

int MidiOutput::_stdChannel = 0;

void MidiOutput::init() {
    // RtMidiOut constructor
    try {
        _midiOut = new RtMidiOut(RtMidi::UNSPECIFIED, QString(tr("MidiEditor output")).toStdString());
    } catch (RtMidiError &error) {
        error.printMessage();
    }
    // PERFORMANCE: No SenderThread - send MIDI directly to eliminate NtWaitForSingleObject overhead
}

void MidiOutput::sendCommand(QByteArray array) {
    sendEnqueuedCommand(array);
}

void MidiOutput::sendCommand(MidiEvent *e) {
    int trackDrumProgram = -1;
#ifdef FLUIDSYNTH_SUPPORT
    // Document-thread callers (note preview) may read the live track name.
    // The player thread must NOT: MidiTrack::name() copies an unsynchronised
    // QString that a GUI rename/undo rewrites - it uses the two-arg overload
    // with the snapshot PlayerThread took on the GUI thread at start.
    // Gated on FFXIV mode: drumProgramForTrackName() returns -1 when off.
    if (isFluidSynthOutput() && e->channel() == 9 && e->track() &&
        FluidSynthEngine::instance()->ffxivSoundFontMode()) {
        trackDrumProgram = FluidSynthEngine::instance()->drumProgramForTrackName(
            e->track()->name());
    }
#endif
    sendCommand(e, trackDrumProgram);
}

void MidiOutput::sendCommand(MidiEvent *e, int trackDrumProgram) {
    if (e->channel() >= 0 && e->channel() < 16 || e->line() == MidiEvent::SYSEX_LINE) {

#ifdef FLUIDSYNTH_SUPPORT
        // Per-note drum program injection for FluidSynth:
        // When FFXIV SoundFont Mode is active, all drums share CH9.
        // Before each NoteOn on CH9, send a Program Change matching the
        // track's FFXIV percussion instrument so FluidSynth uses the
        // correct SoundFont preset.  When the track name is unknown
        // (typical for GM-style drum tracks that pack kick/snare/toms/
        // cymbals on a single channel), fall back to mapping the GM
        // drum key to the closest FFXIV percussion preset.
        // The track's program arrives pre-resolved (trackDrumProgram) so this
        // path never reads the document's track name on the player thread.
        if (isFluidSynthOutput() && e->channel() == 9 &&
            FluidSynthEngine::instance()->ffxivSoundFontMode()) {
            NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(e);
            if (noteOn && noteOn->velocity() > 0) {
                int prog = trackDrumProgram;
                if (prog < 0) {
                    prog = FluidSynthEngine::ffxivDrumProgramForGmNote(
                        noteOn->note());
                }
                if (prog >= 0) {
                    QByteArray pc;
                    pc.append(static_cast<char>(0xC9)); // Program Change CH9
                    pc.append(static_cast<char>(prog));
                    sendEnqueuedCommand(pc);
                }
            }
        }
#endif

        // SysEx: save() is the SMF form (F0 <varlen> data F7); FluidSynth and
        // the hardware port want the wire form F0 <data> F7. (F085)
        if (SysExEvent *sx = dynamic_cast<SysExEvent *>(e)) {
            QByteArray wire;
            wire.append(char(0xF0));
            wire.append(sx->data());
            wire.append(char(0xF7));
            sendEnqueuedCommand(wire);
        } else {
            sendEnqueuedCommand(e->save());
        }

        // Update visualizer activity (thread-safe, works for all player modes)
        {
            NoteOnEvent *noteOn = dynamic_cast<NoteOnEvent *>(e);
            if (noteOn && noteOn->velocity() > 0) {
                channelActivity[e->channel()].store(noteOn->velocity(), std::memory_order_relaxed);
            }
        }

        if (isAlternativePlayer) {
            NoteOnEvent *n = dynamic_cast<NoteOnEvent *>(e);
            QMutexLocker locker(&playedNotesMutex);
            if (n && n->velocity() > 0) {
                playedNotes[n->channel()].append(n->note());
            } else if (n && n->velocity() == 0) {
                playedNotes[n->channel()].removeOne(n->note());
            } else {
                OffEvent *o = dynamic_cast<OffEvent *>(e);
                if (o) {
                    n = dynamic_cast<NoteOnEvent *>(o->onEvent());
                    if (n) {
                        playedNotes[n->channel()].removeOne(n->note());
                    }
                }
            }
        }
    }
}

QStringList MidiOutput::outputPorts() {
    QStringList ports;

    // init() swallows an RtMidiOut constructor failure and leaves _midiOut
    // null, so every dereference needs the guard MidiInput::inputPorts()
    // already has. The FluidSynth virtual port stays available in that case.
    if (_midiOut) {
        // Check outputs.
        unsigned int nPorts = _midiOut->getPortCount();

        for (unsigned int i = 0; i < nPorts; i++) {
            try {
                ports.append(QString::fromStdString(_midiOut->getPortName(i)));
            } catch (RtMidiError &) {
            }
        }
    }

#ifdef FLUIDSYNTH_SUPPORT
    // Add FluidSynth as a virtual output port
    ports.append(FLUIDSYNTH_PORT_NAME);
#endif

    return ports;
}

bool MidiOutput::setOutputPort(QString name) {
#ifdef FLUIDSYNTH_SUPPORT
    // Handle FluidSynth virtual port
    if (name == FLUIDSYNTH_PORT_NAME) {
        // Remember previous port in case FluidSynth init fails
        QString previousPort = _outPort;

        // Close any RtMidi port. closePort() frees state that the player and
        // MIDI-Thru threads use inside sendEnqueuedCommand(), so hold the
        // writer side while it runs (never across the recursive call below).
        if (_midiOut) {
            QWriteLocker locker(&_outputLock);
            _midiOut->closePort();
        }

        // Initialize FluidSynth engine
        FluidSynthEngine *engine = FluidSynthEngine::instance();
        if (!engine->isInitialized()) {
            if (!engine->initialize()) {
                qWarning() << "Failed to initialize FluidSynth engine";
                // Try to restore the previous port so user isn't left with no audio
                if (!previousPort.isEmpty() && previousPort != FLUIDSYNTH_PORT_NAME) {
                    setOutputPort(previousPort);
                }
                return false;
            }
        }
        {
            QWriteLocker locker(&_outputLock);
            _outPort = name;
        }
        return true;
    }
#endif

    if (!_midiOut) {
        return false;
    }

    // Locate the target port BEFORE touching the current routing: shutting
    // FluidSynth down first left a stale port name with a dead engine while
    // _outPort still said FluidSynth, i.e. silent playback with no rollback.
    unsigned int nPorts = _midiOut->getPortCount();
    int targetPort = -1;

    for (unsigned int i = 0; i < nPorts; i++) {
        try {
            // if the current port has the given name, select it
            if (_midiOut->getPortName(i) == name.toStdString()) {
                targetPort = static_cast<int>(i);
                break;
            }
        } catch (RtMidiError &) {
        }
    }

    if (targetPort < 0) {
        // port not found - current routing (and a running FluidSynth engine)
        // stays untouched
        return false;
    }

    // closePort()/openPort() and the FluidSynth teardown free objects that the
    // player thread and the MIDI-Thru callback thread read inside
    // sendEnqueuedCommand(), so no send may be in flight while they run.
    QWriteLocker locker(&_outputLock);

    try {
        _midiOut->closePort();
        _midiOut->openPort(static_cast<unsigned int>(targetPort));
    } catch (RtMidiError &error) {
        error.printMessage();
        return false;
    }

#ifdef FLUIDSYNTH_SUPPORT
    // Switching away from FluidSynth: shut the engine down only once the new
    // port is really open, so a failed open leaves the old routing playable.
    if (_outPort == FLUIDSYNTH_PORT_NAME) {
        FluidSynthEngine::instance()->shutdown();
    }
#endif

    _outPort = name;
    return true;
}

QString MidiOutput::outputPort() {
    return _outPort;
}

void MidiOutput::sendEnqueuedCommand(QByteArray array) {
    // Reader side of the port switch: keeps setOutputPort() from freeing the
    // FluidSynth synth or the RtMidi port under this send (the player thread
    // and RtMidi's MIDI-Thru callback thread both land here).
    QReadLocker locker(&_outputLock);

    if (_outPort != "") {
#ifdef FLUIDSYNTH_SUPPORT
        // Route to FluidSynth if it's the active output
        if (_outPort == FLUIDSYNTH_PORT_NAME) {
            FluidSynthEngine::instance()->sendMidiData(array);
            return;
        }
#endif

        // convert data to std::vector
        std::vector<unsigned char> message;

        foreach(char byte, array) {
            message.push_back(byte);
        }
        try {
            if (_midiOut) {
                _midiOut->sendMessage(&message);
            }
        } catch (RtMidiError &error) {
            error.printMessage();
        }
    }
}

void MidiOutput::setStandardChannel(int channel) {
    _stdChannel = channel;
}

int MidiOutput::standardChannel() {
    return _stdChannel;
}

void MidiOutput::sendProgram(int channel, int prog) {
    QByteArray array = QByteArray();
    array.append(0xC0 | channel);
    array.append(prog);
    sendCommand(array);
}

void MidiOutput::resetChannelPrograms()
{
    // Reset all 16 MIDI channels to program 0 (Acoustic Grand Piano in GM)
    for (int channel = 0; channel < 16; channel++) {
        sendProgram(channel, 0);
    }
}

bool MidiOutput::isConnected() {
    return _outPort != "";
}

#ifdef FLUIDSYNTH_SUPPORT
bool MidiOutput::isFluidSynthOutput() {
    return _outPort == FLUIDSYNTH_PORT_NAME;
}
#endif
