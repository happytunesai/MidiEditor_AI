/*
 * test_tool_definitions
 *
 * Schema-validity tests for src/ai/ToolDefinitions::toolSchemas() — the
 * OpenAI-format function-calling tool array exposed to the Agent loop.
 *
 * Scope
 * -----
 * Pure JSON shape only — no dispatch, no widget. The non-schema entry
 * points (executeTool, exec*) reference MidiFile / MidiPilotWidget /
 * MidiEventSerializer / NoteOnEvent etc., but they are unreferenced from
 * toolSchemas(); the Release build's /Gy + /OPT:REF strips them at link
 * time.
 *
 * FFXIV mode is read once from QSettings("MidiEditor", "NONE"). We use
 * QStandardPaths::setTestModeEnabled(true) so the developer's real
 * settings are never modified, and we explicitly toggle and restore the
 * AI/ffxiv_mode key inside the relevant tests.
 */

#include <QtTest/QtTest>
#include <QObject>
#include <QImage>
#include <QList>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <functional>

// ---- ODR shims ------------------------------------------------------------
// ToolDefinitions.cpp drags in a wide surface (MidiFile/Track/Channel/Event,
// FFXIVChannelFixer, MidiEventSerializer, EditorContext, MidiPilotWidget,
// Protocol). /OPT:REF does not strip those references in this project's
// link config (verified — same root cause as the MidiEventSerializer
// blocker described in Planning/06_TEST_CASES.md §2.6). We therefore
// out-of-line stub every external symbol toolSchemas() never calls. Return
// types are irrelevant for MSVC name mangling (only class + method name +
// parameter list matter), so we use minimal placeholders.

class MidiFile;
class MidiTrack;
class MidiChannel;
class MidiEvent;
class OffEvent;
class MatrixWidget;
class Protocol;
class MidiPilotWidget;

class FFXIVChannelFixer {
public:
    static QJsonObject fixChannels(MidiFile *file, int mode,
                                   std::function<void(int, QString const &)> cb);
};
QJsonObject FFXIVChannelFixer::fixChannels(MidiFile *, int, std::function<void(int, QString const &)>) { return QJsonObject(); }

class MidiFile {
public:
    MidiTrack *track(int);
    int numTracks();
    MidiChannel *channel(int);
    Protocol *protocol();
    QList<MidiEvent *> *eventsBetween(int, int);
};
MidiTrack *MidiFile::track(int) { return nullptr; }
int MidiFile::numTracks() { return 0; }
MidiChannel *MidiFile::channel(int) { return nullptr; }
Protocol *MidiFile::protocol() { return nullptr; }
QList<MidiEvent *> *MidiFile::eventsBetween(int, int) { return nullptr; }

class MidiTrack {
public:
    int assignedChannel();
    QString name();
};
int MidiTrack::assignedChannel() { return -1; }
QString MidiTrack::name() { return QString(); }

class MidiChannel {
public:
    QMultiMap<int, MidiEvent *> *eventMap();
};
QMultiMap<int, MidiEvent *> *MidiChannel::eventMap() { return nullptr; }

class MidiEvent {
public:
    int midiTime();
    int channel();
    MidiTrack *track();
};
int MidiEvent::midiTime() { return 0; }
int MidiEvent::channel() { return 0; }
MidiTrack *MidiEvent::track() { return nullptr; }

class OnEvent {
public:
    OffEvent *offEvent();
};
OffEvent *OnEvent::offEvent() { return nullptr; }

class NoteOnEvent {
public:
    int velocity();
    int note();
};
int NoteOnEvent::velocity() { return 0; }
int NoteOnEvent::note() { return 0; }

class Protocol {
public:
    void startNewAction(QString, QImage * = nullptr);
    void endAction();
};
void Protocol::startNewAction(QString, QImage *) {}
void Protocol::endAction() {}

class MidiPilotWidget {
public:
    QJsonObject executeAction(QJsonObject const &);
};
QJsonObject MidiPilotWidget::executeAction(QJsonObject const &) { return QJsonObject(); }

class EditorContext {
public:
    static QJsonObject captureState(MidiFile *file, MatrixWidget *matrix = nullptr);
};
QJsonObject EditorContext::captureState(MidiFile *, MatrixWidget *) { return QJsonObject(); }

class MidiEventSerializer {
public:
    static QJsonArray serialize(QList<MidiEvent *> const &events, MidiFile *file);
};
QJsonArray MidiEventSerializer::serialize(QList<MidiEvent *> const &, MidiFile *) { return QJsonArray(); }

#include "../src/ai/ToolDefinitions.h"

namespace {

// Expected tools when FFXIV mode is OFF (default).
const QStringList kCoreToolNames = {
    QStringLiteral("get_editor_state"),
    QStringLiteral("get_track_info"),
    QStringLiteral("query_events"),
    QStringLiteral("create_track"),
    QStringLiteral("rename_track"),
    QStringLiteral("set_channel"),
    QStringLiteral("insert_events"),
    QStringLiteral("replace_events"),
    QStringLiteral("delete_events"),
    QStringLiteral("set_tempo"),
    QStringLiteral("set_time_signature"),
    QStringLiteral("move_events_to_track"),
};

// Extra tools added when FFXIV mode is ON.
const QStringList kFfxivToolNames = {
    QStringLiteral("validate_ffxiv"),
    QStringLiteral("convert_drums_ffxiv"),
    QStringLiteral("setup_channel_pattern"),
};

// Valid JSON Schema "type" values we expect inside parameters.
const QSet<QString> kAllowedParamTypes = {
    QStringLiteral("string"),
    QStringLiteral("integer"),
    QStringLiteral("number"),
    QStringLiteral("boolean"),
    QStringLiteral("array"),
    QStringLiteral("object"),
    QStringLiteral("null"),
};

// Recursive type-string sanity check: any nested object that contains a
// "type" string must use one of the allowed JSON Schema primitives. anyOf
// branches are also walked.
bool hasOnlyValidTypes(const QJsonValue &v) {
    if (v.isObject()) {
        QJsonObject obj = v.toObject();
        if (obj.contains(QStringLiteral("type"))) {
            QJsonValue t = obj.value(QStringLiteral("type"));
            if (t.isString() && !kAllowedParamTypes.contains(t.toString())) {
                return false;
            }
        }
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (!hasOnlyValidTypes(it.value())) return false;
        }
    } else if (v.isArray()) {
        for (const QJsonValue &child : v.toArray()) {
            if (!hasOnlyValidTypes(child)) return false;
        }
    }
    return true;
}

void setFfxivMode(bool on) {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.setValue(QStringLiteral("AI/ffxiv_mode"), on);
    s.sync();
}

void clearFfxivMode() {
    QSettings s(QStringLiteral("MidiEditor"), QStringLiteral("NONE"));
    s.remove(QStringLiteral("AI/ffxiv_mode"));
    s.sync();
}

} // namespace

class TestToolDefinitions : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // Redirect QSettings to a private path so we never modify the
        // developer's real MidiEditor settings on this machine.
        QStandardPaths::setTestModeEnabled(true);
        clearFfxivMode();
    }

    void cleanupTestCase() {
        clearFfxivMode();
    }

    void init() {
        // Default each test to FFXIV-off unless it explicitly opts in.
        clearFfxivMode();
    }

    // -----------------------------------------------------------------
    void toolSchemas_ffxivOff_returnsExactlyTheCoreToolSet() {
        QJsonArray tools = ToolDefinitions::toolSchemas();
        QCOMPARE(tools.size(), kCoreToolNames.size());

        QSet<QString> namesSeen;
        for (const QJsonValue &v : tools) {
            QString n = v.toObject().value(QStringLiteral("function"))
                                    .toObject().value(QStringLiteral("name")).toString();
            namesSeen.insert(n);
        }
        QSet<QString> expected(kCoreToolNames.begin(), kCoreToolNames.end());
        QCOMPARE(namesSeen, expected);
    }

    // -----------------------------------------------------------------
    void toolSchemas_ffxivOn_appendsThreeFfxivTools() {
        setFfxivMode(true);
        QJsonArray tools = ToolDefinitions::toolSchemas();
        QCOMPARE(tools.size(), kCoreToolNames.size() + kFfxivToolNames.size());

        QSet<QString> namesSeen;
        for (const QJsonValue &v : tools) {
            namesSeen.insert(v.toObject().value(QStringLiteral("function"))
                                          .toObject().value(QStringLiteral("name")).toString());
        }
        for (const QString &name : kFfxivToolNames) {
            QVERIFY2(namesSeen.contains(name),
                     qPrintable(QStringLiteral("FFXIV tool missing: %1").arg(name)));
        }
        for (const QString &name : kCoreToolNames) {
            QVERIFY2(namesSeen.contains(name),
                     qPrintable(QStringLiteral("Core tool missing under FFXIV: %1").arg(name)));
        }
    }

    // -----------------------------------------------------------------
    void toolSchemas_everyTool_hasOpenAiFunctionShape() {
        setFfxivMode(true); // exercise the full set
        QJsonArray tools = ToolDefinitions::toolSchemas();

        for (const QJsonValue &v : tools) {
            QJsonObject tool = v.toObject();
            QCOMPARE(tool.value(QStringLiteral("type")).toString(),
                     QStringLiteral("function"));

            QVERIFY(tool.contains(QStringLiteral("function")));
            QJsonObject fn = tool.value(QStringLiteral("function")).toObject();

            QVERIFY(fn.contains(QStringLiteral("name")));
            QVERIFY(fn.contains(QStringLiteral("description")));
            QVERIFY(fn.contains(QStringLiteral("parameters")));
            QVERIFY(fn.contains(QStringLiteral("strict")));

            QVERIFY(!fn.value(QStringLiteral("name")).toString().isEmpty());
            QVERIFY(!fn.value(QStringLiteral("description")).toString().isEmpty());
            QCOMPARE(fn.value(QStringLiteral("strict")).toBool(), true);
        }
    }

    // -----------------------------------------------------------------
    void toolSchemas_everyParameters_isStrictModeObject() {
        setFfxivMode(true);
        QJsonArray tools = ToolDefinitions::toolSchemas();

        for (const QJsonValue &v : tools) {
            QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            QString name = fn.value(QStringLiteral("name")).toString();
            QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();

            QCOMPARE(params.value(QStringLiteral("type")).toString(),
                     QStringLiteral("object"));

            QVERIFY2(params.contains(QStringLiteral("properties")),
                     qPrintable(QStringLiteral("%1 missing properties").arg(name)));
            QVERIFY2(params.contains(QStringLiteral("required")),
                     qPrintable(QStringLiteral("%1 missing required").arg(name)));
            QVERIFY2(params.contains(QStringLiteral("additionalProperties")),
                     qPrintable(QStringLiteral("%1 missing additionalProperties").arg(name)));

            // Strict mode mandates additionalProperties == false.
            QCOMPARE(params.value(QStringLiteral("additionalProperties")).toBool(), false);
        }
    }

    // -----------------------------------------------------------------
    void toolSchemas_everyRequiredKey_existsInProperties() {
        setFfxivMode(true);
        QJsonArray tools = ToolDefinitions::toolSchemas();

        for (const QJsonValue &v : tools) {
            QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            QString name = fn.value(QStringLiteral("name")).toString();
            QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
            QJsonObject props = params.value(QStringLiteral("properties")).toObject();
            QJsonArray required = params.value(QStringLiteral("required")).toArray();

            for (const QJsonValue &r : required) {
                const QString key = r.toString();
                QVERIFY2(props.contains(key),
                         qPrintable(QStringLiteral("Tool %1: required key '%2' missing from properties")
                                    .arg(name, key)));
            }
        }
    }

    // -----------------------------------------------------------------
    void toolSchemas_noDuplicateToolNames() {
        setFfxivMode(true);
        QJsonArray tools = ToolDefinitions::toolSchemas();

        QSet<QString> seen;
        for (const QJsonValue &v : tools) {
            const QString n = v.toObject().value(QStringLiteral("function"))
                                          .toObject().value(QStringLiteral("name")).toString();
            QVERIFY2(!seen.contains(n),
                     qPrintable(QStringLiteral("Duplicate tool name: %1").arg(n)));
            seen.insert(n);
        }
    }

    // -----------------------------------------------------------------
    void toolSchemas_everyParamType_isAValidJsonSchemaType() {
        setFfxivMode(true);
        QJsonArray tools = ToolDefinitions::toolSchemas();

        for (const QJsonValue &v : tools) {
            QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            QString name = fn.value(QStringLiteral("name")).toString();
            QJsonValue params = fn.value(QStringLiteral("parameters"));
            QVERIFY2(hasOnlyValidTypes(params),
                     qPrintable(QStringLiteral("Tool %1 has an invalid JSON Schema type").arg(name)));
        }
    }

    // -----------------------------------------------------------------
    void toolSchemas_writeToolsThatAcceptEvents_useArraySchemaWithItems() {
        setFfxivMode(false);
        QJsonArray tools = ToolDefinitions::toolSchemas();

        const QSet<QString> eventConsumers = {
            QStringLiteral("insert_events"),
            QStringLiteral("replace_events"),
        };

        int verified = 0;
        for (const QJsonValue &v : tools) {
            QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            if (!eventConsumers.contains(name)) continue;

            QJsonObject props = fn.value(QStringLiteral("parameters")).toObject()
                                  .value(QStringLiteral("properties")).toObject();
            QVERIFY2(props.contains(QStringLiteral("events")),
                     qPrintable(QStringLiteral("%1 missing 'events' property").arg(name)));
            QJsonObject events = props.value(QStringLiteral("events")).toObject();
            QCOMPARE(events.value(QStringLiteral("type")).toString(),
                     QStringLiteral("array"));
            QVERIFY2(events.contains(QStringLiteral("items")),
                     qPrintable(QStringLiteral("%1.events missing 'items'").arg(name)));
            QVERIFY(events.value(QStringLiteral("items")).toObject()
                          .contains(QStringLiteral("anyOf")));
            ++verified;
        }
        QCOMPARE(verified, eventConsumers.size());
    }

    // -----------------------------------------------------------------
    void toolSchemas_setTimeSignature_denominatorIsRestrictedEnum() {
        QJsonArray tools = ToolDefinitions::toolSchemas();
        for (const QJsonValue &v : tools) {
            QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            if (fn.value(QStringLiteral("name")).toString() != QStringLiteral("set_time_signature"))
                continue;

            QJsonObject denom = fn.value(QStringLiteral("parameters")).toObject()
                                   .value(QStringLiteral("properties")).toObject()
                                   .value(QStringLiteral("denominator")).toObject();
            QCOMPARE(denom.value(QStringLiteral("type")).toString(),
                     QStringLiteral("integer"));
            QJsonArray allowed = denom.value(QStringLiteral("enum")).toArray();
            // The schema must constrain denominators to musical powers of two.
            QSet<int> values;
            for (const QJsonValue &n : allowed) values.insert(n.toInt());
            const QSet<int> expected = {1, 2, 4, 8, 16, 32};
            QCOMPARE(values, expected);
            return;
        }
        QFAIL("set_time_signature tool not found");
    }
};

QTEST_APPLESS_MAIN(TestToolDefinitions)
#include "test_tool_definitions.moc"
