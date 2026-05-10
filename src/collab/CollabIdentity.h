/*
 * MidiEditor AI
 *
 * Collaboration: peer identity (display name + machine UUID).
 *
 * This is intentionally lightweight: a free-text display name plus a stable
 * per-machine UUID. No keys, no certificates, no pairing phrase. The author
 * of a PR is whoever the recipient chooses to trust — same trust model as a
 * Git patch on a mailing list. Crypto can be layered on later (signed
 * bundles, encrypted-at-rest history) without changing this core schema.
 */

#ifndef COLLABIDENTITY_H
#define COLLABIDENTITY_H

#include <QString>

class CollabIdentity {
public:
    /**
     * \brief The user-visible display name.
     *
     * Persisted in QSettings (Collab/identity/displayName). Defaults to the
     * OS username on first call; falls back to "Anonymous" if no username
     * is available. Free text — the user can change it any time.
     */
    static QString displayName();
    static void setDisplayName(const QString &name);

    /**
     * \brief The stable per-machine identifier.
     *
     * Persisted in QSettings (Collab/identity/machineId). Generated lazily
     * on first call via QUuid::createUuid(). Stable across application
     * restarts; only changes if the user explicitly regenerates it (which
     * is destructive — all PRs created before the regeneration will carry
     * the old machineId).
     */
    static QString machineId();

    /**
     * \brief Generate a fresh machine UUID.
     *
     * Destructive: any future PR will be tagged with the new ID. Existing
     * PR bundles in inbox/outbox keep their original ID. Use only when the
     * user explicitly opts in (e.g. "I cloned this VM, give me a new ID").
     */
    static void regenerateMachineId();

    /**
     * \brief Combined "Name (machineId-prefix)" for UI display.
     *
     * Disambiguates two users with the same display name. Returns e.g.
     * "Alice (a1b2c3d4)".
     */
    static QString displayLabel();
};

#endif // COLLABIDENTITY_H
