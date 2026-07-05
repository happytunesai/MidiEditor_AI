#include "DrumKitPreset.h"

DrumKitPreset DrumKitPreset::gmPreset() {
    DrumKitPreset p;
    p.name = "General MIDI";
    p.groups = {
        {"Bass Drum",    {35, 36}},
        {"Snare",        {37, 38, 39, 40}},
        {"Low Tom",      {41, 43, 45}},
        {"Mid Tom",      {47, 48}},
        {"High Tom",     {50}},
        {"Hi-Hat",       {42, 44, 46}},
        {"Crash Cymbal", {49, 57}},
        {"Ride Cymbal",  {51, 53, 59}},
        {"Percussion",   {54, 55, 56, 58, 60, 61, 62, 63, 64, 65, 66, 67,
                          68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81}},
    };
    return p;
}

DrumKitPreset DrumKitPreset::rockPreset() {
    DrumKitPreset p;
    p.name = "Rock";
    p.groups = {
        {"Kick",         {35, 36}},
        {"Snare",        {38, 40}},
        {"Hi-Hat",       {42, 44, 46}},
        {"Toms",         {41, 43, 45, 47, 48, 50}},
        {"Crash",        {49, 57}},
        {"Ride",         {51, 53, 59}},
        {"Other",        {37, 39, 54, 55, 56, 58, 60, 61, 62, 63, 64, 65,
                          66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81}},
    };
    return p;
}

DrumKitPreset DrumKitPreset::jazzPreset() {
    DrumKitPreset p;
    p.name = "Jazz";
    p.groups = {
        {"Kick",         {35, 36}},
        {"Snare/Brush",  {37, 38, 39, 40}},
        {"Hi-Hat",       {42, 44, 46}},
        {"Ride",         {51, 53, 59}},
        {"Toms",         {41, 43, 45, 47, 48, 50}},
        {"Crash/Splash", {49, 55, 57}},
        {"Percussion",   {54, 56, 58, 60, 61, 62, 63, 64, 65, 66, 67,
                          68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81}},
    };
    return p;
}

// FFXIV bard percussion split (v2.0 #2). Buckets GM channel-9 drum notes into
// the FFXIV percussion instruments by NAME so a COSMETIC CH9 split (tracks stay
// on channel 9, pitches untouched) plays the right sound via the name-keyed
// program-change injection (FluidSynthEngine::drumProgramForTrackName). The four
// playable instrument names MUST stay byte-identical to that map's keys
// ("Bass Drum"/"Snare Drum"/"Cymbal"/"Bongo"); "Other Percussion" is the
// catch-all for GM drums with no FFXIV equivalent (woodblocks, whistles, bells,
// ...) so the user can handle them by hand. Toms -> Bass Drum (FFXIV has no
// tom); Timpani is a tonal instrument handled separately by the channel fixer,
// NOT part of this percussion split.
DrumKitPreset DrumKitPreset::ffxivPreset() {
    DrumKitPreset p;
    p.name = "FFXIV";
    p.groups = {
        {"Bass Drum",        {35, 36, 41, 43, 45, 47, 48, 50}},
        {"Snare Drum",       {37, 38, 39, 40}},
        {"Cymbal",           {42, 44, 46, 49, 51, 52, 53, 55, 57, 59}},
        {"Bongo",            {60, 61, 62, 63, 64, 65, 66}},
        {"Other Percussion", {54, 56, 58, 67, 68, 69, 70, 71, 72, 73, 74,
                              75, 76, 77, 78, 79, 80, 81}},
    };
    return p;
}

QList<DrumKitPreset> DrumKitPreset::presets() {
    return {gmPreset(), rockPreset(), jazzPreset()};
}

// ---------------------------------------------------------------------------
// FFXIV pitch-mapping presets (v2.0 Phase 2). Note tables follow the FFXIV
// bard community kits (BardMusicPlayer/MogNotate lineage); the program
// numbers are OUR FF14-c3c6 SoundFont presets (Bongo 116, Bass Drum 117,
// Snare Drum 118, Cymbal 119 - verified against the SF phdr, see
// FFXIVChannelFixer::programNumber). Track names MUST stay byte-identical to
// FluidSynthEngine::drumProgramForTrackName's keys. GM notes not listed in a
// kit (hi-hats, rides, ...) intentionally have no FFXIV equivalent and stay
// on channel 9 via the split's "Other Percussion" sweep.
// ---------------------------------------------------------------------------

QList<FfxivDrumMapPreset> FfxivDrumMapPreset::presets() {
    auto makePreset = [](const QString &name,
                         const QList<FfxivDrumNoteMap> &bassDrum,
                         const QList<FfxivDrumNoteMap> &snareDrum,
                         const QList<FfxivDrumNoteMap> &cymbal,
                         const QList<FfxivDrumNoteMap> &bongo) {
        FfxivDrumMapPreset p;
        p.name = name;
        p.groups = {
            {"Bass Drum",  117, bassDrum},
            {"Snare Drum", 118, snareDrum},
            {"Cymbal",     119, cymbal},
            {"Bongo",      116, bongo},
        };
        return p;
    };

    // "MidiEditor AI (Happy Tunes)" - the app's house kit, calibrated against
    // real before/after edits (3-song MCP comparison, 2026-07-02): kicks
    // ANCHOR on C4 (both GM kicks -> 60), snares ANCHOR on C5 (both GM
    // snares -> 72), toms shift a flat +24, and the crash-type cymbals
    // anchor on the editing convention "low crashes on C5, china on F#5,
    // high crashes on C6". Hi-hats, rides, splash and the remaining hand
    // percussion are deliberately UNMAPPED: they fall into "Other
    // Percussion" (channel 10, GM pitches untouched) because rhythm cymbals
    // rarely translate 1:1 into FFXIV - which cymbal source lands in which
    // zone (and what gets dropped) is a per-song editing decision.
    FfxivDrumMapPreset houseKit;
    houseKit.name = "MidiEditor AI (Happy Tunes)";
    houseKit.groups = {
        {"Bass Drum",  117, {{35, 60}, {36, 60}, {41, 65}, {43, 67},
                             {45, 69}, {47, 71}, {48, 72}, {50, 74}}},
        {"Snare Drum", 118, {{38, 72}, {40, 72}}},
        {"Cymbal",     119, {{49, 72}, {52, 78}, {57, 84}}},
        // Bongos/congas follow the Bard Metal hand-percussion block. The
        // calibration edits placed bongos BY EAR per song (matching the
        // original's pitch/feel to the in-game bongos - no stable pattern
        // exists), so a community default is the honest baseline here. When
        // a bongo line is too dense, editors also spill notes onto other
        // percussion instruments chosen by pitch similarity - that stays a
        // manual technique by design.
        {"Bongo",      116, {{60, 70}, {61, 67}, {62, 72}, {63, 65}, {64, 74}}},
    };

    return {
        houseKit,
        makePreset("Mog Amp",
            {{35, 55}, {36, 57}, {41, 63}, {43, 66}, {45, 70}, {47, 73}, {48, 77}, {50, 80}},
            {{38, 67}, {40, 69}},
            {{49, 71}, {52, 69}, {55, 77}, {57, 71}},
            {{60, 70}, {61, 67}}),
        makePreset("Bard Forge 1",
            {{35, 48}, {36, 51}, {41, 56}, {43, 58}, {45, 60}, {47, 62}, {48, 51}, {50, 53}},
            {{38, 62}, {40, 64}},
            {{49, 73}, {52, 76}, {55, 79}, {57, 81}},
            {{60, 60}, {61, 61}}),
        makePreset("Bard Forge 2",
            {{35, 53}, {36, 55}, {41, 58}, {43, 61}, {45, 65}, {47, 68}, {48, 71}, {50, 74}},
            {{38, 64}, {40, 66}},
            {{49, 71}, {52, 69}, {55, 77}, {57, 71}},
            {{60, 70}, {61, 67}}),
        makePreset("Bard Metal",
            {{35, 59}, {36, 60}, {41, 62}, {43, 64}, {45, 66}, {47, 68}, {48, 69}, {50, 71}},
            {{38, 68}, {39, 84}, {40, 68}},
            {{49, 78}, {57, 81}},
            {{60, 70}, {61, 67}, {62, 72}, {63, 65}, {64, 74}}),
    };
}
