#ifndef PROVIDERPROFILESTORE_H
#define PROVIDERPROFILESTORE_H

/*
 * Phase 50 (v2.3): named AI endpoint configurations ("provider profiles").
 * A profile bundles {provider, base URL, model}; its API key lives in the
 * settings scope next to today's per-provider keys and NEVER travels in file
 * presets. Applying a profile is a macro over the four active settings keys
 * (AI/provider, AI/api_base_url, AI/api_key, AI/model) - AiClient stays
 * untouched and "no profile selected" keeps today's ad-hoc behaviour.
 * Mirrors the PromptProfileStore pattern (non-UI, seam-tested).
 * NOT to be confused with Prompt Profiles (per-model system prompts).
 *
 * Storage layout (everything through AppPaths::settings(), never a raw
 * QSettings - the routing guard in test_app_paths pins that):
 *
 *   AI/providerProfiles/<id>/name         display name as the user typed it
 *   AI/providerProfiles/<id>/provider     "openai" | ... | "custom"
 *   AI/providerProfiles/<id>/base_url
 *   AI/providerProfiles/<id>/model        may be empty
 *   AI/api_key/profile:<id>               the profile's key (same scope as
 *                                         today's AI/api_key/<provider>)
 *   AI/provider_profile_active            last applied profile name (a hint,
 *                                         see activeProfileName())
 *
 * Model-list scope (\ref modelScopeId): favourites (AI/favorites/<scope>) and
 * the cached model list are keyed per endpoint, not per provider, so two
 * Custom profiles pointing at different servers keep their own lists.
 *
 * <id> is the display name percent-encoded (\ref encodeName): '/' is the
 * QSettings group separator, so a raw name would silently fan out into
 * nested groups ("HF / local" -> three levels) and could never be found
 * again. Everything outside [A-Za-z0-9 ._-] is encoded, which also keeps
 * the id safe for the portable-mode INI backend. An encoding that would
 * overrun the backend's key-name limit is shortened (see \ref encodeName);
 * the display name itself is always kept verbatim in the /name value.
 */

#include <QString>
#include <QStringList>

class ProviderProfileStore {
public:
    struct Profile {
        QString name;
        QString provider;  ///< "openai" | "openrouter" | "gemini" | "ollama" | "custom"
        QString baseUrl;   ///< meaningful for custom; informational otherwise
        QString model;     ///< optional default model
    };

    /// Sorted list of stored profile names (display names).
    static QStringList profileNames();
    /// True when a profile of that name exists.
    static bool exists(const QString &name);
    /// Load one profile; ok=false when the name does not exist.
    static Profile load(const QString &name, bool *ok = nullptr);
    /// The API key stored with a profile (empty for local endpoints).
    static QString apiKeyFor(const QString &name);
    /** Create or overwrite a profile (the key is stored separately).
     *
     *  Returns false when the profile is unusable (no name, no provider) AND
     *  when the settings backend refused the write: the values are read back
     *  through a fresh handle, so a caller never gets a "saved" answer for a
     *  profile that is not there. A half-written new profile is rolled back so
     *  no orphan API key stays behind. */
    static bool save(const Profile &profile, const QString &apiKey);

    /** Delete a profile: its group, its API key, the active hint if it points
     *  here - and the state that hangs off its endpoint scope (favourites and
     *  the cached model list), because the scope is derived from the name and
     *  a later profile of the same name would inherit it. Only a profile's OWN
     *  "custom:profile:<id>" scope is cleared; the provider-wide scope that
     *  non-custom profiles share is never touched. */
    static bool remove(const QString &name);

    /** Apply a stored profile to the active settings keys.
     *
     *  Sets AI/provider, AI/api_base_url and AI/api_key; AI/model only when
     *  the profile carries one. The per-provider key memory
     *  (AI/api_key/<provider>) is refreshed too so a later provider switch
     *  does not hand the client a key from a different endpoint - but only
     *  when the profile HAS a key, so a keyless local-server profile never
     *  erases a remembered cloud key. The profile's own key at
     *  AI/api_key/profile:<id> is never touched by any provider switch.
     */
    static bool apply(const QString &name, QString *error = nullptr);

    /** The profile the CURRENT active settings match, or an empty string when
     *  the configuration is ad-hoc. Derived state on purpose: the UI shows a
     *  profile name only while provider, base URL, model and key still equal
     *  the stored ones, so any edit falls back to "no profile" without the
     *  widgets having to track edits. */
    static QString activeProfileName();

    /// activeProfileName() for an arbitrary quadruple (the settings pages
    /// compare their unsaved UI fields with this).
    static QString nameMatching(const QString &provider, const QString &baseUrl,
                                const QString &model, const QString &apiKey);

    /// True when the stored profile \a name describes exactly this quadruple.
    /// A profile without a model pins only the endpoint and ignores \a model.
    static bool matches(const QString &name, const QString &provider,
                        const QString &baseUrl, const QString &model,
                        const QString &apiKey);

    /// True when the stored profile \a name describes this ENDPOINT - provider,
    /// base URL and key - whatever model it was saved with. This is the
    /// identity that model lists and favourites hang off: picking another model
    /// does not move you to a different server.
    static bool matchesEndpoint(const QString &name, const QString &provider,
                                const QString &baseUrl, const QString &apiKey);

    /// \ref nameMatching for the endpoint alone (model ignored).
    ///
    /// \a preferredName is the caller's LIVE selection (the profile its picker
    /// currently shows). When that profile still describes this endpoint it
    /// wins over the stored active hint - two profiles may share one endpoint,
    /// and without this the provider picker would name the other one while the
    /// profile picker next to it names the selected one.
    static QString nameMatchingEndpoint(const QString &provider,
                                        const QString &baseUrl,
                                        const QString &apiKey,
                                        const QString &preferredName = QString());

    // --- model-list / favourites scope -----------------------------------
    //
    // Favourites live at AI/favorites/<scope> and the cached model list at the
    // same key inside midipilot_models.json. The scope is:
    //
    //   "<provider>"                 for openai / openrouter / gemini / ollama
    //                                and for ad-hoc Custom settings
    //   "custom:profile:<id>"        while a stored CUSTOM profile describes
    //                                the endpoint (<id> = \ref encodeName)
    //
    // Only Custom is split per profile, because only Custom changes which
    // server answers /models. An OpenAI or Gemini profile talks to the same
    // catalogue as every other profile of that provider and therefore shares
    // the plain provider scope on purpose. Ad-hoc Custom settings keep the
    // plain "custom" scope, so favourites saved before profiles existed stay
    // exactly where they were.

    /// Scope for an explicit endpoint (the settings page passes its unsaved
    /// fields, the chat footer the live client values).
    static QString modelScopeId(const QString &provider, const QString &baseUrl,
                                const QString &apiKey);

    /// \ref modelScopeId for the currently stored connection settings.
    static QString activeModelScopeId();

    /// The scope a stored profile owns. Empty when no such profile exists;
    /// the provider itself for non-custom profiles (they share it).
    static QString modelScopeIdForProfile(const QString &name);

    /// Remember/forget the last applied profile (a hint for activeProfileName()).
    static void setActiveProfileHint(const QString &name);

    // --- pure decision helpers -------------------------------------------
    //
    // No settings access, no widgets: the rules the two connection pickers
    // (AiSettingsWidget page, MidiPilotWidget footer) have to agree on, in one
    // place where the store test can pin their state tables.

    /// What a provider switch must do with AI/api_key/<provider> for the
    /// provider being LEFT. \ref keyMemoryActionOnLeave decides it.
    enum class KeyMemoryAction {
        Keep,   ///< leave the remembered key alone
        Store,  ///< write the field's key
        Erase   ///< the user cleared the key on purpose - drop the memory
    };

    /** The per-provider key memory follows the FIELD, but only for a user who
     *  was really on that provider and really emptied its key.
     *
     *  An empty field alone must not erase the memory: applying a keyless local
     *  profile POURS an empty key into the field, and wiping the remembered
     *  cloud token there would destroy the key the store deliberately protects
     *  (see \ref apply). An empty field the user typed away is the opposite -
     *  without an Erase the cleared key silently returns on the next provider
     *  round-trip and is sent again.
     *
     *  \a userEditedKeyField is the caller's dirty flag: true only when the key
     *  field was edited BY THE USER while this provider was showing (a
     *  setText() from a switch or a profile does not count). */
    static KeyMemoryAction keyMemoryActionOnLeave(const QString &fieldKey,
                                                  bool userEditedKeyField);

    /** The key a provider switch may re-attach when ENTERING \a provider.
     *
     *  \a rememberedKey is AI/api_key/<provider>, \a endpointNeedsKey the
     *  caller's verdict for the endpoint that is about to be in force
     *  (AiClient::providerRequiresKey - kept out of this header so the store
     *  stays free of the client).
     *
     *  Only Custom is filtered, and only by locality: its endpoint is
     *  user-defined, so the same provider id can mean a cloud router one minute
     *  and a server on this machine the next. Re-attaching a remembered cloud
     *  token to a loopback endpoint would put that token into a local server's
     *  Authorization header. Every other provider keeps its memory verbatim -
     *  an Ollama behind an auth proxy still gets its key back. */
    static QString keyMemoryOnEnter(const QString &provider,
                                    const QString &rememberedKey,
                                    bool endpointNeedsKey);

    /** Should a connection picker offer the plain ad-hoc "Custom" entry?
     *
     *  \a matchedProfileName is the stored profile the LIVE endpoint resolves
     *  to (\ref nameMatchingEndpoint), resolved once by the caller so this rule
     *  and the picker's selection can never disagree. \a builtInBaseUrls are
     *  the default endpoints of the built-in providers - a base URL equal to
     *  one of them is what switching provider writes, not something a user
     *  typed. Trailing slashes are normalised on both sides. */
    static bool shouldOfferAdHocCustomEntry(const QString &provider,
                                            const QString &baseUrl,
                                            const QString &matchedProfileName,
                                            const QStringList &builtInBaseUrls);

    /// Trimmed, whitespace-collapsed, length-capped display name. Empty means
    /// "not a usable profile name".
    static QString normalizeName(const QString &raw);
    /// Display name -> settings-group id (percent encoding, see file comment).
    /// Ids longer than \ref maxEncodedIdLength() are shortened to a readable
    /// head plus a digest ("head~<hex>"), because a Windows registry key name
    /// has a hard length limit and a 64-character non-Latin name encodes to
    /// several hundred characters. The mapping stays deterministic and the
    /// display name is kept verbatim in the group's "name" value. '~' cannot
    /// appear in a plain encoding, so a shortened id never collides with one.
    static QString encodeName(const QString &name);
    /// Inverse of \ref encodeName - for plain (unshortened) ids only.
    static QString decodeName(const QString &id);

    /// Longest accepted display name.
    static int maxNameLength();
    /// Longest id \ref encodeName may produce.
    static int maxEncodedIdLength();
};

#endif // PROVIDERPROFILESTORE_H
