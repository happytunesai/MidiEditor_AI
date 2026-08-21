#include "ProviderProfileStore.h"

// Skeleton for Phase 50 - the implementation lands with the v2.3 sprint.

QStringList ProviderProfileStore::profileNames()
{
    return {};
}

ProviderProfileStore::Profile ProviderProfileStore::load(const QString &name, bool *ok)
{
    Q_UNUSED(name);
    if (ok) *ok = false;
    return {};
}

bool ProviderProfileStore::save(const Profile &profile, const QString &apiKey)
{
    Q_UNUSED(profile);
    Q_UNUSED(apiKey);
    return false;
}

bool ProviderProfileStore::remove(const QString &name)
{
    Q_UNUSED(name);
    return false;
}

bool ProviderProfileStore::apply(const QString &name, QString *error)
{
    Q_UNUSED(name);
    if (error) *error = QStringLiteral("Not implemented yet.");
    return false;
}
