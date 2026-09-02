/*
 * MidiEditor
 *
 * Instrument Definitions Manager
 */

#include "InstrumentDefinitions.h"
#include "MidiFile.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

InstrumentDefinitions* InstrumentDefinitions::_instance = 0;

namespace {

// QSettings treats '/' as its group separator, so a .ins section name carrying
// one ("Roland/GS") used to be written as a NESTED group that loadOverrides()
// could no longer find - those program-name overrides were silently lost on
// restart. Escape the separator (and the escape character itself) so any
// section name survives the round trip. A name with neither '%' nor a separator
// is written unchanged, which keeps overrides saved by earlier versions
// readable.
const char* const USER_CUSTOM_SECTION = "_UserCustom_";

QString encodeSectionName(const QString& section) {
    QString encoded;
    encoded.reserve(section.size());
    for (int i = 0; i < section.size(); i++) {
        const QChar c = section.at(i);
        if (c == QLatin1Char('%')) {
            encoded += QLatin1String("%25");
        } else if (c == QLatin1Char('/')) {
            encoded += QLatin1String("%2F");
        } else if (c == QLatin1Char('\\')) {
            encoded += QLatin1String("%5C");
        } else {
            encoded += c;
        }
    }
    return encoded;
}

QString decodeSectionName(const QString& section) {
    QString decoded;
    decoded.reserve(section.size());
    for (int i = 0; i < section.size(); i++) {
        const QChar c = section.at(i);
        if (c == QLatin1Char('%') && i + 2 < section.size()) {
            bool ok = false;
            const int code = section.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                decoded += QChar(code);
                i += 2;
                continue;
            }
        }
        decoded += c;
    }
    return decoded;
}

} // namespace

InstrumentDefinitions::InstrumentDefinitions() {
}

InstrumentDefinitions::~InstrumentDefinitions() {
}

InstrumentDefinitions* InstrumentDefinitions::instance() {
    if (!_instance) {
        _instance = new InstrumentDefinitions();
    }
    return _instance;
}

void InstrumentDefinitions::cleanup() {
    if (_instance) {
        delete _instance;
        _instance = 0;
    }
}

void InstrumentDefinitions::clear() {
    _definitions.clear();
    _overrides.clear();
    // _ccOverrides stays: the control-change names belong to the Control Change
    // settings page, not to the .ins definition feature whose "Clear" button is
    // the only caller here. Wiping them made the next saveOverrides() drop every
    // custom controller name from the settings file.
    _inheritance.clear();
    _currentFile = "";
    _currentInstrument = "";
}

bool InstrumentDefinitions::load(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    _definitions.clear();
    _inheritance.clear();
    _currentFile = filename;
    
    // Keep the current instrument if it still exists, otherwise reset
    QString oldInstrument = _currentInstrument;
    _currentInstrument = "";

    QTextStream in(&file);
    QString currentSection = "";
    
    // Matches "10=Flute" or "10 = Flute"
    QRegularExpression entryRegex("^\\s*(\\d+)\\s*=\\s*(.+)$");
    
    // Matches "[Section Name]"
    QRegularExpression sectionRegex("^\\[(.+)\\]$");

    // Matches "BasedOn=Section Name"
    QRegularExpression basedOnRegex("^BasedOn=(.+)$");

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(";") || line.startsWith(".")) {
            continue;
        }

        QRegularExpressionMatch sectionMatch = sectionRegex.match(line);
        if (sectionMatch.hasMatch()) {
            currentSection = sectionMatch.captured(1).trimmed();
            continue;
        }

        if (!currentSection.isEmpty()) {
            QRegularExpressionMatch entryMatch = entryRegex.match(line);
            if (entryMatch.hasMatch()) {
                bool ok;
                int program = entryMatch.captured(1).toInt(&ok);
                // MIDI programs are 0-127. Some files might use 1-128 or 0-127.
                // .ins files typically use 0-based indexing for patches.
                if (ok && program >= 0 && program < 128) {
                    _definitions[currentSection][program] = entryMatch.captured(2).trimmed();
                }
            } else {
                QRegularExpressionMatch basedOnMatch = basedOnRegex.match(line);
                if (basedOnMatch.hasMatch()) {
                    _inheritance[currentSection] = basedOnMatch.captured(1).trimmed();
                    // Ensure section exists in definitions so we can iterate it later
                    if (!_definitions.contains(currentSection)) {
                         _definitions[currentSection] = QMap<int, QString>();
                    }
                }
            }
        }
    }

    // Resolve inheritance
    QList<QString> visited;
    // Create a temporary list of keys to avoid issues if _definitions changes during iteration (though it shouldn't)
    QStringList sections = _definitions.keys(); 
    // Also include sections that are in _inheritance but might not have had any definitions
    // (though we added them above)
    
    foreach (const QString& section, sections) {
        visited.clear();
        resolveInheritance(section, visited);
    }
    
    if (_definitions.contains(oldInstrument)) {
        _currentInstrument = oldInstrument;
    } else if (!_definitions.isEmpty()) {
        // Select the first one by default if available
        _currentInstrument = _definitions.firstKey();
    }

    return true;
}

void InstrumentDefinitions::resolveInheritance(const QString& section, QList<QString>& visited) {
    if (visited.contains(section)) {
        return; // Cycle detected or already visited in this chain
    }
    visited.append(section);

    if (_inheritance.contains(section)) {
        QString base = _inheritance[section];
        
        // Recursively resolve base first
        resolveInheritance(base, visited);
        
        if (_definitions.contains(base)) {
            // Merge base into current
            // We want current definitions to override base definitions
            // So we start with base, and insert current
            QMap<int, QString> merged = _definitions[base];
            const QMap<int, QString>& current = _definitions[section];
            
            QMap<int, QString>::const_iterator it;
            for (it = current.constBegin(); it != current.constEnd(); ++it) {
                merged.insert(it.key(), it.value());
            }
            
            _definitions[section] = merged;
        }
    }
}

QStringList InstrumentDefinitions::instruments() const {
    return _definitions.keys();
}

void InstrumentDefinitions::selectInstrument(const QString& name) {
    if (_definitions.contains(name)) {
        _currentInstrument = name;
    }
}

QString InstrumentDefinitions::currentInstrument() const {
    return _currentInstrument;
}

QString InstrumentDefinitions::currentFile() const {
    return _currentFile;
}

void InstrumentDefinitions::setInstrumentName(int program, const QString& name) {
    // Allow empty instrument (defaults/overrides only)
    QString key = _currentInstrument;
    
    if (name.isEmpty()) {
        // If name is empty, remove override
        if (_overrides.contains(key)) {
            _overrides[key].remove(program);
        }
    } else {
        // Add or update override
        _overrides[key][program] = name;
    }
}

QMap<int, QString> InstrumentDefinitions::instrumentNames() const {
    QMap<int, QString> names;
    
    // First load base definitions
    if (!_currentInstrument.isEmpty() && _definitions.contains(_currentInstrument)) {
        names = _definitions[_currentInstrument];
    }
    
    // Then apply overrides
    // Use current instrument key (which might be empty string)
    QString key = _currentInstrument;
    if (_overrides.contains(key)) {
        QMapIterator<int, QString> it(_overrides[key]);
        while (it.hasNext()) {
            it.next();
            names.insert(it.key(), it.value());
        }
    }
    
    return names;
}

void InstrumentDefinitions::setControlChangeName(int control, const QString& name) {
    if (name.isEmpty()) {
        _ccOverrides.remove(control);
    } else {
        _ccOverrides[control] = name;
    }
}

QString InstrumentDefinitions::controlChangeName(int control) const {
    if (_ccOverrides.contains(control)) {
        return _ccOverrides[control];
    }
    return "";
}

QMap<int, QString> InstrumentDefinitions::controlChangeNames() const {
    return _ccOverrides;
}

void InstrumentDefinitions::loadOverrides(QSettings* settings) {
    if (!settings) return;
    
    _overrides.clear();
    
    settings->beginGroup("InstrumentDefinitions/Overrides");
    QStringList instruments = settings->childGroups();
    foreach(QString section, instruments) {
        // Handle placeholder for custom/empty instrument
        QString instr = (section == QLatin1String(USER_CUSTOM_SECTION))
                            ? QString()
                            : decodeSectionName(section);
        
        settings->beginGroup(section);
        QStringList keys = settings->childKeys();
        foreach(QString key, keys) {
            bool ok;
            int program = key.toInt(&ok);
            if (ok) {
                _overrides[instr][program] = settings->value(key).toString();
            }
        }
        settings->endGroup();
    }
    settings->endGroup();

    settings->beginGroup("InstrumentDefinitions/CCOverrides");
    QStringList ccKeys = settings->childKeys();
    foreach(QString key, ccKeys) {
        bool ok;
        int control = key.toInt(&ok);
        if (ok) {
            _ccOverrides[control] = settings->value(key).toString();
        }
    }
    settings->endGroup();
}

void InstrumentDefinitions::saveOverrides(QSettings* settings) {
    if (!settings) return;
    
    settings->beginGroup("InstrumentDefinitions/Overrides");
    settings->remove(""); // Clear previous overrides
    
    QMap<QString, QMap<int, QString> >::const_iterator it;
    for (it = _overrides.constBegin(); it != _overrides.constEnd(); ++it) {
        QString instr = it.key();
        // Use placeholder for empty instrument
        QString section = instr.isEmpty() ? QString(QLatin1String(USER_CUSTOM_SECTION))
                                          : encodeSectionName(instr);
        // A real .ins section literally named like the placeholder must not be
        // read back as the custom (empty-name) bank - escape it so it differs.
        if (!instr.isEmpty() && section == QLatin1String(USER_CUSTOM_SECTION)) {
            section = QLatin1String("%5F") + section.mid(1);
        }
        
        settings->beginGroup(section);
        
        QMap<int, QString> bank = it.value();
        QMap<int, QString>::const_iterator bankIt;
        for (bankIt = bank.constBegin(); bankIt != bank.constEnd(); ++bankIt) {
            settings->setValue(QString::number(bankIt.key()), bankIt.value());
        }
        
        settings->endGroup();
    }
    settings->endGroup();

    settings->beginGroup("InstrumentDefinitions/CCOverrides");
    settings->remove(""); // Clear previous overrides
    
    QMapIterator<int, QString> itCC(_ccOverrides);
    while (itCC.hasNext()) {
        itCC.next();
        settings->setValue(QString::number(itCC.key()), itCC.value());
    }
    settings->endGroup();
}

QString InstrumentDefinitions::instrumentName(int program) const {
    // Check overrides first
    QString key = _currentInstrument;
    if (_overrides.contains(key) && _overrides[key].contains(program)) {
        return _overrides[key][program];
    }
    
    // Fallback to definitions
    if (!_currentInstrument.isEmpty() && _definitions.contains(_currentInstrument)) {
        const QMap<int, QString>& bank = _definitions.value(_currentInstrument);
        if (bank.contains(program)) {
            return bank.value(program);
        }
    }
    
    // Fallback to GM
    return gmInstrumentName(program);
}

QString InstrumentDefinitions::gmInstrumentName(int program) {
    return MidiFile::gmInstrumentName(program);
}
