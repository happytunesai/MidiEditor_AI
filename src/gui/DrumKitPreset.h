#ifndef DRUMKITPRESET_H_
#define DRUMKITPRESET_H_

#include <QList>
#include <QString>

struct DrumGroup {
    QString name;
    QList<int> noteNumbers;
};

class DrumKitPreset {
public:
    QString name;
    QList<DrumGroup> groups;

    static QList<DrumKitPreset> presets();
    static DrumKitPreset gmPreset();
    static DrumKitPreset rockPreset();
    static DrumKitPreset jazzPreset();

    // FFXIV bard percussion grouping (v2.0): buckets GM channel-9 drum notes
    // into the FFXIV percussion instrument tracks by NAME. Standalone (not part
    // of presets()) - used by the dedicated "FFXIV drum split" action.
    static DrumKitPreset ffxivPreset();
};

// ---------------------------------------------------------------------------
// v2.0 Phase 2: FFXIV pitch-mapping presets for the drum split's transpose
// mode. Each maps GM channel-9 drum notes onto concrete pitches of the FFXIV
// percussion instruments (community kits: Mog Amp, Bard Forge 1/2, Bard
// Metal). The split tracks STAY on channel 9: for percussion tracks
// recognised by NAME, playback/export inject the FFXIV program from the
// track name per hit (the GM-note fallback, where the note number selects
// the drum, only applies to unnamed tracks) - so the note number is free to
// carry the kit pitch. Distinct types from DrumKitPreset/DrumGroup on
// purpose (that model is note-GROUPING only and has other consumers).
// ---------------------------------------------------------------------------

struct FfxivDrumNoteMap {
    int sourceNote;   ///< GM drum note on channel 9 (35-81)
    int targetNote;   ///< pitch on the FFXIV instrument (C3-C6 = 48-84)
};

struct FfxivDrumMapGroup {
    QString trackName;                 ///< FFXIV instrument (also the track name)
    int programNumber;                 ///< FF14 SoundFont program for that instrument
    QList<FfxivDrumNoteMap> mappings;  ///< per-note source -> target pitches
};

struct FfxivDrumMapPreset {
    QString name;
    QList<FfxivDrumMapGroup> groups;

    /// The shipped community kits: Mog Amp, Bard Forge 1, Bard Forge 2, Bard Metal.
    static QList<FfxivDrumMapPreset> presets();
};

#endif // DRUMKITPRESET_H_
