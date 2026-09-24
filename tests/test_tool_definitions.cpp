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
                                   std::function<void(int, QString const &)> cb,
                                   bool resyncNonGuitar);
};
QJsonObject FFXIVChannelFixer::fixChannels(MidiFile *, int, std::function<void(int, QString const &)>, bool) { return QJsonObject(); }

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
    void setApplyTarget(MidiFile *);  // Phase 28: executeTool's apply-target guard
    void setFfxivMode(bool);          // Phase 46: set_ffxiv_mode tool
};
QJsonObject MidiPilotWidget::executeAction(QJsonObject const &) { return QJsonObject(); }
void MidiPilotWidget::setApplyTarget(MidiFile *) {}
void MidiPilotWidget::setFfxivMode(bool) {}

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

// execGetSelection() (added with the get_selection tool) references these.
// forFile() is used since Phase 28 (per-document selection) so the tool reads the
// run's own document selection rather than the globally-active one.
class Selection {
public:
    static Selection *instance();
    static Selection *forFile(MidiFile *);
    QList<MidiEvent *> selectedEvents();
};
Selection *Selection::instance() { return nullptr; }
Selection *Selection::forFile(MidiFile *) { return nullptr; }
QList<MidiEvent *> Selection::selectedEvents() { return QList<MidiEvent *>(); }

#include "../src/ai/ToolDefinitions.h"
#include "../src/AppPaths.h"

namespace {

// Expected tools when FFXIV mode is OFF (default).
const QStringList kCoreToolNames = {
    QStringLiteral("get_editor_state"),
    QStringLiteral("get_track_info"),
    QStringLiteral("query_events"),
    QStringLiteral("get_selection"),
    QStringLiteral("create_track"),
    QStringLiteral("rename_track"),
    QStringLiteral("set_channel"),
    QStringLiteral("remove_track"),
    QStringLiteral("insert_events"),
    QStringLiteral("replace_events"),
    QStringLiteral("delete_events"),
    QStringLiteral("delete_events_by_index"),
    QStringLiteral("set_tempo"),
    QStringLiteral("set_time_signature"),
    QStringLiteral("move_events_to_track"),
    QStringLiteral("convert_tempo_preserve_duration"), // v2.2 #2: CORE, not FFXIV
    QStringLiteral("thin_tempo_map"),          // v2.3 Phase 49: CORE, not FFXIV
    QStringLiteral("set_ffxiv_mode"), // Phase 46: CORE - reaches the gated bundle
    QStringLiteral("transpose_events"),        // Phase 46 pt 3 (octet #2)
    QStringLiteral("split_chords_to_tracks"),  // Phase 46 pt 3 (octet #2)
    QStringLiteral("copy_events_to_track"),    // Phase 46 pt 3 (octet #2)
    QStringLiteral("search_help"),             // Phase 44 manual bot
    QStringLiteral("get_help_section"),        // Phase 44 manual bot
    // v2.4.0 cross-tab tools. list_documents was MCP-only from v2.0 and is
    // now CORE (the MCP server no longer appends its own copy); the other two
    // are new. switch_document is deliberately NOT here - its definition is
    // gated behind ToolSchemaOptions::includeDocumentSwitch (AgentRunner opts
    // in; the MCP server appends its OWN activate-the-tab variant instead).
    QStringLiteral("list_documents"),
    QStringLiteral("get_document_overview"),
    QStringLiteral("import_tracks_from_document"),
    // v2.5.0 (Phase 51): document and file tools behind the AI gate, and
    // 51.2's exact timing read.
    QStringLiteral("save_document"),
    QStringLiteral("save_document_as"),
    QStringLiteral("new_document"),
    QStringLiteral("open_document"),
    QStringLiteral("close_document"),
    QStringLiteral("get_timing_map"),
};

// The Phase 51 document tools, for the contract slots below.
const QStringList kDocumentFileToolNames = {
    QStringLiteral("save_document"),
    QStringLiteral("save_document_as"),
    QStringLiteral("new_document"),
    QStringLiteral("open_document"),
    QStringLiteral("close_document"),
};

// Extra tools added when FFXIV mode is ON.
const QStringList kFfxivToolNames = {
    QStringLiteral("validate_ffxiv"),
    QStringLiteral("convert_drums_ffxiv"),
    QStringLiteral("setup_channel_pattern"),
    QStringLiteral("analyze_voice_load"),
    QStringLiteral("auto_fit_voice_load"),
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

// Phase 45: through AppPaths::settings(), which initTestCase redirects to a
// throwaway scope. The previous code wrote QSettings("MidiEditor","NONE")
// DIRECTLY - on Windows that is the user's real registry key (test mode
// does not cover the registry), so every run toggled and finally REMOVED
// the developer's actual FFXIV-mode setting. Same defect class the v2.1.0
// round-2 review found in the service tests.
void setFfxivMode(bool on) {
    auto s = AppPaths::settings();
    s->setValue(QStringLiteral("AI/ffxiv_mode"), on);
    s->sync();
}

void clearFfxivMode() {
    auto s = AppPaths::settings();
    s->remove(QStringLiteral("AI/ffxiv_mode"));
    s->sync();
}

} // namespace

class TestToolDefinitions : public QObject {
    Q_OBJECT

private slots:

    void initTestCase() {
        // Redirect BOTH mechanisms away from the developer's real config:
        // QStandardPaths test mode for file-based scopes, and the central
        // AppPaths seam for the explicit scope (= the Windows registry,
        // which test mode does NOT cover).
        QStandardPaths::setTestModeEnabled(true);
        AppPaths::setSettingsScopeForTests(QStringLiteral("MidiEditorTest"),
                                           QStringLiteral("ToolDefinitionsTest"));
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
        // Name kept for history; current FFXIV bundle is 4 tools (validate_ffxiv,
        // convert_drums_ffxiv, setup_channel_pattern, analyze_voice_load).
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

    // -----------------------------------------------------------------
    // NOTE: pre-Phase-31 there were two cases here that asserted
    // executeTool("insert_events"|"replace_events", ...) rejected a
    // pitch_bend-only payload at the dispatch layer. Phase 31 moved that
    // guard into AgentRunner (where it is gated on `gpt-5.5*` only) and
    // ToolDefinitions::executeTool now passes such payloads through to
    // widget->executeAction. The dispatch-layer rejection no longer exists,
    // so those tests were removed. The pitch_bend-only *detection* helper
    // (`isPitchBendOnlyPayload`) is still covered above.

    // -----------------------------------------------------------------
    // Phase 31 — schema-light overload: when ToolSchemaOptions opts out of
    // pitch_bend, the events.anyOf branch in insert_events / replace_events
    // must contain note / cc / program_change but NOT pitch_bend, AND the
    // tool description must not mention "pitch_bend" either.
    void toolSchemas_schemaLight_omitsPitchBendBranch() {
        ToolDefinitions::ToolSchemaOptions opts;
        opts.includePitchBend = false;
        QJsonArray tools = ToolDefinitions::toolSchemas(opts);

        auto findEventsAnyOfTypes = [&](const QString &toolName) -> QStringList {
            QStringList types;
            for (const QJsonValue &v : tools) {
                QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
                if (fn.value(QStringLiteral("name")).toString() != toolName)
                    continue;
                QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
                QJsonObject props = params.value(QStringLiteral("properties")).toObject();
                QJsonObject events = props.value(QStringLiteral("events")).toObject();
                QJsonObject items = events.value(QStringLiteral("items")).toObject();
                QJsonArray anyOf = items.value(QStringLiteral("anyOf")).toArray();
                for (const QJsonValue &branch : anyOf) {
                    QJsonObject bp = branch.toObject().value(QStringLiteral("properties")).toObject();
                    QJsonObject typeProp = bp.value(QStringLiteral("type")).toObject();
                    QJsonArray enumVals = typeProp.value(QStringLiteral("enum")).toArray();
                    for (const QJsonValue &e : enumVals)
                        types << e.toString();
                }
            }
            return types;
        };

        QStringList ins = findEventsAnyOfTypes(QStringLiteral("insert_events"));
        QStringList rep = findEventsAnyOfTypes(QStringLiteral("replace_events"));

        QVERIFY(ins.contains(QStringLiteral("note")));
        QVERIFY(ins.contains(QStringLiteral("cc")));
        QVERIFY(ins.contains(QStringLiteral("program_change")));
        QVERIFY(!ins.contains(QStringLiteral("pitch_bend")));

        QVERIFY(rep.contains(QStringLiteral("note")));
        QVERIFY(!rep.contains(QStringLiteral("pitch_bend")));

        // Description text on the schema-light variant must not advertise
        // pitch_bend either, otherwise the model will still try to emit it.
        for (const QJsonValue &v : tools) {
            QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            if (name != QStringLiteral("insert_events") && name != QStringLiteral("replace_events"))
                continue;
            const QString desc = fn.value(QStringLiteral("description")).toString();
            QVERIFY2(!desc.contains(QStringLiteral("pitch_bend")),
                     qPrintable(QStringLiteral("schema-light %1 description leaks 'pitch_bend': %2")
                                .arg(name, desc)));
        }
    }

    // Default no-arg toolSchemas() must remain fully byte-identical to the
    // includePitchBend=true overload (MCP-server contract).
    void toolSchemas_default_equalsExplicitTrue() {
        QJsonArray a = ToolDefinitions::toolSchemas();
        ToolDefinitions::ToolSchemaOptions opts;
        opts.includePitchBend = true;
        QJsonArray b = ToolDefinitions::toolSchemas(opts);
        QCOMPARE(a, b);
    }

    // -----------------------------------------------------------------
    // Phase 46 follow-up: the FFXIV gate is read on EVERY toolSchemas() call,
    // in both directions. AgentRunner relies on exactly this to re-derive its
    // tool list mid-run after a set_ffxiv_mode call (the panel checkbox and the
    // MCP tools/list_changed broadcast already followed along); if the gate
    // were ever cached, the run that switched the mode would keep the old list
    // to its last step.
    void toolSchemas_ffxivGate_isReReadOnEveryCall() {
        const auto coreCount = ToolDefinitions::toolSchemas().size();
        QCOMPARE(coreCount, kCoreToolNames.size());

        setFfxivMode(true);
        QJsonArray on = ToolDefinitions::toolSchemas();
        QCOMPARE(on.size(), coreCount + kFfxivToolNames.size());

        // ... and back off again, same process, no re-init.
        setFfxivMode(false);
        QJsonArray off = ToolDefinitions::toolSchemas();
        QCOMPARE(off.size(), coreCount);
        QSet<QString> namesSeen;
        for (const QJsonValue &v : off) {
            namesSeen.insert(v.toObject().value(QStringLiteral("function"))
                                          .toObject().value(QStringLiteral("name")).toString());
        }
        for (const QString &name : kFfxivToolNames) {
            QVERIFY2(!namesSeen.contains(name),
                     qPrintable(QStringLiteral("FFXIV tool still listed after "
                                               "disabling the mode: %1").arg(name)));
        }
        // set_ffxiv_mode itself must survive both states — it is the only way
        // back in once the bundle is gone.
        QVERIFY(namesSeen.contains(QStringLiteral("set_ffxiv_mode")));
    }

    // -----------------------------------------------------------------
    // Protocol-panel attribution. These three strings are documented in
    // manual/mcp-server.html and must stay identical to the static
    // protoPrefix() in MidiPilotWidget.cpp — the tools that open their own
    // Protocol action (transpose_events, split_chords_to_tracks,
    // copy_events_to_track, convert_tempo_preserve_duration) build their
    // label from this helper instead of a second, drifting format.
    void protocolActorPrefix_matchesTheDocumentedFormat() {
        QCOMPARE(ToolDefinitions::protocolActorPrefix(QString()),
                 QStringLiteral("MidiPilot"));
        QCOMPARE(ToolDefinitions::protocolActorPrefix(QStringLiteral("mcp")),
                 QStringLiteral("MidiPilotMCP"));
        QCOMPARE(ToolDefinitions::protocolActorPrefix(
                     QStringLiteral("mcp:VS Code Copilot")),
                 QStringLiteral("MidiPilotMCP (VS Code Copilot)"));
        // A trailing colon with no client name degrades to the plain MCP form.
        QCOMPARE(ToolDefinitions::protocolActorPrefix(QStringLiteral("mcp:")),
                 QStringLiteral("MidiPilotMCP"));
        // Anything that is not an MCP source is the built-in panel.
        QCOMPARE(ToolDefinitions::protocolActorPrefix(QStringLiteral("agent")),
                 QStringLiteral("MidiPilot"));
    }

    // -----------------------------------------------------------------
    // v2.3 Phase 49 — thin_tempo_map. A CORE tool (no FFXIV gate: a
    // DAW-exported tempo ramp is a MIDI problem, not a bard one) whose two
    // parameters are BOTH optional, which in strict mode means both are listed
    // in `required` and both carry a null branch. The generic strict-mode test
    // above covers the shape for every tool; this one pins the tool's own
    // contract so a later edit cannot quietly drop the null branches and make
    // "call it with defaults" impossible for the model.
    void thinTempoMap_schemaIsStrictWithBothArgsOptional() {
        const QJsonArray tools = ToolDefinitions::toolSchemas();
        QJsonObject fn;
        for (const QJsonValue &v : tools) {
            const QJsonObject candidate =
                v.toObject().value(QStringLiteral("function")).toObject();
            if (candidate.value(QStringLiteral("name")).toString()
                == QStringLiteral("thin_tempo_map")) {
                fn = candidate;
                break;
            }
        }
        QVERIFY2(!fn.isEmpty(), "thin_tempo_map is not in the CORE tool list");

        // The description has to tell the model WHEN to reach for it.
        const QString description =
            fn.value(QStringLiteral("description")).toString().toLower();
        QVERIFY2(description.contains(QStringLiteral("tempo")), qPrintable(description));
        QVERIFY2(description.contains(QStringLiteral("dryrun=true")),
                 qPrintable(description));

        const QJsonObject params =
            fn.value(QStringLiteral("parameters")).toObject();
        const QJsonObject props =
            params.value(QStringLiteral("properties")).toObject();
        QStringList required;
        for (const QJsonValue &rv : params.value(QStringLiteral("required")).toArray())
            required << rv.toString();
        for (const QString &key : {QStringLiteral("toleranceMs"),
                                   QStringLiteral("dryRun")}) {
            QVERIFY2(props.contains(key), qPrintable(key));
            QVERIFY2(required.contains(key), qPrintable(key));
            bool nullBranch = false;
            for (const QJsonValue &b :
                 props.value(key).toObject().value(QStringLiteral("anyOf")).toArray()) {
                if (b.toObject().value(QStringLiteral("type")).toString()
                    == QStringLiteral("null"))
                    nullBranch = true;
            }
            QVERIFY2(nullBranch,
                     qPrintable(QStringLiteral("%1 has no null branch, so the "
                                               "model cannot omit it").arg(key)));
        }
        QCOMPARE(required.size(), props.size());
    }

    // Both arguments are optional in substance, so neither an explicit null
    // nor a missing key may be rejected as a missing required parameter.
    // (This build stubs the handler out - MidiFile is not linked - so the
    // ERROR TEXT is what proves the call got past argument validation.)
    void thinTempoMap_acceptsNullAndOmittedArgs() {
        QJsonObject nulls;
        nulls[QStringLiteral("toleranceMs")] = QJsonValue::Null;
        nulls[QStringLiteral("dryRun")] = QJsonValue::Null;
        for (const QJsonObject &args : {nulls, QJsonObject{}}) {
            const QJsonObject r = ToolDefinitions::executeTool(
                QStringLiteral("thin_tempo_map"), args, nullptr, nullptr);
            const QString err = r.value(QStringLiteral("error")).toString();
            QVERIFY2(!err.contains(QStringLiteral("missing required")),
                     qPrintable(err));
            QVERIFY2(!err.contains(QStringLiteral("Unknown tool")), qPrintable(err));
        }
    }

    // -----------------------------------------------------------------
    // v2.4.0 cross-tab tools: list_documents / get_document_overview /
    // import_tracks_from_document are CORE; switch_document is opt-in.
    // The generic strict-mode sweep above already covers their shape - the
    // slots below pin each tool's OWN contract so a later edit cannot
    // quietly change it.

    // list_documents was appended by McpServer::convertToolSchemas from v2.0
    // to v2.3.0; the core definition replaced that append, and existing MCP
    // clients must see a byte-identical tool. Full-string compare, not
    // contains(): the description IS the MCP compatibility surface.
    void crossTab_listDocuments_isParameterlessWithVerbatimMcpDescription() {
        const QJsonObject fn = findTool(QStringLiteral("list_documents"));
        QVERIFY2(!fn.isEmpty(), "list_documents is not in the CORE tool list");
        QCOMPARE(fn.value(QStringLiteral("description")).toString(),
                 QStringLiteral(
                     "List all documents (tabs) open in the editor across both "
                     "editor groups: index, title, file path, group (0 = left, "
                     "1 = right), active and modified flags. Use the index with "
                     "switch_document."));
        const QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
        QVERIFY(params.value(QStringLiteral("properties")).toObject().isEmpty());
        QVERIFY(params.value(QStringLiteral("required")).toArray().isEmpty());
    }

    void crossTab_getDocumentOverview_requiresIndexAndPromisesNoRebind() {
        const QJsonObject fn = findTool(QStringLiteral("get_document_overview"));
        QVERIFY2(!fn.isEmpty(), "get_document_overview is not in the CORE tool list");

        const QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
        const QJsonObject props = params.value(QStringLiteral("properties")).toObject();
        QCOMPARE(props.size(), 1);
        // documentIndex is truly mandatory: a plain integer, NO null branch -
        // the required-gate in executeTool must reject its omission.
        QCOMPARE(props.value(QStringLiteral("documentIndex")).toObject()
                     .value(QStringLiteral("type")).toString(),
                 QStringLiteral("integer"));
        const QJsonArray required = params.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 1);
        QCOMPARE(required.first().toString(), QStringLiteral("documentIndex"));

        // The read-only promise is the tool's whole point (v1.9 binding
        // invariant: reads of another tab must never move the run).
        const QString desc = fn.value(QStringLiteral("description")).toString();
        QVERIFY2(desc.contains(QStringLiteral("Does NOT switch, activate, or re-bind")),
                 qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("Read-only")), qPrintable(desc));
    }

    // Same STRICT-SCHEMA-001 idiom as thin_tempo_map: every property in
    // `required`, optionality = anyOf[<type>, null], dryRun defaulting true.
    void crossTab_importTracks_schemaIsStrictWithOptionalNullBranches() {
        const QJsonObject fn = findTool(QStringLiteral("import_tracks_from_document"));
        QVERIFY2(!fn.isEmpty(), "import_tracks_from_document is not in the CORE tool list");

        const QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
        const QJsonObject props = params.value(QStringLiteral("properties")).toObject();
        QStringList required;
        for (const QJsonValue &rv : params.value(QStringLiteral("required")).toArray())
            required << rv.toString();
        QCOMPARE(required.size(), props.size());
        for (const QString &key : {QStringLiteral("documentIndex"),
                                   QStringLiteral("trackIndexes"),
                                   QStringLiteral("dryRun")}) {
            QVERIFY2(props.contains(key), qPrintable(key));
            QVERIFY2(required.contains(key), qPrintable(key));
        }

        // documentIndex is mandatory (no null branch)...
        QCOMPARE(props.value(QStringLiteral("documentIndex")).toObject()
                     .value(QStringLiteral("type")).toString(),
                 QStringLiteral("integer"));
        // ...the other two are optional in substance: null branch present, so
        // executeTool's allowsNull escape lets callers omit them.
        for (const QString &key : {QStringLiteral("trackIndexes"),
                                   QStringLiteral("dryRun")}) {
            bool nullBranch = false;
            for (const QJsonValue &b : props.value(key).toObject()
                                           .value(QStringLiteral("anyOf")).toArray()) {
                if (b.toObject().value(QStringLiteral("type")).toString()
                    == QStringLiteral("null"))
                    nullBranch = true;
            }
            QVERIFY2(nullBranch,
                     qPrintable(QStringLiteral("%1 has no null branch, so the "
                                               "model cannot omit it").arg(key)));
        }

        // The description carries the whole cross-tab contract the model works
        // from: dry-run-confirm gate, source untouched, collisions and tempo
        // difference reported (never remapped/blocked), tick rescaling.
        const QString desc = fn.value(QStringLiteral("description")).toString();
        QVERIFY2(desc.contains(QStringLiteral("dryRun=true")), qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("dryRun=false")), qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("source document is not modified")),
                 qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("reported")), qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("rescaled")), qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("One undoable step")), qPrintable(desc));
    }

    // switch_document's DEFINITION exists only behind
    // ToolSchemaOptions::includeDocumentSwitch. Both default-options consumers
    // depend on its absence: the MCP server appends its OWN switch_document
    // (activate-the-tab contract) and must not see a shadowing second
    // definition, and executeTool's required-gate walks the default schema.
    void switchDocument_definitionIsOptInOnly() {
        setFfxivMode(true); // prove the gate composes with the FFXIV gate too

        const QJsonArray defaults = ToolDefinitions::toolSchemas();
        QVERIFY2(findToolIn(defaults, QStringLiteral("switch_document")).isEmpty(),
                 "switch_document leaked into the default (MCP-facing) schema");
        QCOMPARE(defaults.size(), kCoreToolNames.size() + kFfxivToolNames.size());

        ToolDefinitions::ToolSchemaOptions opts;
        opts.includeDocumentSwitch = true;
        const QJsonArray withSwitch = ToolDefinitions::toolSchemas(opts);
        QCOMPARE(withSwitch.size(), defaults.size() + 1);

        const QJsonObject fn = findToolIn(withSwitch, QStringLiteral("switch_document"));
        QVERIFY2(!fn.isEmpty(), "includeDocumentSwitch=true did not add switch_document");
        QCOMPARE(fn.value(QStringLiteral("strict")).toBool(), true);

        const QJsonObject params = fn.value(QStringLiteral("parameters")).toObject();
        const QJsonObject props = params.value(QStringLiteral("properties")).toObject();
        QCOMPARE(props.size(), 1);
        QCOMPARE(props.value(QStringLiteral("index")).toObject()
                     .value(QStringLiteral("type")).toString(),
                 QStringLiteral("integer"));
        const QJsonArray required = params.value(QStringLiteral("required")).toArray();
        QCOMPARE(required.size(), 1);
        QCOMPARE(required.first().toString(), QStringLiteral("index"));

        // The MidiPilot variant's UI semantics live in this description: the
        // visible tab stays put (deliberate difference to MCP's variant), the
        // model must re-read state after switching, and the tool is reserved
        // for explicit user requests.
        const QString desc = fn.value(QStringLiteral("description")).toString();
        QVERIFY2(desc.contains(QStringLiteral("visible tab does NOT change")),
                 qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("get_editor_state")), qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("ONLY when the user explicitly asks")),
                 qPrintable(desc));
        QVERIFY2(desc.contains(QStringLiteral("undo steps land")), qPrintable(desc));

        // The opted-in schema ships to real providers - it has to satisfy the
        // same strict-mode contract as the default set (one violation rejects
        // the whole request).
        QStringList problems;
        for (const QJsonValue &v : withSwitch) {
            const QJsonObject f = v.toObject().value(QStringLiteral("function")).toObject();
            checkStrictObject(f.value(QStringLiteral("parameters")).toObject(),
                              f.value(QStringLiteral("name")).toString(),
                              QStringLiteral("parameters"), problems);
        }
        QVERIFY2(problems.isEmpty(),
                 qPrintable(problems.join(QStringLiteral("\n  "))));
        clearFfxivMode();
    }

    // Reaching the generic dispatcher with switch_document means a caller ran
    // it without a runner (the AgentRunner intercepts it pre-dispatch to
    // re-bind the run; the MCP server intercepts it pre-bound-file-resolution
    // to activate the tab). The refusal must be structured and must say that
    // nothing was switched - never a success, never the generic "Unknown
    // tool" (which would read as a wiring bug rather than a contract).
    void executeTool_switchDocument_isRefusedByGenericDispatch() {
        for (const QJsonObject &args :
             {QJsonObject{{QStringLiteral("index"), 1}}, QJsonObject{}}) {
            const QJsonObject r = ToolDefinitions::executeTool(
                QStringLiteral("switch_document"), args, nullptr, nullptr);
            QCOMPARE(r.value(QStringLiteral("success")).toBool(true), false);
            QCOMPARE(r.value(QStringLiteral("handledBy")).toString(),
                     QStringLiteral("runtime"));
            const QString err = r.value(QStringLiteral("error")).toString();
            QVERIFY2(err.contains(QStringLiteral("No document was switched.")),
                     qPrintable(err));
            QVERIFY2(!err.contains(QStringLiteral("Unknown tool")), qPrintable(err));
        }
    }

    // list_documents takes no arguments and needs neither a file nor (at the
    // validation layer) a widget: the call must get past the required-gate
    // and reach its own read executor. (This build stubs the executor out, so
    // the "Stub build" error text is the proof of arrival - same trick as
    // thinTempoMap_acceptsNullAndOmittedArgs.)
    void executeTool_listDocuments_acceptsEmptyArgsAndDispatchesToItsExecutor() {
        const QJsonObject r = ToolDefinitions::executeTool(
            QStringLiteral("list_documents"), QJsonObject{}, nullptr, nullptr);
        const QString err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("Stub build: list_documents")),
                 qPrintable(err));
    }

    void executeTool_getDocumentOverview_gatesOnDocumentIndex() {
        // Missing documentIndex fails fast at the schema-derived gate...
        QJsonObject r = ToolDefinitions::executeTool(
            QStringLiteral("get_document_overview"), QJsonObject{}, nullptr, nullptr);
        QCOMPARE(r.value(QStringLiteral("success")).toBool(), false);
        QString err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("missing required")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("documentIndex")), qPrintable(err));

        // ...and with it, the call reaches the (stubbed) read executor.
        r = ToolDefinitions::executeTool(
            QStringLiteral("get_document_overview"),
            QJsonObject{{QStringLiteral("documentIndex"), 1}}, nullptr, nullptr);
        err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("Stub build: get_document_overview")),
                 qPrintable(err));
    }

    // The required-gate must demand ONLY documentIndex: trackIndexes and
    // dryRun carry null branches, so the allowsNull escape has to let both an
    // explicit null and a plain omission through (that escape breaking is
    // exactly how auto_fit_voice_load once rejected valid calls).
    void executeTool_importTracks_gatesOnlyOnDocumentIndex() {
        const QJsonObject r = ToolDefinitions::executeTool(
            QStringLiteral("import_tracks_from_document"), QJsonObject{},
            nullptr, nullptr);
        QCOMPARE(r.value(QStringLiteral("success")).toBool(), false);
        const QString err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("documentIndex")), qPrintable(err));
        QVERIFY2(!err.contains(QStringLiteral("trackIndexes")), qPrintable(err));
        QVERIFY2(!err.contains(QStringLiteral("dryRun")), qPrintable(err));
    }

    void executeTool_importTracks_acceptsNullAndOmittedOptionals() {
        QJsonObject nulls;
        nulls[QStringLiteral("documentIndex")] = 1;
        nulls[QStringLiteral("trackIndexes")] = QJsonValue::Null;
        nulls[QStringLiteral("dryRun")] = QJsonValue::Null;
        QJsonObject omitted;
        omitted[QStringLiteral("documentIndex")] = 1;
        for (const QJsonObject &args : {nulls, omitted}) {
            const QJsonObject r = ToolDefinitions::executeTool(
                QStringLiteral("import_tracks_from_document"), args,
                nullptr, nullptr);
            const QString err = r.value(QStringLiteral("error")).toString();
            QVERIFY2(!err.contains(QStringLiteral("missing required")), qPrintable(err));
            // Past the gate = the (stubbed) executor answered, proving the
            // tool dispatches to its OWN Protocol-action path, not to
            // widget->executeAction.
            QVERIFY2(err.contains(QStringLiteral(
                         "Stub build: import_tracks_from_document")),
                     qPrintable(err));
        }
    }

    // -----------------------------------------------------------------
    // v2.5.0 (Phase 51): document and file tools. The gate itself is unit-
    // tested in test_ai_file_naming; these slots pin what the MODEL sees -
    // the gate has to be in the descriptions, because the model cannot see it
    // before it runs into it - and the argument contract.

    void documentTools_descriptionsCarryTheAiGate() {
        for (const QString &name : kDocumentFileToolNames) {
            const QJsonObject fn = findTool(name);
            QVERIFY2(!fn.isEmpty(), qPrintable(name + QStringLiteral(" is not a CORE tool")));
        }
        for (const QString &name : {QStringLiteral("save_document"),
                                    QStringLiteral("save_document_as")}) {
            const QString desc = findTool(name).value(QStringLiteral("description")).toString();
            QVERIFY2(desc.contains(QStringLiteral(".midipilot.mid")), qPrintable(desc));
            QVERIFY2(desc.contains(QStringLiteral(".mcp.mid")), qPrintable(desc));
            QVERIFY2(desc.contains(QStringLiteral("counter")), qPrintable(desc));
            QVERIFY2(desc.contains(QStringLiteral("undone")), qPrintable(desc));
        }
        // "Keep the original": copy first, then edit - so a user Ctrl+S never
        // hits the original.
        const QString save = findTool(QStringLiteral("save_document"))
                                 .value(QStringLiteral("description")).toString();
        QVERIFY2(save.contains(QStringLiteral("save FIRST")), qPrintable(save));
        QVERIFY2(save.contains(QStringLiteral("ask the user")), qPrintable(save));
        // Neither caller is re-bound silently by new/open.
        for (const QString &name : {QStringLiteral("new_document"),
                                    QStringLiteral("open_document")}) {
            const QString desc = findTool(name).value(QStringLiteral("description")).toString();
            QVERIFY2(desc.contains(QStringLiteral("switch_document")), qPrintable(desc));
            QVERIFY2(desc.contains(QStringLiteral("get_editor_state")), qPrintable(desc));
        }
        const QString close = findTool(QStringLiteral("close_document"))
                                  .value(QStringLiteral("description")).toString();
        QVERIFY2(close.contains(QStringLiteral("unsaved changes")), qPrintable(close));
    }

    void documentTools_argumentContract() {
        auto nullable = [](const QJsonObject &prop) {
            for (const QJsonValue &b : prop.value(QStringLiteral("anyOf")).toArray()) {
                if (b.toObject().value(QStringLiteral("type")).toString() == QStringLiteral("null"))
                    return true;
            }
            return false;
        };
        auto propsOf = [](const QString &tool) {
            return findTool(tool).value(QStringLiteral("parameters")).toObject()
                .value(QStringLiteral("properties")).toObject();
        };
        // save_document(_as): one optional name.
        for (const QString &name : {QStringLiteral("save_document"),
                                    QStringLiteral("save_document_as")}) {
            const QJsonObject props = propsOf(name);
            QCOMPARE(props.size(), 1);
            QVERIFY(nullable(props.value(QStringLiteral("name")).toObject()));
        }
        // new_document: optional ppq.
        QCOMPARE(propsOf(QStringLiteral("new_document")).size(), 1);
        QVERIFY(nullable(propsOf(QStringLiteral("new_document"))
                             .value(QStringLiteral("ppq")).toObject()));
        // open_document / close_document: one mandatory argument each.
        QCOMPARE(propsOf(QStringLiteral("open_document")).value(QStringLiteral("path"))
                     .toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("string"));
        QCOMPARE(propsOf(QStringLiteral("close_document")).value(QStringLiteral("documentIndex"))
                     .toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("integer"));
    }

    // The required-gate demands exactly the mandatory arguments; past it the
    // call reaches the document executor (stubbed here - its "Stub build"
    // text is the proof of arrival).
    void executeTool_documentTools_gateAndDispatch() {
        for (const QString &name : {QStringLiteral("save_document"),
                                    QStringLiteral("save_document_as"),
                                    QStringLiteral("new_document")}) {
            const QJsonObject r = ToolDefinitions::executeTool(name, QJsonObject{},
                                                               nullptr, nullptr);
            const QString err = r.value(QStringLiteral("error")).toString();
            QVERIFY2(err.contains(QStringLiteral("Stub build: ") + name), qPrintable(err));
        }
        QJsonObject r = ToolDefinitions::executeTool(QStringLiteral("open_document"),
                                                     QJsonObject{}, nullptr, nullptr);
        QVERIFY2(r.value(QStringLiteral("error")).toString().contains(QStringLiteral("path")),
                 qPrintable(r.value(QStringLiteral("error")).toString()));
        r = ToolDefinitions::executeTool(QStringLiteral("close_document"), QJsonObject{},
                                         nullptr, nullptr);
        QVERIFY2(r.value(QStringLiteral("error")).toString()
                     .contains(QStringLiteral("documentIndex")),
                 qPrintable(r.value(QStringLiteral("error")).toString()));
        r = ToolDefinitions::executeTool(QStringLiteral("close_document"),
                                         QJsonObject{{QStringLiteral("documentIndex"), 1}},
                                         nullptr, nullptr);
        QVERIFY2(r.value(QStringLiteral("error")).toString()
                     .contains(QStringLiteral("Stub build: close_document")),
                 qPrintable(r.value(QStringLiteral("error")).toString()));
    }

    // -----------------------------------------------------------------
    // MCP-ARGS-001: an undeclared argument is refused and named - never
    // silently dropped (a dropped "types" once made delete_events remove
    // every event in the range).
    void executeTool_unknownArgument_isRefusedAndNamed() {
        QJsonObject args;
        args[QStringLiteral("trackIndex")] = 0;
        args[QStringLiteral("startTick")] = 0;
        args[QStringLiteral("endTick")] = 10;
        args[QStringLiteral("kinds")] = QJsonArray{QStringLiteral("program_change")};
        QJsonObject r = ToolDefinitions::executeTool(QStringLiteral("delete_events"), args,
                                                     nullptr, nullptr);
        QCOMPARE(r.value(QStringLiteral("success")).toBool(true), false);
        QString err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("does not accept")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("kinds")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("types")), qPrintable(err)); // the real name is listed
        QVERIFY2(err.contains(QStringLiteral("Nothing was changed")), qPrintable(err));

        // A parameterless tool says so.
        r = ToolDefinitions::executeTool(QStringLiteral("get_editor_state"),
                                         QJsonObject{{QStringLiteral("verbose"), true}},
                                         nullptr, nullptr);
        err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("verbose")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("parameters are: none")), qPrintable(err));

        // Missing and unknown together: both named in one answer.
        r = ToolDefinitions::executeTool(QStringLiteral("remove_track"),
                                         QJsonObject{{QStringLiteral("track"), 1}},
                                         nullptr, nullptr);
        err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("missing required")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("trackIndex")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("does not accept parameter(s): track ")),
                 qPrintable(err));
    }

    // MCP-ARGS-001 part 2: query_events / delete_events filter by the kinds
    // insert_events writes; null = every kind (the old behaviour).
    void queryAndDeleteEvents_offerNullableTypesFilter() {
        for (const QString &name : {QStringLiteral("query_events"),
                                    QStringLiteral("delete_events")}) {
            const QJsonObject params = findTool(name).value(QStringLiteral("parameters")).toObject();
            const QJsonObject types = params.value(QStringLiteral("properties")).toObject()
                                          .value(QStringLiteral("types")).toObject();
            QVERIFY2(!types.isEmpty(), qPrintable(name));
            QStringList kinds;
            bool nullBranch = false;
            for (const QJsonValue &b : types.value(QStringLiteral("anyOf")).toArray()) {
                const QJsonObject branch = b.toObject();
                if (branch.value(QStringLiteral("type")).toString() == QStringLiteral("null"))
                    nullBranch = true;
                for (const QJsonValue &k : branch.value(QStringLiteral("items")).toObject()
                                               .value(QStringLiteral("enum")).toArray())
                    kinds << k.toString();
            }
            QVERIFY2(nullBranch, qPrintable(name));
            QCOMPARE(QSet<QString>(kinds.begin(), kinds.end()),
                     (QSet<QString>{QStringLiteral("note"), QStringLiteral("cc"),
                                    QStringLiteral("pitch_bend"),
                                    QStringLiteral("program_change")}));
            bool listed = false;
            for (const QJsonValue &rv : params.value(QStringLiteral("required")).toArray())
                listed = listed || rv.toString() == QStringLiteral("types");
            QVERIFY2(listed, qPrintable(name)); // strict mode: optional = listed + null
        }
    }

    // 51.2: set_tempo takes an exact microseconds value or a fractional BPM;
    // both nullable, so a call with only one of them passes the gate.
    void setTempo_acceptsExactMicrosecondsOrFractionalBpm() {
        const QJsonObject params = findTool(QStringLiteral("set_tempo"))
                                       .value(QStringLiteral("parameters")).toObject();
        const QJsonObject props = params.value(QStringLiteral("properties")).toObject();
        auto branchTypes = [&](const QString &key) {
            QStringList t;
            for (const QJsonValue &b : props.value(key).toObject()
                                           .value(QStringLiteral("anyOf")).toArray())
                t << b.toObject().value(QStringLiteral("type")).toString();
            return t;
        };
        QCOMPARE(branchTypes(QStringLiteral("bpm")),
                 (QStringList{QStringLiteral("number"), QStringLiteral("null")}));
        QCOMPARE(branchTypes(QStringLiteral("microsecondsPerQuarter")),
                 (QStringList{QStringLiteral("integer"), QStringLiteral("null")}));
        QCOMPARE(params.value(QStringLiteral("required")).toArray().size(), 3);

        // The timing read is parameterless and dispatches to its executor.
        const QJsonObject r = ToolDefinitions::executeTool(
            QStringLiteral("get_timing_map"), QJsonObject{}, nullptr, nullptr);
        QVERIFY2(r.value(QStringLiteral("error")).toString()
                     .contains(QStringLiteral("Stub build: get_timing_map")),
                 qPrintable(r.value(QStringLiteral("error")).toString()));
    }

    // -----------------------------------------------------------------
    // Phase 31 — public isPitchBendOnlyPayload helper used by AgentRunner.
    void isPitchBendOnlyPayload_detectsAllPitchBend() {
        QJsonArray evs;
        QJsonObject pb;
        pb[QStringLiteral("type")] = QStringLiteral("pitch_bend");
        pb[QStringLiteral("tick")] = 0;
        pb[QStringLiteral("value")] = 8192;
        evs.append(pb);
        evs.append(pb);
        QVERIFY(ToolDefinitions::isPitchBendOnlyPayload(evs));
    }

    void isPitchBendOnlyPayload_falseForEmpty() {
        QVERIFY(!ToolDefinitions::isPitchBendOnlyPayload(QJsonArray{}));
    }

    void isPitchBendOnlyPayload_falseWhenMixedWithNote() {
        QJsonArray evs;
        QJsonObject pb;
        pb[QStringLiteral("type")] = QStringLiteral("pitch_bend");
        pb[QStringLiteral("tick")] = 0;
        pb[QStringLiteral("value")] = 8192;
        evs.append(pb);
        QJsonObject note;
        note[QStringLiteral("type")] = QStringLiteral("note");
        note[QStringLiteral("tick")] = 0;
        note[QStringLiteral("pitch")] = 60;
        note[QStringLiteral("velocity")] = 100;
        note[QStringLiteral("duration")] = 240;
        evs.append(note);
        QVERIFY(!ToolDefinitions::isPitchBendOnlyPayload(evs));
    }

    // -----------------------------------------------------------------
    // Argument-validation hardening: a tool call missing a required parameter
    // must fail fast with a clear, schema-derived message — BEFORE any dispatch,
    // so we can pass null file/widget (the validation path never touches them).
    void executeTool_missingRequired_returnsClearError() {
        QJsonObject r = ToolDefinitions::executeTool(
            QStringLiteral("remove_track"), QJsonObject{}, nullptr, nullptr);
        QCOMPARE(r.value(QStringLiteral("success")).toBool(), false);
        QVERIFY2(r.value(QStringLiteral("error")).toString().contains(QStringLiteral("trackIndex")),
                 qPrintable(r.value(QStringLiteral("error")).toString()));
    }

    // Misnamed args (a common MCP-client mistake) surface as "missing required",
    // naming the real parameters — not a confusing tool-specific error.
    void executeTool_misnamedArgs_namesTheRealRequiredParams() {
        QJsonObject args;
        args[QStringLiteral("fromTrack")] = 0;   // wrong name (real: sourceTrackIndex)
        args[QStringLiteral("toTrack")] = 1;     // wrong name (real: targetTrackIndex)
        args[QStringLiteral("startTick")] = 0;
        args[QStringLiteral("endTick")] = 100;
        QJsonObject r = ToolDefinitions::executeTool(
            QStringLiteral("move_events_to_track"), args, nullptr, nullptr);
        QCOMPARE(r.value(QStringLiteral("success")).toBool(), false);
        const QString err = r.value(QStringLiteral("error")).toString();
        QVERIFY2(err.contains(QStringLiteral("sourceTrackIndex")), qPrintable(err));
        QVERIFY2(err.contains(QStringLiteral("targetTrackIndex")), qPrintable(err));
    }

    // -----------------------------------------------------------------
    // STRICT-SCHEMA-001: the OpenAI Responses API validates function schemas
    // in STRICT mode by default, and strict mode demands that `required` list
    // EVERY key in `properties` (optionality is expressed by an anyOf with a
    // null branch, never by leaving a key out) and that objects set
    // additionalProperties=false. A single violation rejects the WHOLE request
    // with HTTP 400, so one sloppy tool takes every other tool down with it -
    // that is how auto_fit_voice_load made FFXIV mode unusable on gpt-5.5 and
    // gpt-5.6 while chat-completions models never noticed.
    //
    // The API reports only the FIRST offending key it finds, so fixing them
    // one HTTP 400 at a time is hopeless. This checks every tool at once,
    // recursively, with FFXIV mode ON so the FFXIV tools are covered too.
    void toolSchemas_satisfyStrictModeInEveryTool() {
        setFfxivMode(true);
        const QJsonArray tools = ToolDefinitions::toolSchemas();
        QVERIFY(!tools.isEmpty());

        QStringList problems;
        for (const QJsonValue &v : tools) {
            const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            checkStrictObject(fn.value(QStringLiteral("parameters")).toObject(),
                              name, QStringLiteral("parameters"), problems);
        }
        clearFfxivMode();

        QVERIFY2(problems.isEmpty(),
                 qPrintable(QStringLiteral(
                     "strict-mode schema violations - the Responses API rejects the "
                     "entire request over any one of these:\n  %1")
                                .arg(problems.join(QStringLiteral("\n  ")))));
    }

    // Phase 47 — the no-pitch_bend schema is no longer a gpt-5.5-only code
    // path: any prompt profile can switch it on for its models, so it now
    // ships to arbitrary providers. Two invariants in one place: the
    // pitch_bend branch really is gone from BOTH write tools, and dropping a
    // branch does not break the strict-mode contract the Responses API
    // enforces (a single violation rejects the whole request, so the reduced
    // schema has to be as strict as the full one).
    void toolSchemas_withoutPitchBend_hasNoPitchBendBranchAndStaysStrict() {
        setFfxivMode(true); // widest tool set: core + FFXIV
        ToolDefinitions::ToolSchemaOptions opts;
        opts.includePitchBend = false;
        const QJsonArray tools = ToolDefinitions::toolSchemas(opts);
        QVERIFY(!tools.isEmpty());

        // --- no pitch_bend branch in insert_events / replace_events -------
        int writeToolsSeen = 0;
        for (const QJsonValue &v : tools) {
            const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            if (name != QStringLiteral("insert_events")
                && name != QStringLiteral("replace_events"))
                continue;
            ++writeToolsSeen;

            const QJsonArray anyOf = fn.value(QStringLiteral("parameters")).toObject()
                                       .value(QStringLiteral("properties")).toObject()
                                       .value(QStringLiteral("events")).toObject()
                                       .value(QStringLiteral("items")).toObject()
                                       .value(QStringLiteral("anyOf")).toArray();
            QVERIFY2(!anyOf.isEmpty(),
                     qPrintable(QStringLiteral("%1: events.items.anyOf missing").arg(name)));
            bool sawNote = false;
            for (const QJsonValue &branch : anyOf) {
                const QJsonArray types = branch.toObject()
                                             .value(QStringLiteral("properties")).toObject()
                                             .value(QStringLiteral("type")).toObject()
                                             .value(QStringLiteral("enum")).toArray();
                for (const QJsonValue &t : types) {
                    const QString type = t.toString();
                    if (type == QStringLiteral("note"))
                        sawNote = true;
                    QVERIFY2(type != QStringLiteral("pitch_bend"),
                             qPrintable(QStringLiteral("%1 still offers a pitch_bend branch")
                                            .arg(name)));
                }
            }
            QVERIFY2(sawNote, qPrintable(QStringLiteral("%1 lost its note branch").arg(name)));
        }
        QCOMPARE(writeToolsSeen, 2);

        // --- and the reduced schema is still strict-mode clean -------------
        QStringList problems;
        for (const QJsonValue &v : tools) {
            const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            checkStrictObject(fn.value(QStringLiteral("parameters")).toObject(),
                              name, QStringLiteral("parameters"), problems);
        }
        clearFfxivMode();

        QVERIFY2(problems.isEmpty(),
                 qPrintable(QStringLiteral(
                     "strict-mode schema violations in the no-pitch_bend variant:\n  %1")
                                .arg(problems.join(QStringLiteral("\n  ")))));
    }

private:
    // Find one tool's "function" object in a schema array; empty when absent.
    static QJsonObject findToolIn(const QJsonArray &tools, const QString &name) {
        for (const QJsonValue &v : tools) {
            const QJsonObject fn =
                v.toObject().value(QStringLiteral("function")).toObject();
            if (fn.value(QStringLiteral("name")).toString() == name)
                return fn;
        }
        return QJsonObject();
    }

    // Same, over the default (no-options) schema.
    static QJsonObject findTool(const QString &name) {
        return findToolIn(ToolDefinitions::toolSchemas(), name);
    }

    // Recursive strict-mode check: every object schema must require all of its
    // properties and forbid extra ones. Descends into properties, anyOf/oneOf
    // branches and array items, because a nested event schema is validated just
    // as strictly as the top level.
    static void checkStrictObject(const QJsonObject &schema,
                                  const QString &toolName,
                                  const QString &path,
                                  QStringList &problems) {
        if (schema.value(QStringLiteral("type")).toString() == QStringLiteral("object")) {
            const QJsonObject props = schema.value(QStringLiteral("properties")).toObject();
            QSet<QString> required;
            for (const QJsonValue &r : schema.value(QStringLiteral("required")).toArray())
                required.insert(r.toString());

            for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
                if (!required.contains(it.key())) {
                    problems << QStringLiteral("%1 (%2): '%3' is in properties but not in required")
                                    .arg(toolName, path, it.key());
                }
            }
            if (schema.value(QStringLiteral("additionalProperties")).toBool(true)) {
                problems << QStringLiteral("%1 (%2): additionalProperties must be false")
                                .arg(toolName, path);
            }
            for (auto it = props.constBegin(); it != props.constEnd(); ++it) {
                checkStrictObject(it.value().toObject(), toolName,
                                  path + QStringLiteral(".") + it.key(), problems);
            }
        }
        for (const QString &key : {QStringLiteral("anyOf"), QStringLiteral("oneOf")}) {
            const QJsonArray branches = schema.value(key).toArray();
            for (int i = 0; i < branches.size(); ++i) {
                checkStrictObject(branches.at(i).toObject(), toolName,
                                  QStringLiteral("%1.%2[%3]").arg(path, key).arg(i), problems);
            }
        }
        if (schema.contains(QStringLiteral("items"))) {
            checkStrictObject(schema.value(QStringLiteral("items")).toObject(), toolName,
                              path + QStringLiteral(".items"), problems);
        }
    }
};

QTEST_APPLESS_MAIN(TestToolDefinitions)
#include "test_tool_definitions.moc"
