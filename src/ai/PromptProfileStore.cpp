#include "PromptProfileStore.h"

#include <QSettings>
#include <QStringList>
#include <QUuid>

namespace {
constexpr const char *kRoot          = "AI/prompt_profiles/";
constexpr const char *kOrderKey      = "AI/prompt_profiles/order";
constexpr const char *kBuiltinSeeded = "AI/prompt_profiles/builtins_seeded";
constexpr const char *kBuiltinVersion = "AI/prompt_profiles/builtins_version";
constexpr const char *kCurrentBuiltinVersion = "2026-04-25.gpt55-toolargs-v3";
constexpr const char *kBuiltinIdGpt55 = "builtin.gpt55_decisive";

QSettings *settings()
{
    static QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    return &s;
}

QString gpt55DecisiveBody()
{
    return QStringLiteral(
    "## GPT-5.5 MIDI TOOL MODE\n"
    "Goal: complete the user's MIDI edit in the fewest useful tool loops.\n"
    "Success criteria:\n"
    "- For melody/composition requests, the first write tool call contains\n"
    "  a program_change at tick 0 (unless drums) and at least one note.\n"
    "- Use this exact note shape: {\"type\":\"note\",\"tick\":0,\"note\":60,\n"
    "  \"velocity\":80,\"duration\":192,\"channel\":null}. Use channel:null\n"
    "  unless you intentionally need a per-note channel override.\n"
    "- Do not use pitch_bend as a placeholder. Use pitch_bend only when\n"
    "  the user explicitly asks for bends, vibrato, or pitch automation.\n"
    "- NEVER call insert_events or replace_events with an `events` array\n"
    "  that contains only pitch_bend items. Such calls are auto-rejected.\n"
    "  If a track already contains a stray pitch_bend you do not need,\n"
    "  simply omit it from your replace_events array \u2014 do NOT copy it\n"
    "  into the new events list as a placeholder.\n"
    "- After a successful write tool result, stop if the requested music now\n"
    "  exists. Do not repeat the same write call or re-create applied data.\n"
    "- If you accidentally wrote only pitch_bend for a melody request, fix it\n"
    "  once with replace_events containing program_change + notes, then stop\n"
    "  after that tool succeeds.\n");
}

PromptProfile makeGpt55Decisive()
{
    PromptProfile p;
    p.id = QString::fromLatin1(kBuiltinIdGpt55);
    p.name = QStringLiteral("GPT-5.5 Decisive");
    p.system = gpt55DecisiveBody();
    p.appendToDefault = true;
    p.builtin = true;
    p.enabled = true;
    p.models = QStringList{
        QStringLiteral("openai:gpt-5.5*"),
        QStringLiteral("openrouter:openai/gpt-5.5*"),
    };
    return p;
}
} // namespace

PromptProfileStore::PromptProfileStore(QObject *parent)
    : QObject(parent)
{
    ensureBuiltinsSeeded(/*force=*/false);
}

QString PromptProfileStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString PromptProfileStore::idKey(const QString &id, const QString &leaf)
{
    return QString::fromLatin1(kRoot) + id + QLatin1Char('/') + leaf;
}

QStringList PromptProfileStore::persistedOrder() const
{
    return settings()->value(QString::fromLatin1(kOrderKey)).toStringList();
}

void PromptProfileStore::persistOrder(const QStringList &ids) const
{
    if (ids.isEmpty())
        settings()->remove(QString::fromLatin1(kOrderKey));
    else
        settings()->setValue(QString::fromLatin1(kOrderKey), ids);
}

bool PromptProfileStore::patternMatches(const QString &pattern,
                                        const QString &providerColonModel)
{
    if (pattern.isEmpty() || providerColonModel.isEmpty())
        return false;
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (pattern.endsWith(QLatin1Char('*'))) {
        QStringView prefix(pattern.constData(), pattern.size() - 1);
        return QStringView(providerColonModel).startsWith(prefix, cs);
    }
    return providerColonModel.compare(pattern, cs) == 0;
}

PromptProfile PromptProfileStore::loadProfile(const QString &id) const
{
    PromptProfile p;
    p.id = id;
    p.name = settings()->value(idKey(id, QStringLiteral("name"))).toString();
    p.system = settings()->value(idKey(id, QStringLiteral("system"))).toString();
    p.appendToDefault =
        settings()->value(idKey(id, QStringLiteral("append_to_default")), true).toBool();
    p.builtin =
        settings()->value(idKey(id, QStringLiteral("builtin")), false).toBool();
    p.enabled =
        settings()->value(idKey(id, QStringLiteral("enabled")), true).toBool();
    p.models =
        settings()->value(idKey(id, QStringLiteral("models"))).toStringList();
    return p;
}

void PromptProfileStore::saveProfile(const PromptProfile &p) const
{
    settings()->setValue(idKey(p.id, QStringLiteral("name")), p.name);
    settings()->setValue(idKey(p.id, QStringLiteral("system")), p.system);
    settings()->setValue(idKey(p.id, QStringLiteral("append_to_default")),
                          p.appendToDefault);
    settings()->setValue(idKey(p.id, QStringLiteral("builtin")), p.builtin);
    settings()->setValue(idKey(p.id, QStringLiteral("enabled")), p.enabled);
    settings()->setValue(idKey(p.id, QStringLiteral("models")), p.models);
}

void PromptProfileStore::ensureBuiltinsSeeded(bool force)
{
    QSettings *s = settings();
    const bool seeded = s->value(QString::fromLatin1(kBuiltinSeeded), false).toBool();
    const QString version = s->value(QString::fromLatin1(kBuiltinVersion)).toString();
    if (!force && seeded && version == QString::fromLatin1(kCurrentBuiltinVersion))
        return;

    const PromptProfile bi = makeGpt55Decisive();

    // Preserve user's enabled state if the built-in already exists.
    bool existedEnabled = s->value(idKey(bi.id, QStringLiteral("enabled")), true).toBool();
    PromptProfile p = bi;
    p.enabled = existedEnabled;
    saveProfile(p);

    QStringList order = persistedOrder();
    if (!order.contains(bi.id))
        order.prepend(bi.id);
    persistOrder(order);

    s->setValue(QString::fromLatin1(kBuiltinSeeded), true);
    s->setValue(QString::fromLatin1(kBuiltinVersion), QString::fromLatin1(kCurrentBuiltinVersion));
}

QList<PromptProfile> PromptProfileStore::profiles() const
{
    QStringList order = persistedOrder();

    // Discover any ids that have at least a name key but aren't in order
    // (e.g. legacy / hand-written entries). Walk the prefix keys.
    QSettings *s = settings();
    s->beginGroup(QStringLiteral("AI/prompt_profiles"));
    const QStringList groups = s->childGroups();
    s->endGroup();
    for (const QString &id : groups) {
        if (!order.contains(id))
            order.append(id);
    }

    QList<PromptProfile> out;
    for (const QString &id : order) {
        PromptProfile p = loadProfile(id);
        if (p.id.isEmpty() || (p.name.isEmpty() && p.system.isEmpty()))
            continue;
        out.append(p);
    }
    return out;
}

PromptProfile PromptProfileStore::resolveForModel(const QString &provider,
                                                  const QString &model) const
{
    if (provider.isEmpty() || model.isEmpty())
        return PromptProfile{};
    const QString key = provider + QLatin1Char(':') + model;
    const QList<PromptProfile> all = profiles();
    for (const PromptProfile &p : all) {
        if (!p.enabled)
            continue;
        for (const QString &pat : p.models) {
            if (patternMatches(pat, key))
                return p;
        }
    }
    return PromptProfile{};
}

QString PromptProfileStore::resolvePromptForModel(const QString &provider,
                                                  const QString &model,
                                                  const QString &defaultPrompt,
                                                  const QString &userCustom) const
{
    const PromptProfile p = resolveForModel(provider, model);
    if (!p.id.isEmpty()) {
        if (p.appendToDefault) {
            // Prefer the user's customised "default" if they wrote one,
            // since that's what the editor would otherwise feed in.
            const QString base = !userCustom.trimmed().isEmpty()
                                     ? userCustom
                                     : defaultPrompt;
            return base + QStringLiteral("\n\n") + p.system;
        }
        return p.system;
    }
    if (!userCustom.trimmed().isEmpty())
        return userCustom;
    return defaultPrompt;
}

void PromptProfileStore::upsert(const PromptProfile &p)
{
    if (p.id.isEmpty())
        return;

    PromptProfile toWrite = p;
    PromptProfile existing = loadProfile(p.id);
    if (existing.builtin) {
        // Built-ins are read-only except for the enabled flag and binding
        // edits the user explicitly opts into via Duplicate. Keep body and
        // builtin marker intact.
        toWrite.builtin = true;
        toWrite.name = existing.name;
        toWrite.system = existing.system;
        toWrite.appendToDefault = existing.appendToDefault;
        toWrite.models = existing.models;
    }
    saveProfile(toWrite);

    QStringList order = persistedOrder();
    if (!order.contains(p.id)) {
        order.append(p.id);
        persistOrder(order);
    }
}

bool PromptProfileStore::remove(const QString &id)
{
    if (id.isEmpty())
        return false;
    PromptProfile existing = loadProfile(id);
    if (existing.builtin)
        return false;

    QSettings *s = settings();
    s->beginGroup(QString::fromLatin1(kRoot) + id);
    s->remove(QString());
    s->endGroup();

    QStringList order = persistedOrder();
    order.removeAll(id);
    persistOrder(order);
    return true;
}

void PromptProfileStore::setOrder(const QStringList &orderedIds)
{
    QStringList valid;
    QSettings *s = settings();
    s->beginGroup(QStringLiteral("AI/prompt_profiles"));
    const QStringList groups = s->childGroups();
    s->endGroup();
    for (const QString &id : orderedIds) {
        if (groups.contains(id))
            valid.append(id);
    }
    persistOrder(valid);
}
