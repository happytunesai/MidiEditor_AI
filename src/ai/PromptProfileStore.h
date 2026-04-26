#ifndef PROMPTPROFILESTORE_H
#define PROMPTPROFILESTORE_H

#include "PromptProfile.h"

#include <QList>
#include <QObject>
#include <QString>

/**
 * \class PromptProfileStore
 *
 * \brief Persists \ref PromptProfile entries under
 *        \c "AI/prompt_profiles/" in QSettings("MidiEditor","NONE")
 *        and resolves the prompt to use for a given <provider, model>.
 *
 * Mirrors \ref ModelFavorites in shape. The store seeds one read-only
 * built-in profile on first construction (\b "GPT-5.5 Decisive") so the
 * same prompt that loops on \c openai:gpt-5.5* gets a focused
 * "commit-after-one-paragraph" rule appended without affecting any other
 * model.
 *
 * \section keys Storage layout
 * \code
 * AI/prompt_profiles/<id>/name
 * AI/prompt_profiles/<id>/system
 * AI/prompt_profiles/<id>/append_to_default     (bool)
 * AI/prompt_profiles/<id>/builtin               (bool)
 * AI/prompt_profiles/<id>/enabled               (bool)
 * AI/prompt_profiles/<id>/models                (QStringList of provider:model[*])
 * AI/prompt_profiles/order                      (QStringList of <id>)
 * AI/prompt_profiles/builtins_seeded            (bool, internal)
 * AI/prompt_profiles/builtins_version           (string, internal)
 * \endcode
 *
 * \section resolution Resolution rules
 * \ref resolveForModel returns the \b first enabled profile whose
 * \c models contains a pattern that matches \c "<provider>:<modelId>"
 * (case-insensitive; \c '*' suffix is the only supported glob).
 * \ref resolvePromptForModel layers the result over the editor defaults:
 *
 * 1. If a profile matches: return either \c profile.system (replace) or
 *    \c defaultPrompt + "\n\n" + profile.system (append).
 * 2. Else, if a non-empty user custom prompt was passed: return that.
 * 3. Else, return \c defaultPrompt.
 */
class PromptProfileStore : public QObject {
    Q_OBJECT
public:
    explicit PromptProfileStore(QObject *parent = nullptr);

    /** Returns all profiles in user-defined order (built-ins first). */
    QList<PromptProfile> profiles() const;

    /** Returns the first profile whose pattern set matches the model, else
     *  a default-constructed (id-empty) profile. */
    PromptProfile resolveForModel(const QString &provider,
                                  const QString &model) const;

    /** Layered resolution as documented in the class comment. */
    QString resolvePromptForModel(const QString &provider,
                                  const QString &model,
                                  const QString &defaultPrompt,
                                  const QString &userCustom) const;

    /** Insert or update a profile (matches on \c p.id). Refuses to overwrite
     *  the body / models of a built-in (only \c enabled may be toggled). */
    void upsert(const PromptProfile &p);

    /** Remove a profile by id. Returns false (and does nothing) for built-ins. */
    bool remove(const QString &id);

    /** Replace the persisted display order. Ids not in the store are dropped. */
    void setOrder(const QStringList &orderedIds);

    /** Generate a new unique id (uuid form, no braces). */
    static QString newId();

    /** Convenience used by tests and the dialog. */
    static bool patternMatches(const QString &pattern,
                               const QString &providerColonModel);

    /** Re-seed the shipped built-ins; used by tests after wiping QSettings. */
    void ensureBuiltinsSeeded(bool force = false);

private:
    static QString idKey(const QString &id, const QString &leaf);
    PromptProfile loadProfile(const QString &id) const;
    void saveProfile(const PromptProfile &p) const;
    QStringList persistedOrder() const;
    void persistOrder(const QStringList &ids) const;
};

#endif // PROMPTPROFILESTORE_H
