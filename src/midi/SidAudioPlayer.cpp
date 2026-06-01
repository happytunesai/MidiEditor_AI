/*
 * MidiEditor AI - authentic SID playback implementation (see SidAudioPlayer.h).
 */
#include "SidAudioPlayer.h"

#include "SidFpPlayer.h" // sidfp::Renderer (from the vendored libsidplayfp adapter)

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QIODevice>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <vector>

namespace { const int kSampleRate = 44100; }

/**
 * A sequential, read-only QIODevice that renders SID PCM on demand: the
 * QAudioSink calls readData() whenever it needs more audio, and we run the
 * libsidplayfp engine just enough to fill the request. A SID loops forever, so
 * the stream never truly ends (short renders are padded with silence).
 */
class SidPullDevice : public QIODevice {
public:
    bool start(const QByteArray &bytes, int song, qint64 skipSamples) {
        if (!_r.open(reinterpret_cast<const uint8_t *>(bytes.constData()),
                     static_cast<std::size_t>(bytes.size()), song, kSampleRate))
            return false;
        // Seek by rendering+discarding up to the start position (SID has no
        // random access). Brief for early positions; longer if you start deep.
        if (skipSamples > 0) {
            std::vector<short> tmp(8192);
            qint64 left = skipSamples;
            while (left > 0) {
                const int want = static_cast<int>(std::min<qint64>(left, (qint64)tmp.size()));
                const int got = _r.render(tmp.data(), want);
                if (got <= 0) break;
                left -= got;
            }
        }
        return QIODevice::open(QIODevice::ReadOnly);
    }
    qint64 readData(char *data, qint64 maxlen) override {
        if (maxlen <= 0) return 0;
        const int maxSamples = static_cast<int>(maxlen / 2);
        int n = _r.render(reinterpret_cast<short *>(data), maxSamples);
        if (n < 0) n = 0;
        if (n < maxSamples) // keep the stream flowing
            std::memset(data + n * 2, 0, static_cast<std::size_t>((maxSamples - n) * 2));
        return static_cast<qint64>(maxSamples) * 2;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
    bool   isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
    void   setVoiceMuted(int voice, bool muted) { _r.setVoiceMuted(voice, muted); }
private:
    sidfp::Renderer _r;
};

SidAudioPlayer *SidAudioPlayer::instance() {
    static SidAudioPlayer inst;
    return &inst;
}

SidAudioPlayer::SidAudioPlayer(QObject *parent) : QObject(parent) {
    _posTimer = new QTimer(this);
    _posTimer->setInterval(50);
    connect(_posTimer, &QTimer::timeout, this, [this] {
        const int pos = positionMs();
        if (_lengthMs > 0 && pos >= _lengthMs) { // reached the note-roll end
            stop();
            emit finished();
            return;
        }
        emit positionChanged(pos);
    });
}

void SidAudioPlayer::setArmed(bool armed) {
    if (_armed == armed) return;
    _armed = armed;
    if (!armed && _playing) stop();
    emit armedChanged(_armed);
}

int SidAudioPlayer::positionMs() const {
    // Real-time wall clock: the SID plays in real time and the note roll is a
    // real-time timeline, so this keeps the cursor locked to the notes (the
    // audio-buffer position from processedUSecs() lagged progressively).
    if (!_playing) return _fromMs;
    return _fromMs + static_cast<int>(_clock.elapsed());
}

void SidAudioPlayer::setVoiceMuted(int voice, bool muted) {
    if (voice < 0 || voice > 2) return;
    if (muted) _muteMask |= (1 << voice);
    else       _muteMask &= ~(1 << voice);
    if (_dev) _dev->setVoiceMuted(voice, muted); // live, while playing
}

void SidAudioPlayer::setSource(const QString &sidPath, int song) {
    if (_playing) stop();
    _path = sidPath;
    _song = song;
    _bytes.clear();
    if (!sidPath.isEmpty()) {
        QFile f(sidPath);
        if (f.open(QIODevice::ReadOnly)) {
            _bytes = f.readAll();
            f.close();
        }
    }
    // Emulation plays the original .sid bytes; if they're gone (a non-.sid file
    // was loaded) it can't run, so drop any arming so the C64 button/switch
    // don't pretend Emulation is active. Then tell the UI to re-gate.
    if (_bytes.isEmpty() && _armed)
        setArmed(false);          // emits armedChanged
    emit sourceChanged();
}

bool SidAudioPlayer::play(int fromMs, int lengthMs) {
    if (_playing) return true;
    if (_bytes.isEmpty()) return false;
    _fromMs = fromMs < 0 ? 0 : fromMs;
    _lengthMs = lengthMs;

    QAudioFormat fmt;
    fmt.setSampleRate(kSampleRate);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice out = QMediaDevices::defaultAudioOutput();
    if (out.isNull()) return false;

    _dev = new SidPullDevice();
    const qint64 skip = static_cast<qint64>(_fromMs) * kSampleRate / 1000;
    if (!_dev->start(_bytes, _song, skip)) {
        delete _dev;
        _dev = nullptr;
        return false;
    }
    // Apply the current mute mask (channel mutes set before pressing Play).
    for (int v = 0; v < 3; ++v)
        _dev->setVoiceMuted(v, (_muteMask >> v) & 1);

    _sink = new QAudioSink(out, fmt, this);
    _sink->start(_dev);
    _clock.start();
    _playing = true;
    _posTimer->start();
    emit stateChanged(true);
    return true;
}

void SidAudioPlayer::stop() {
    if (_posTimer) _posTimer->stop();
    if (_sink) {
        _sink->stop();
        _sink->deleteLater();
        _sink = nullptr;
    }
    if (_dev) {
        _dev->close();
        delete _dev;
        _dev = nullptr;
    }
    if (_playing) {
        _playing = false;
        emit stateChanged(false);
    }
}
