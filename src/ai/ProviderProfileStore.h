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

    /// Sorted list of stored profile names.
    static QStringList profileNames();
    /// Load one profile; ok=false when the name does not exist.
    static Profile load(const QString &name, bool *ok = nullptr);
    /// Create or overwrite a profile (the key is stored separately).
    static bool save(const Profile &profile, const QString &apiKey);
    static bool remove(const QString &name);
    /// Apply a stored profile to the active settings keys.
    static bool apply(const QString &name, QString *error = nullptr);
};

#endif // PROVIDERPROFILESTORE_H
