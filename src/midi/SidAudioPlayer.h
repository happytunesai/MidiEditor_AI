/*
 * MidiEditor AI - authentic SID playback (Phase 42 / B).
 *
 * Plays the *original* .sid file through libsidplayfp's cycle-accurate engine
 * (via sidfp::Renderer) to a QAudioSink, so the user can A/B the real tune
 * against the converted MIDI they're editing. Singleton; lives on the GUI
 * thread, the QAudioSink pulls PCM on demand.
 */
#ifndef SIDAUDIOPLAYER_H
#define SIDAUDIOPLAYER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>

class QAudioSink;
class QTimer;
class SidPullDevice;

class SidAudioPlayer : public QObject {
    Q_OBJECT
public:
    static SidAudioPlayer *instance();

    /// Remember the .sid that was just imported (so playback can re-run it).
    /// Clears any current source/playback when given an empty path.
    void setSource(const QString &sidPath, int song = 0);
    bool hasSource() const { return !_bytes.isEmpty(); }
    QString sourcePath() const { return _path; }
    bool isPlaying() const { return _playing; }

    /// "Armed": the C64 button selected Emulation mode, so the transport plays
    /// the original .sid instead of the converted MIDI. Toggled by the button;
    /// does NOT start playback by itself.
    void setArmed(bool armed);
    bool isArmed() const { return _armed; }

    /// Current playback position in ms (start offset + audio played so far).
    int positionMs() const;

    /// Mute/unmute SID voice 0-2 (mirrors the editor's per-channel mute).
    /// Takes effect immediately while playing and on the next play().
    void setVoiceMuted(int voice, bool muted);

    /// Render the loaded .sid to an audio file through the authentic libsidplayfp
    /// engine (the ORIGINAL tune, not the converted MIDI), for the window
    /// [fromMs, toMs). \a fileType is "wav" | "ogg" | "flac" (MP3 is produced by
    /// the caller via a temp WAV + LAME). \a oggQuality is 10-100 (ignored for
    /// WAV/FLAC). Uses the bundled libsndfile (loaded dynamically). Blocking -
    /// call from a worker thread. Returns false on error/cancel. hasSource()
    /// must be true.
    bool exportToFile(const QString &path, const QString &fileType,
                      int fromMs, int toMs, int oggQuality = 60,
                      std::function<void(int)> progress = nullptr,
                      std::atomic<bool> *cancel = nullptr);

public slots:
    /// Start playback from \a fromMs (the editor's cursor position). If
    /// \a lengthMs > 0, playback auto-stops there (the end of the note roll) so
    /// it behaves like real playback instead of looping forever.
    bool play(int fromMs = 0, int lengthMs = 0);
    void stop();

signals:
    void stateChanged(bool playing);  ///< play/stop (drives the C64 button glow)
    void armedChanged(bool armed);    ///< Emulation arming toggled
    void positionChanged(int ms);     ///< ~50 ms ticks while playing (cursor sync)
    void finished();                  ///< reached lengthMs (end of the note roll)
    void sourceChanged();             ///< setSource() ran: a .sid is/!is now loaded
                                      ///< (gates Emulation availability in the UI)

private:
    explicit SidAudioPlayer(QObject *parent = nullptr);

    QString        _path;
    QByteArray     _bytes;
    int            _song = 0;
    QAudioSink    *_sink = nullptr;
    SidPullDevice *_dev = nullptr;
    QTimer        *_posTimer = nullptr;
    QElapsedTimer  _clock;          ///< real-time playback clock (cursor sync)
    int            _fromMs = 0;
    int            _lengthMs = 0;   ///< auto-stop position (0 = no limit)
    int            _muteMask = 0;   ///< bit v set => SID voice v muted
    bool           _playing = false;
    bool           _armed = false;
};

#endif // SIDAUDIOPLAYER_H
