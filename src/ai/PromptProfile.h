#ifndef PROMPTPROFILE_H
#define PROMPTPROFILE_H

#include <QString>
#include <QStringList>

/**
 * \struct PromptProfile
 *
 * \brief A user- or built-in-defined system-prompt override bound to a set
 *        of \c "<provider>:<modelId>" patterns (with optional \c '*' suffix
 *        glob).
 *
 * Persisted by \ref PromptProfileStore under
 * \c "AI/prompt_profiles/<id>/...". When the active model matches any pattern
 * in \ref models and \ref enabled is true, the profile's \ref system text
 * either replaces or appends to the default system prompt depending on
 * \ref appendToDefault.
 */
struct PromptProfile {
    QString id;                 ///< Stable internal id (uuid-like).
    QString name;               ///< Human label shown in the dialog.
    QString system;             ///< The prompt body (replace or append).
    bool appendToDefault = true;///< true → append, false → replace.
    bool builtin = false;       ///< Read-only shipped profile.
    bool enabled = true;        ///< Resolution skips disabled profiles.
    QStringList models;         ///< "<provider>:<modelId>" or "<provider>:<prefix>*".
};

#endif // PROMPTPROFILE_H
