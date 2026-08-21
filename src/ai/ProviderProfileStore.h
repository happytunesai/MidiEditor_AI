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
 * <id> is the display name percent-encoded (\ref encodeName): '/' is the
 * QSettings group separator, so a raw name would silently fan out into
 * nested groups ("HF / local" -> three levels) and could never be found
 * again. Everything outside [A-Za-z0-9 ._-] is encoded, which also keeps
 * the id safe for the portable-mode INI backend.
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
    /// Create or overwrite a profile (the key is stored separately).
    static bool save(const Profile &profile, const QString &apiKey);
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

    /// Remember/forget the last applied profile (a hint for activeProfileName()).
    static void setActiveProfileHint(const QString &name);

    /// Trimmed, whitespace-collapsed, length-capped display name. Empty means
    /// "not a usable profile name".
    static QString normalizeName(const QString &raw);
    /// Display name -> settings-group id (percent encoding, see file comment).
    static QString encodeName(const QString &name);
    /// Inverse of \ref encodeName.
    static QString decodeName(const QString &id);

    /// Longest accepted display name.
    static int maxNameLength();
};

#endif // PROVIDERPROFILESTORE_H
