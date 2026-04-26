// Pure unit test for PromptProfileStore: profile CRUD, glob matching,
// resolution layering, persistence round-trip, and built-in seeding.
//
// QSettings sandboxing: PromptProfileStore uses the hard-coded
// QSettings("MidiEditor","NONE"). Two strategies are used here to keep the
// test from polluting the user's real registry / Application Support:
//
//  1. QStandardPaths::setTestModeEnabled(true) before the first store is
//     instantiated. This redirects QSettings(NativeFormat) on platforms
//     that honour it and is harmless elsewhere.
//  2. All test-created profile ids are prefixed with a per-process suffix
//     so two parallel runs (Release + Debug) cannot collide. The shipped
//     built-in id "builtin.gpt55_decisive" is wiped in cleanupTestCase to
//     leave the registry clean.
#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTest>
#include <QUuid>

#include "../src/ai/PromptProfile.h"
#include "../src/ai/PromptProfileStore.h"

namespace {
constexpr const char *kBuiltinId = "builtin.gpt55_decisive";
constexpr const char *kRootGroup = "AI/prompt_profiles";
constexpr const char *kBuiltinVersionKey = "AI/prompt_profiles/builtins_version";

void wipeAllProfilesFromSettings()
{
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.beginGroup(QString::fromLatin1(kRootGroup));
    s.remove(QString()); // clear the whole group
    s.endGroup();
    s.sync();
}
} // namespace

class TestPromptProfiles : public QObject {
    Q_OBJECT

private:
    QString _suffix;

    QString tag(const QString &base) const
    {
        return base + QStringLiteral("__test_") + _suffix;
    }

    PromptProfile makeProfile(const QString &nameTag,
                              const QStringList &models,
                              const QString &body,
                              bool append) const
    {
        PromptProfile p;
        p.id = QStringLiteral("test-") + _suffix + QStringLiteral("-")
               + QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.name = nameTag;
        p.system = body;
        p.appendToDefault = append;
        p.builtin = false;
        p.enabled = true;
        p.models = models;
        return p;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("MidiEditor"));
        QCoreApplication::setApplicationName(QStringLiteral("NONE"));
        _suffix = QString::number(QCoreApplication::applicationPid());
        wipeAllProfilesFromSettings();
    }

    void cleanupTestCase()
    {
        wipeAllProfilesFromSettings();
    }

    void cleanup()
    {
        // Each test creates a fresh sandbox.
        wipeAllProfilesFromSettings();
    }

    // ----------------------------------------------------------- patterns --
    void patternMatches_basic()
    {
        QVERIFY(PromptProfileStore::patternMatches(
            QStringLiteral("openai:gpt-5.5"),
            QStringLiteral("openai:gpt-5.5")));
        QVERIFY(!PromptProfileStore::patternMatches(
            QStringLiteral("openai:gpt-5.5"),
            QStringLiteral("openai:gpt-5.4")));
        QVERIFY(PromptProfileStore::patternMatches(
            QStringLiteral("openai:GPT-5.5"),
            QStringLiteral("openai:gpt-5.5")) /*case-insensitive*/);
    }

    void patternMatches_globSuffix()
    {
        QVERIFY(PromptProfileStore::patternMatches(
            QStringLiteral("openai:gpt-5.5*"),
            QStringLiteral("openai:gpt-5.5-pro-2026-04-23")));
        QVERIFY(PromptProfileStore::patternMatches(
            QStringLiteral("openrouter:openai/gpt-5.5*"),
            QStringLiteral("openrouter:openai/gpt-5.5-mini")));
        QVERIFY(!PromptProfileStore::patternMatches(
            QStringLiteral("openai:gpt-5.5*"),
            QStringLiteral("openai:gpt-5.4-mini")));
    }

    // ------------------------------------------------------------ resolve --
    void resolve_returnsDefaultWhenNoProfileMatches()
    {
        PromptProfileStore store;
        // Wipe everything (including the just-seeded built-in) so we are
        // guaranteed to hit the no-match branch.
        wipeAllProfilesFromSettings();

        const QString got = store.resolvePromptForModel(
            QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
            QStringLiteral("DEFAULT"), QString());
        QCOMPARE(got, QStringLiteral("DEFAULT"));
    }

    void resolve_returnsProfileBodyWhenMatching_replaceMode()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;

        PromptProfile p = makeProfile(
            tag(QStringLiteral("Replace")),
            {QStringLiteral("openai:gpt-4o*")},
            QStringLiteral("BODY-ONLY"),
            /*append=*/false);
        store.upsert(p);

        const QString got = store.resolvePromptForModel(
            QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
            QStringLiteral("DEFAULT"), QString());
        QCOMPARE(got, QStringLiteral("BODY-ONLY"));
    }

    void resolve_appendsToDefaultWhenAppendFlagSet()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;

        PromptProfile p = makeProfile(
            tag(QStringLiteral("Append")),
            {QStringLiteral("openai:gpt-4o*")},
            QStringLiteral("EXTRA-RULES"),
            /*append=*/true);
        store.upsert(p);

        const QString got = store.resolvePromptForModel(
            QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
            QStringLiteral("DEFAULT"), QString());
        QCOMPARE(got, QStringLiteral("DEFAULT\n\nEXTRA-RULES"));
    }

    void resolve_globMatch_gpt55StarMatchesGpt55ProDated()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;
        store.ensureBuiltinsSeeded(/*force=*/true);

        const PromptProfile resolved = store.resolveForModel(
            QStringLiteral("openai"),
            QStringLiteral("gpt-5.5-pro-2026-04-23"));
        QCOMPARE(resolved.id, QString::fromLatin1(kBuiltinId));
        QVERIFY(resolved.appendToDefault);
        QVERIFY(resolved.builtin);
    }

    void resolve_disabledProfileIsIgnored()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;

        PromptProfile p = makeProfile(
            tag(QStringLiteral("Off")),
            {QStringLiteral("openai:gpt-4o*")},
            QStringLiteral("UNUSED"),
            /*append=*/false);
        p.enabled = false;
        store.upsert(p);

        const QString got = store.resolvePromptForModel(
            QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
            QStringLiteral("DEFAULT"), QString());
        QCOMPARE(got, QStringLiteral("DEFAULT"));
    }

    void resolve_userCustomTakesPrecedenceOverDefault_butNotOverProfile()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;

        // No profile for this model: userCustom wins over default.
        QCOMPARE(store.resolvePromptForModel(
                     QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
                     QStringLiteral("DEFAULT"), QStringLiteral("USER")),
                 QStringLiteral("USER"));

        // Replace-mode profile beats userCustom outright.
        PromptProfile p = makeProfile(
            tag(QStringLiteral("Replace2")),
            {QStringLiteral("openai:gpt-4o*")},
            QStringLiteral("BODY"),
            /*append=*/false);
        store.upsert(p);
        QCOMPARE(store.resolvePromptForModel(
                     QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
                     QStringLiteral("DEFAULT"), QStringLiteral("USER")),
                 QStringLiteral("BODY"));

        // Append-mode profile uses userCustom as the base if non-empty.
        wipeAllProfilesFromSettings();
        PromptProfileStore store2;
        PromptProfile p2 = makeProfile(
            tag(QStringLiteral("Append2")),
            {QStringLiteral("openai:gpt-4o*")},
            QStringLiteral("ADD"),
            /*append=*/true);
        store2.upsert(p2);
        QCOMPARE(store2.resolvePromptForModel(
                     QStringLiteral("openai"), QStringLiteral("gpt-4o-mini"),
                     QStringLiteral("DEFAULT"), QStringLiteral("USER")),
                 QStringLiteral("USER\n\nADD"));
    }

    // -------------------------------------------------------- persistence --
    void persist_roundTripsAcrossStoreInstances()
    {
        wipeAllProfilesFromSettings();
        const QString sysText =
            QStringLiteral("Hello\nProfile\nWith newlines & specials: %!@#");
        QString id;
        {
            PromptProfileStore store;
            PromptProfile p = makeProfile(
                tag(QStringLiteral("Roundtrip")),
                {QStringLiteral("openrouter:openai/gpt-5.5*"),
                 QStringLiteral("openai:gpt-5.5-pro")},
                sysText,
                /*append=*/true);
            id = p.id;
            store.upsert(p);
        }

        PromptProfileStore store2;
        const QList<PromptProfile> all = store2.profiles();
        const PromptProfile *found = nullptr;
        for (const PromptProfile &x : all) {
            if (x.id == id) { found = &x; break; }
        }
        QVERIFY(found != nullptr);
        QCOMPARE(found->system, sysText);
        QCOMPARE(found->models.size(), 2);
        QVERIFY(found->appendToDefault);
        QVERIFY(found->enabled);
        QVERIFY(!found->builtin);
    }

    // ----------------------------------------------------------- built-in --
    void builtin_isSeededOnFirstConstruction()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;
        const QList<PromptProfile> all = store.profiles();
        bool found = false;
        for (const PromptProfile &p : all) {
            if (p.id == QString::fromLatin1(kBuiltinId)) {
                QVERIFY(p.builtin);
                QVERIFY(p.appendToDefault);
                QVERIFY(p.enabled);
                QVERIFY(p.models.contains(QStringLiteral("openai:gpt-5.5*")));
                QVERIFY(p.models.contains(QStringLiteral("openrouter:openai/gpt-5.5*")));
                found = true;
                break;
            }
        }
        QVERIFY2(found, "GPT-5.5 Decisive built-in not seeded");
    }

    void builtin_cannotBeDeleted()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;
        const bool removed = store.remove(QString::fromLatin1(kBuiltinId));
        QVERIFY2(!removed, "remove() on built-in must return false");
        QVERIFY(!store.resolveForModel(QStringLiteral("openai"),
                                       QStringLiteral("gpt-5.5-pro")).id.isEmpty());
    }

    void builtin_bodyAndModelsAreImmutable()
    {
        wipeAllProfilesFromSettings();
        PromptProfileStore store;

        PromptProfile evil;
        evil.id = QString::fromLatin1(kBuiltinId);
        evil.name = QStringLiteral("Hijacked");
        evil.system = QStringLiteral("HIJACK");
        evil.appendToDefault = false;
        evil.builtin = false;
        evil.enabled = false;
        evil.models = {QStringLiteral("anthropic:claude-3.5*")};
        store.upsert(evil);

        // Body, name, models, append, builtin all preserved. Only `enabled`
        // is honoured (so the user can disable a built-in).
        const QList<PromptProfile> all = store.profiles();
        const PromptProfile *found = nullptr;
        for (const PromptProfile &p : all) {
            if (p.id == QString::fromLatin1(kBuiltinId)) {
                found = &p; break;
            }
        }
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("GPT-5.5 Decisive"));
        QVERIFY(found->system.contains(QStringLiteral("MIDI TOOL MODE")));
        QVERIFY(found->appendToDefault);
        QVERIFY(found->builtin);
        QVERIFY(!found->enabled);
        QVERIFY(found->models.contains(QStringLiteral("openai:gpt-5.5*")));
    }

    void builtin_versionRefreshesBodyButPreservesEnabledState()
    {
        wipeAllProfilesFromSettings();

        QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
        const QString root = QStringLiteral("AI/prompt_profiles/") + QString::fromLatin1(kBuiltinId);
        s.setValue(root + QStringLiteral("/name"), QStringLiteral("Old GPT-5.5"));
        s.setValue(root + QStringLiteral("/system"), QStringLiteral("OLD BODY"));
        s.setValue(root + QStringLiteral("/append_to_default"), false);
        s.setValue(root + QStringLiteral("/builtin"), true);
        s.setValue(root + QStringLiteral("/enabled"), false);
        s.setValue(root + QStringLiteral("/models"), QStringList{QStringLiteral("openai:old")});
        s.setValue(QStringLiteral("AI/prompt_profiles/order"), QStringList{QString::fromLatin1(kBuiltinId)});
        s.setValue(QStringLiteral("AI/prompt_profiles/builtins_seeded"), true);
        s.remove(QString::fromLatin1(kBuiltinVersionKey));
        s.sync();

        PromptProfileStore refreshed;
        const QList<PromptProfile> all = refreshed.profiles();
        const PromptProfile *found = nullptr;
        for (const PromptProfile &p : all) {
            if (p.id == QString::fromLatin1(kBuiltinId)) {
                found = &p; break;
            }
        }
        QVERIFY(found != nullptr);
        QCOMPARE(found->name, QStringLiteral("GPT-5.5 Decisive"));
        QVERIFY(found->system.contains(QStringLiteral("channel\":null")));
        QVERIFY(found->appendToDefault);
        QVERIFY(found->builtin);
        QVERIFY(!found->enabled);
        QVERIFY(found->models.contains(QStringLiteral("openai:gpt-5.5*")));
    }
};

QTEST_MAIN(TestPromptProfiles)
#include "test_prompt_profiles.moc"
