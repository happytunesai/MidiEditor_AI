#include "ProviderProfileStore.h"

#include "../AppPaths.h"
#include "ModelFavorites.h"
#include "ModelListCache.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QSettings>
#include <QStringList>

namespace {

constexpr const char *kRoot        = "AI/providerProfiles";
constexpr const char *kKeyPrefix   = "AI/api_key/profile:";
constexpr const char *kActiveHint  = "AI/provider_profile_active";

// The four active keys a profile is a macro over.
constexpr const char *kActiveProvider = "AI/provider";
constexpr const char *kActiveBaseUrl  = "AI/api_base_url";
constexpr const char *kActiveApiKey   = "AI/api_key";
constexpr const char *kActiveModel    = "AI/model";

constexpr int kMaxNameLength = 64;

// Longest id encodeName() may hand to the settings backend. A Windows registry
// key NAME is capped (the documented limit counts the whole path from the root
// key), and a 64-character CJK name percent-encodes to 576 characters - the
// write then fails while the profile's API key, a plain value name with a far
// higher limit, survives. 120 leaves room for the "Software\<org>\<app>\
// AI\providerProfiles\" prefix even under the long test scope.
constexpr int kMaxEncodedIdLength = 120;
// How much of the display name a shortened id keeps for readability, and how
// many hex digits of the digest make it unique.
constexpr int kShortIdHeadChars = 8;
constexpr int kShortIdDigestChars = 16;
// Separator inside a shortened id. Deliberately OUTSIDE the safe set below, so
// a literal name containing it encodes to "%7E" and no plain id can ever look
// like a shortened one.
constexpr char kShortIdSeparator = '~';

// The only provider whose endpoint (and therefore model list) is user-defined.
constexpr const char *kCustomProvider = "custom";
// Scope prefix for a custom profile's favourites / cached model list.
constexpr const char *kCustomScopePrefix = "custom:profile:";

// Phase 45 / PORTABLE-SPLIT-001: everything through AppPaths, so portable
// installs and tests see one store instead of one per backend.
std::unique_ptr<QSettings> settings()
{
    return AppPaths::settings();
}

QString groupOf(const QString &id, const QString &leaf)
{
    return QString::fromLatin1(kRoot) + QLatin1Char('/') + id
           + QLatin1Char('/') + leaf;
}

QString keyKeyOf(const QString &id)
{
    return QString::fromLatin1(kKeyPrefix) + id;
}

// Everything outside [A-Za-z0-9 ._-] becomes "%XX" over the UTF-8 bytes.
QString percentEncode(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    QString out;
    out.reserve(utf8.size());
    for (int i = 0; i < utf8.size(); ++i) {
        const unsigned char u = static_cast<unsigned char>(utf8.at(i));
        const bool safe = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                          || (u >= '0' && u <= '9')
                          || u == '.' || u == '_' || u == '-' || u == ' ';
        if (safe) {
            out += QLatin1Char(static_cast<char>(u));
        } else {
            out += QLatin1Char('%');
            out += QString::number(u, 16).toUpper().rightJustified(
                2, QLatin1Char('0'));
        }
    }
    return out;
}

// Endpoint URLs differing only in trailing slashes are the same endpoint - the
// same normalisation both connection pickers apply before they compare. Without
// it a profile stored as "https://host/v1/" could never match the live
// "https://host/v1" and would be unreachable from every endpoint lookup.
QString normalizedEndpoint(const QString &url)
{
    QString u = url.trimmed();
    while (u.endsWith(QLatin1Char('/')))
        u.chop(1);
    return u;
}

bool sameUrl(const QString &a, const QString &b)
{
    return normalizedEndpoint(a).compare(normalizedEndpoint(b),
                                         Qt::CaseInsensitive) == 0;
}

} // namespace

int ProviderProfileStore::maxNameLength()
{
    return kMaxNameLength;
}

ProviderProfileStore::KeyMemoryAction
ProviderProfileStore::keyMemoryActionOnLeave(const QString &fieldKey,
                                             bool userEditedKeyField,
                                             bool fieldKeyCameFromProfile)
{
    // The key on screen is the PROFILE's, untouched since it was poured in: it
    // travels with that profile (AI/api_key/profile:<id>) and must not be
    // written into the provider's own memory. Doing so replaced the key the
    // user keeps for the plain provider entry with a profile's - and on Custom
    // it handed a profile's cloud token to the next ad-hoc endpoint.
    if (fieldKeyCameFromProfile && !userEditedKeyField)
        return KeyMemoryAction::Keep;
    if (!fieldKey.trimmed().isEmpty())
        return KeyMemoryAction::Store;
    // An empty field is only a deliberate clear when the user typed it away
    // here; poured in by a keyless profile it must leave the memory alone.
    return userEditedKeyField ? KeyMemoryAction::Erase : KeyMemoryAction::Keep;
}

QString ProviderProfileStore::keyMemoryOnEnter(const QString &provider,
                                               const QString &rememberedKey,
                                               bool endpointNeedsKey)
{
    if (provider.trimmed().compare(QLatin1String(kCustomProvider),
                                   Qt::CaseInsensitive) == 0
        && !endpointNeedsKey) {
        return QString();
    }
    return rememberedKey;
}

bool ProviderProfileStore::shouldOfferAdHocCustomEntry(
    const QString &provider, const QString &baseUrl,
    const QString &matchedProfileName, const QStringList &builtInBaseUrls)
{
    // Mandatory half of the rule: whatever the app is actually doing must be on
    // the list. While the connection is ad-hoc custom - provider "custom" and no
    // stored profile describing this endpoint - "Custom" IS the active entry,
    // and hiding it would leave the picker naming a connection nobody is using.
    if (provider.trimmed().compare(QLatin1String(kCustomProvider),
                                   Qt::CaseInsensitive) == 0) {
        return matchedProfileName.isEmpty();
    }

    // A live endpoint that IS a saved profile is represented by that profile's
    // own entry. Offering "Custom" beside it would be a decoy: picking it keeps
    // the very same URL and key, so the selection snaps back to the profile
    // while the model list is rebuilt underneath.
    if (!matchedProfileName.isEmpty())
        return false;

    // Otherwise offer it only when a custom endpoint is really configured: a
    // base URL that is merely a built-in provider's default is what switching
    // provider writes, not something anyone typed. A non-default URL left
    // behind by a built-in provider (Ollama on another host, say) does count -
    // picking "Custom" would talk to exactly that URL.
    const QString url = normalizedEndpoint(baseUrl);
    if (url.isEmpty())
        return false;
    for (const QString &d : builtInBaseUrls) {
        if (url.compare(normalizedEndpoint(d), Qt::CaseInsensitive) == 0)
            return false;
    }
    return true;
}

bool ProviderProfileStore::selectionNeedsFixedCustomEntry(
    const QString &provider, const QString &selectedProfile)
{
    if (provider.trimmed().compare(QLatin1String(kCustomProvider),
                                   Qt::CaseInsensitive) != 0) {
        return false;  // the other four entries are unconditional everywhere
    }
    // Provider(custom): the fixed entry IS the selection. Deliberately no
    // endpoint lookup - whether a stored profile happens to describe this URL
    // says nothing about what the user picked.
    return normalizeName(selectedProfile).isEmpty();
}

QString ProviderProfileStore::normalizeName(const QString &raw)
{
    // simplified() drops leading/trailing whitespace and collapses inner runs
    // (incl. tabs and newlines pasted from somewhere) into single spaces.
    QString n = raw.simplified();
    if (n.size() > kMaxNameLength)
        n = n.left(kMaxNameLength).trimmed();
    return n;
}

QString ProviderProfileStore::encodeName(const QString &name)
{
    const QString normalized = normalizeName(name);
    const QString full = percentEncode(normalized);
    if (full.size() <= kMaxEncodedIdLength)
        return full;

    // Too long for the settings backend (see kMaxEncodedIdLength). Keep a
    // readable head - encoded from whole CHARACTERS, so it can never end in a
    // half-written "%E4" escape - and make the id unique with a digest of the
    // full encoding. Deterministic: the same display name always maps to the
    // same id, so the profile stays findable and its model-list scope stable.
    const QString head = percentEncode(normalized.left(kShortIdHeadChars));
    const QString digest = QString::fromLatin1(
        QCryptographicHash::hash(full.toUtf8(), QCryptographicHash::Sha1)
            .toHex()
            .left(kShortIdDigestChars));
    return head + QLatin1Char(kShortIdSeparator) + digest;
}

int ProviderProfileStore::maxEncodedIdLength()
{
    return kMaxEncodedIdLength;
}

QString ProviderProfileStore::decodeName(const QString &id)
{
    return QString::fromUtf8(
        QByteArray::fromPercentEncoding(id.toUtf8(), '%'));
}

QStringList ProviderProfileStore::profileNames()
{
    auto s = settings();
    s->beginGroup(QString::fromLatin1(kRoot));
    const QStringList ids = s->childGroups();
    s->endGroup();

    QStringList names;
    for (const QString &id : ids) {
        // The display name is stored verbatim; the decoded id is only the
        // fallback for a hand-edited settings file.
        QString name = s->value(groupOf(id, QStringLiteral("name"))).toString();
        if (name.isEmpty())
            name = decodeName(id);
        if (name.isEmpty() || names.contains(name))
            continue;
        names.append(name);
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

bool ProviderProfileStore::exists(const QString &name)
{
    const QString id = encodeName(name);
    if (id.isEmpty())
        return false;
    return settings()->contains(groupOf(id, QStringLiteral("provider")));
}

ProviderProfileStore::Profile ProviderProfileStore::load(const QString &name,
                                                         bool *ok)
{
    Profile p;
    const QString id = encodeName(name);
    auto s = settings();
    if (id.isEmpty() || !s->contains(groupOf(id, QStringLiteral("provider")))) {
        if (ok) *ok = false;
        return p;
    }
    p.name = s->value(groupOf(id, QStringLiteral("name")),
                      normalizeName(name)).toString();
    p.provider = s->value(groupOf(id, QStringLiteral("provider"))).toString();
    p.baseUrl = s->value(groupOf(id, QStringLiteral("base_url"))).toString();
    p.model = s->value(groupOf(id, QStringLiteral("model"))).toString();
    if (ok) *ok = true;
    return p;
}

QString ProviderProfileStore::apiKeyFor(const QString &name)
{
    const QString id = encodeName(name);
    if (id.isEmpty())
        return QString();
    return settings()->value(keyKeyOf(id)).toString();
}

bool ProviderProfileStore::save(const Profile &profile, const QString &apiKey)
{
    const QString name = normalizeName(profile.name);
    const QString id = encodeName(name);
    if (name.isEmpty() || id.isEmpty() || profile.provider.trimmed().isEmpty())
        return false;

    const QString provider = profile.provider.trimmed();
    auto s = settings();
    const bool existedBefore = s->contains(groupOf(id, QStringLiteral("provider")));

    s->setValue(groupOf(id, QStringLiteral("name")), name);
    s->setValue(groupOf(id, QStringLiteral("provider")), provider);
    s->setValue(groupOf(id, QStringLiteral("base_url")),
                profile.baseUrl.trimmed());
    s->setValue(groupOf(id, QStringLiteral("model")), profile.model.trimmed());
    // The key lives beside today's per-provider keys, never in the profile
    // group that a future export/import feature might treat as shareable.
    s->setValue(keyKeyOf(id), apiKey);
    s->sync();

    // Honest reporting: setValue() cannot fail, but the BACKEND can refuse the
    // write (a registry key name has a hard length limit). Read the group back
    // through a fresh handle instead of trusting the call, so no caller ever
    // shows "profile saved" for a profile that is not there.
    if (settings()->value(groupOf(id, QStringLiteral("provider"))).toString()
        == provider) {
        return true;
    }
    // A brand-new profile that did not land must not leave its API key behind.
    // An overwrite is left alone: the previous values are still the truth.
    if (!existedBefore) {
        s->beginGroup(QString::fromLatin1(kRoot) + QLatin1Char('/') + id);
        s->remove(QString());
        s->endGroup();
        s->remove(keyKeyOf(id));
        s->sync();
    }
    return false;
}

bool ProviderProfileStore::remove(const QString &name)
{
    const QString id = encodeName(name);
    if (id.isEmpty())
        return false;
    auto s = settings();
    if (!s->contains(groupOf(id, QStringLiteral("provider"))))
        return false;

    // Resolve the endpoint scope BEFORE the group is gone - it is derived from
    // the stored profile.
    const QString scope = modelScopeIdForProfile(name);

    s->beginGroup(QString::fromLatin1(kRoot) + QLatin1Char('/') + id);
    s->remove(QString());
    s->endGroup();
    s->remove(keyKeyOf(id));

    if (normalizeName(s->value(QString::fromLatin1(kActiveHint)).toString())
        == normalizeName(name))
        s->remove(QString::fromLatin1(kActiveHint));

    // The scope is derived from the NAME, so a profile created later under the
    // same name would inherit this one's favourites and cached model list -
    // a foreign server's models offered as if they were its own. Only a
    // profile's own "custom:profile:<id>" scope is cleared; the provider-wide
    // scope that non-custom profiles share belongs to everyone.
    if (scope.startsWith(QString::fromLatin1(kCustomScopePrefix))) {
        ModelFavorites::setFavorites(scope, QStringList());
        // The profile IS gone - a cache file that could not be rewritten does
        // not make the deletion fail, but it must not pass unnoticed either:
        // the stale entry would be handed to the next profile of that name.
        if (!ModelListCache::forget(scope)) {
            qWarning() << "ProviderProfileStore: deleted profile" << name
                       << "- its cached model list could not be dropped from"
                       << ModelListCache::cacheFilePath();
        }
    }
    return true;
}

bool ProviderProfileStore::apply(const QString &name, QString *error)
{
    bool ok = false;
    const Profile p = load(name, &ok);
    if (!ok) {
        if (error)
            *error = QStringLiteral("No provider profile named \"%1\".")
                         .arg(normalizeName(name));
        return false;
    }

    const QString key = apiKeyFor(name);
    auto s = settings();
    s->setValue(QString::fromLatin1(kActiveProvider), p.provider);
    s->setValue(QString::fromLatin1(kActiveBaseUrl), p.baseUrl);
    s->setValue(QString::fromLatin1(kActiveApiKey), key);
    if (!p.model.isEmpty())
        s->setValue(QString::fromLatin1(kActiveModel), p.model);
    // Fill an EMPTY per-provider memory slot so a first-time setup that only
    // ever applied a profile still finds a key on a later provider switch.
    // A remembered key is never overwritten: it is the user's own (typed or
    // committed on the settings page), and the profile's key belongs to the
    // profile - the same rule keyMemoryActionOnLeave() enforces on leave.
    const QString memorySlot = QStringLiteral("AI/api_key/%1").arg(p.provider);
    if (!key.isEmpty() && s->value(memorySlot).toString().isEmpty())
        s->setValue(memorySlot, key);
    s->setValue(QString::fromLatin1(kActiveHint), p.name);
    return true;
}

bool ProviderProfileStore::matchesEndpoint(const QString &name,
                                           const QString &provider,
                                           const QString &baseUrl,
                                           const QString &apiKey)
{
    bool ok = false;
    const Profile p = load(name, &ok);
    if (!ok)
        return false;
    if (p.provider.compare(provider.trimmed(), Qt::CaseInsensitive) != 0)
        return false;
    if (!sameUrl(p.baseUrl, baseUrl))
        return false;
    return apiKeyFor(name) == apiKey;
}

bool ProviderProfileStore::matches(const QString &name, const QString &provider,
                                   const QString &baseUrl, const QString &model,
                                   const QString &apiKey)
{
    if (!matchesEndpoint(name, provider, baseUrl, apiKey))
        return false;
    bool ok = false;
    const Profile p = load(name, &ok);
    // A profile without a model pins only the endpoint.
    return ok && (p.model.isEmpty() || p.model == model.trimmed());
}

QString ProviderProfileStore::nameMatching(const QString &provider,
                                           const QString &baseUrl,
                                           const QString &model,
                                           const QString &apiKey)
{
    if (provider.trimmed().isEmpty())
        return QString();

    // The hint is the common case (a profile was applied and nothing changed).
    const QString hint = settings()
                             ->value(QString::fromLatin1(kActiveHint))
                             .toString();
    if (!hint.isEmpty() && matches(hint, provider, baseUrl, model, apiKey))
        return normalizeName(hint);

    const QStringList all = profileNames();
    for (const QString &n : all) {
        if (matches(n, provider, baseUrl, model, apiKey))
            return n;
    }
    return QString();
}

QString ProviderProfileStore::nameMatchingEndpoint(const QString &provider,
                                                   const QString &baseUrl,
                                                   const QString &apiKey,
                                                   const QString &preferredName)
{
    if (provider.trimmed().isEmpty())
        return QString();

    // The caller's live selection outranks the stored hint: when two profiles
    // describe one endpoint, the hint may still name the other one, and the
    // two pickers would then show different names for the same connection.
    if (!preferredName.isEmpty()
        && matchesEndpoint(preferredName, provider, baseUrl, apiKey)) {
        return normalizeName(preferredName);
    }

    // Same order as nameMatching(): the hint first, so two profiles sharing an
    // endpoint resolve to the one that was actually applied.
    const QString hint = settings()
                             ->value(QString::fromLatin1(kActiveHint))
                             .toString();
    if (!hint.isEmpty() && matchesEndpoint(hint, provider, baseUrl, apiKey))
        return normalizeName(hint);

    const QStringList all = profileNames();
    for (const QString &n : all) {
        if (matchesEndpoint(n, provider, baseUrl, apiKey))
            return n;
    }
    return QString();
}

QString ProviderProfileStore::modelScopeId(const QString &provider,
                                           const QString &baseUrl,
                                           const QString &apiKey)
{
    const QString prov = provider.trimmed();
    if (prov.compare(QLatin1String(kCustomProvider), Qt::CaseInsensitive) != 0)
        return prov;

    const QString name = nameMatchingEndpoint(prov, baseUrl, apiKey);
    if (name.isEmpty())
        return QString::fromLatin1(kCustomProvider);  // ad-hoc: pre-profile scope
    const QString id = encodeName(name);
    if (id.isEmpty())
        return QString::fromLatin1(kCustomProvider);
    return QString::fromLatin1(kCustomScopePrefix) + id;
}

QString ProviderProfileStore::activeModelScopeId()
{
    auto s = settings();
    return modelScopeId(s->value(QString::fromLatin1(kActiveProvider)).toString(),
                        s->value(QString::fromLatin1(kActiveBaseUrl)).toString(),
                        s->value(QString::fromLatin1(kActiveApiKey)).toString());
}

QString ProviderProfileStore::modelScopeIdForProfile(const QString &name)
{
    bool ok = false;
    const Profile p = load(name, &ok);
    if (!ok)
        return QString();
    if (p.provider.compare(QLatin1String(kCustomProvider), Qt::CaseInsensitive) != 0)
        return p.provider;  // shares the provider-wide scope by design
    const QString id = encodeName(p.name);
    if (id.isEmpty())
        return QString();
    return QString::fromLatin1(kCustomScopePrefix) + id;
}

QString ProviderProfileStore::activeProfileName()
{
    auto s = settings();
    return nameMatching(s->value(QString::fromLatin1(kActiveProvider)).toString(),
                        s->value(QString::fromLatin1(kActiveBaseUrl)).toString(),
                        s->value(QString::fromLatin1(kActiveModel)).toString(),
                        s->value(QString::fromLatin1(kActiveApiKey)).toString());
}

QString ProviderProfileStore::validatedActiveProfileName(const QString &provider,
                                                         const QString &baseUrl,
                                                         const QString &apiKey)
{
    const QString hint = normalizeName(
        settings()->value(QString::fromLatin1(kActiveHint)).toString());
    // Deliberately no fallback scan: a hint that no longer fits means "no
    // profile", which is the honest answer for a connection nobody selected.
    return hintForSelection(hint, provider, baseUrl, apiKey);
}

QString ProviderProfileStore::hintForSelection(const QString &selectedProfile,
                                               const QString &provider,
                                               const QString &baseUrl,
                                               const QString &apiKey)
{
    const QString name = normalizeName(selectedProfile);
    if (name.isEmpty())
        return QString();
    // ENDPOINT identity, not the full quadruple: picking another model keeps
    // you on the same server, so the profile selection survives it (only the
    // model-sensitive "Provider profile" row falls back to "(No profile)").
    return matchesEndpoint(name, provider, baseUrl, apiKey) ? name : QString();
}

void ProviderProfileStore::setActiveProfileHint(const QString &name)
{
    const QString n = normalizeName(name);
    auto s = settings();
    if (n.isEmpty())
        s->remove(QString::fromLatin1(kActiveHint));
    else
        s->setValue(QString::fromLatin1(kActiveHint), n);
}
