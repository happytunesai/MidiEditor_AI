/*
 * MidiEditor AI
 *
 * IceConfig — settings I/O for the WebRTC ICE-server list (Plan §11.10,
 * Phase 9.6). Defaults to Google's free public STUN pool; the user can
 * replace the list in Settings → Collaboration to use self-hosted
 * STUN/TURN, or clear it to disable WebRTC mode entirely.
 *
 * The list is stored in QSettings as a single multi-line string,
 * one URL per line, identical to what a user pastes into the GUI
 * field. Parsed on demand into a vector of `rtc::IceServer`-compatible
 * URI strings.
 */

#ifndef ICECONFIG_H
#define ICECONFIG_H

#ifdef MIDIEDITOR_WEBRTC_ENABLED

#include <QString>
#include <QStringList>

class IceConfig {
public:
    /** \brief Google's public STUN pool, used as the default when the
     *  user hasn't configured their own. Per the user-supplied list
     *  in §11.10. Dual-port coverage (3478/19302 + 5349) so at least
     *  one endpoint is reachable from networks that block specific
     *  ports. */
    static QStringList googleDefaults();

    /** \brief Read the user's configured ICE-server list from
     *  QSettings ("Collab/lan/iceServers"). Falls back to
     *  \ref googleDefaults when the setting is unset or empty. Each
     *  entry is one URI (e.g. "stun:stun.l.google.com:19302" or
     *  "turn:user:pass@host:port"). Blank lines and lines starting
     *  with '#' are skipped. */
    static QStringList load();

    /** \brief Persist a list (one URI per line) to QSettings. Pass
     *  empty list to revert to defaults on next load. */
    static void save(const QStringList &uris);
};

#endif // MIDIEDITOR_WEBRTC_ENABLED

#endif // ICECONFIG_H
