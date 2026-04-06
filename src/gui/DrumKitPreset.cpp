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

QList<DrumKitPreset> DrumKitPreset::presets() {
    return {gmPreset(), rockPreset(), jazzPreset()};
}
