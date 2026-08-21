#include "ProviderProfileStore.h"

#include "../AppPaths.h"

#include <QByteArray>
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

bool sameUrl(const QString &a, const QString &b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace

int ProviderProfileStore::maxNameLength()
{
    return kMaxNameLength;
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
    const QByteArray utf8 = normalizeName(name).toUtf8();
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

    auto s = settings();
    s->setValue(groupOf(id, QStringLiteral("name")), name);
    s->setValue(groupOf(id, QStringLiteral("provider")),
                profile.provider.trimmed());
    s->setValue(groupOf(id, QStringLiteral("base_url")),
                profile.baseUrl.trimmed());
    s->setValue(groupOf(id, QStringLiteral("model")), profile.model.trimmed());
    // The key lives beside today's per-provider keys, never in the profile
    // group that a future export/import feature might treat as shareable.
    s->setValue(keyKeyOf(id), apiKey);
    return true;
}

bool ProviderProfileStore::remove(const QString &name)
{
    const QString id = encodeName(name);
    if (id.isEmpty())
        return false;
    auto s = settings();
    if (!s->contains(groupOf(id, QStringLiteral("provider"))))
        return false;

    s->beginGroup(QString::fromLatin1(kRoot) + QLatin1Char('/') + id);
    s->remove(QString());
    s->endGroup();
    s->remove(keyKeyOf(id));

    if (normalizeName(s->value(QString::fromLatin1(kActiveHint)).toString())
        == normalizeName(name))
        s->remove(QString::fromLatin1(kActiveHint));
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
    // Keep the per-provider memory consistent, but never wipe a remembered
    // key with the empty key of a local (Ollama / llama.cpp) profile.
    if (!key.isEmpty())
        s->setValue(QStringLiteral("AI/api_key/%1").arg(p.provider), key);
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
                                                   const QString &apiKey)
{
    if (provider.trimmed().isEmpty())
        return QString();

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

void ProviderProfileStore::setActiveProfileHint(const QString &name)
{
    const QString n = normalizeName(name);
    auto s = settings();
    if (n.isEmpty())
        s->remove(QString::fromLatin1(kActiveHint));
    else
        s->setValue(QString::fromLatin1(kActiveHint), n);
}
