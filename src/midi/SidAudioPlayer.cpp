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
#include <QLibrary>
#include <QMediaDevices>
#include <QMutex>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <vector>

namespace { const int kSampleRate = 44100; }

// ---------------------------------------------------------------------------
// Minimal libsndfile binding for the SID audio export (WAV/OGG/FLAC). We load
// the sndfile.dll that FluidSynth already ships (no link-time dependency / dev
// package), so it's exactly the version FluidSynth uses - ABI-safe. The
// libsndfile struct/function ABI below has been stable for ~20 years.
// ---------------------------------------------------------------------------
namespace {
struct SfInfo { qint64 frames; int samplerate; int channels; int format; int sections; int seekable; };
typedef void *SndHandle;
typedef SndHandle (*Fn_open)(const wchar_t *, int, SfInfo *); // sf_wchar_open (Windows wide path)
typedef qint64   (*Fn_write)(SndHandle, const short *, qint64); // sf_write_short
typedef int      (*Fn_close)(SndHandle);                        // sf_close
typedef int      (*Fn_command)(SndHandle, int, void *, int);    // sf_command

struct SndApi {
    Fn_open open = nullptr; Fn_write write = nullptr;
    Fn_close close = nullptr; Fn_command command = nullptr;
    bool ok() const { return open && write && close; }
};

const SndApi &sndApi() {
    static const SndApi api = [] {
        SndApi a;
        static QLibrary lib(QStringLiteral("sndfile")); // app-lifetime; FluidSynth also keeps it loaded
        if (lib.load()) {
            a.open    = reinterpret_cast<Fn_open>(lib.resolve("sf_wchar_open"));
            a.write   = reinterpret_cast<Fn_write>(lib.resolve("sf_write_short"));
            a.close   = reinterpret_cast<Fn_close>(lib.resolve("sf_close"));
            a.command = reinterpret_cast<Fn_command>(lib.resolve("sf_command"));
        }
        return a;
    }();
    return api;
}

// libsndfile constants (stable public API).
constexpr int kSfmWrite        = 0x20;
constexpr int kSfFormatWav     = 0x010000;
constexpr int kSfFormatFlac    = 0x170000;
constexpr int kSfFormatOgg     = 0x200000;
constexpr int kSfFormatPcm16   = 0x0002;
constexpr int kSfFormatVorbis  = 0x0060;
constexpr int kSfcSetVbrQuality = 0x1300; // SFC_SET_VBR_ENCODING_QUALITY (double 0..1)
} // namespace

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
        int n;
        {
            // _r is rendered here on the QAudioSink pull thread while the GUI
            // thread may call setVoiceMuted(); serialise the two so they don't
            // touch the libsidplayfp renderer concurrently (BUG-CORE-006).
            QMutexLocker lock(&_mutex);
            n = _r.render(reinterpret_cast<short *>(data), maxSamples);
        }
        if (n < 0) n = 0;
        if (n < maxSamples) // keep the stream flowing
            std::memset(data + n * 2, 0, static_cast<std::size_t>((maxSamples - n) * 2));
        return static_cast<qint64>(maxSamples) * 2;
    }
    qint64 writeData(const char *, qint64) override { return -1; }
    bool   isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return (1 << 20) + QIODevice::bytesAvailable(); }
    void   setVoiceMuted(int voice, bool muted) {
        QMutexLocker lock(&_mutex); // serialise with the render() pull thread (BUG-CORE-006)
        _r.setVoiceMuted(voice, muted);
    }
private:
    QMutex          _mutex; // guards _r between the QAudioSink pull thread and the GUI
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

bool SidAudioPlayer::exportToFile(const QString &path, const QString &fileType,
                                  int fromMs, int toMs, int oggQuality,
                                  std::function<void(int)> progress,
                                  std::atomic<bool> *cancel) {
    if (_bytes.isEmpty())
        return false;

    const QString t = fileType.toLower();
    int sfFormat = 0;
    const bool isOgg = (t == QStringLiteral("ogg") || t == QStringLiteral("oga"));
    if (t == QStringLiteral("wav"))       sfFormat = kSfFormatWav  | kSfFormatPcm16;
    else if (t == QStringLiteral("flac")) sfFormat = kSfFormatFlac | kSfFormatPcm16;
    else if (isOgg)                       sfFormat = kSfFormatOgg  | kSfFormatVorbis;
    else return false; // MP3 is produced by the caller (temp WAV + LAME)

    const SndApi &sf = sndApi();
    if (!sf.ok())
        return false; // sndfile.dll not loadable

    // Render the ORIGINAL .sid through the cycle-accurate engine (fresh Renderer
    // so it doesn't disturb live playback).
    sidfp::Renderer r;
    if (!r.open(reinterpret_cast<const uint8_t *>(_bytes.constData()),
                static_cast<std::size_t>(_bytes.size()), _song, kSampleRate))
        return false;

    const qint64 from = fromMs > 0 ? qint64(fromMs) : 0;
    const qint64 to   = (toMs > fromMs) ? qint64(toMs) : from;
    const qint64 startSample = from * kSampleRate / 1000;
    qint64 totalSamples = (to - from) * qint64(kSampleRate) / 1000;
    if (totalSamples < 0) totalSamples = 0;

    // Seek to the window start by rendering+discarding (SID has no random access).
    {
        std::vector<short> skip(8192);
        qint64 left = startSample;
        while (left > 0) {
            if (cancel && cancel->load()) return false;
            const int want = static_cast<int>(std::min<qint64>(left, (qint64)skip.size()));
            const int got = r.render(skip.data(), want);
            if (got <= 0) break;
            left -= got;
        }
    }

    SfInfo info{};
    info.samplerate = kSampleRate;
    info.channels   = 1;
    info.format     = sfFormat;
    SndHandle h = sf.open(reinterpret_cast<const wchar_t *>(path.utf16()), kSfmWrite, &info);
    if (!h)
        return false;
    if (isOgg && sf.command) {
        double q = qBound(0.0, oggQuality / 100.0, 1.0);
        sf.command(h, kSfcSetVbrQuality, &q, static_cast<int>(sizeof(double)));
    }

    std::vector<short> buf(16384);
    qint64 written = 0;
    bool ok = true;
    while (written < totalSamples) {
        if (cancel && cancel->load()) { ok = false; break; }
        const int want = static_cast<int>(std::min<qint64>(totalSamples - written, (qint64)buf.size()));
        int got = r.render(buf.data(), want);
        if (got <= 0) {
            // A SID loops forever, so render normally keeps yielding; if it ever
            // stops, pad with silence to the requested length (bounded by the
            // window) so the file matches the note-roll instead of truncating.
            std::fill_n(buf.begin(), want, short(0));
            got = want;
        }
        if (sf.write(h, buf.data(), got) != got) { ok = false; break; }
        written += got;
        if (progress) {
            const int pct = totalSamples > 0
                ? static_cast<int>(written * 100 / totalSamples) : 100;
            progress(qBound(0, pct, 100));
        }
    }
    sf.close(h);
    if (!ok)
        QFile::remove(path); // drop partial / cancelled output
    return ok;
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
