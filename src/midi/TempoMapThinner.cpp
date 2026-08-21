#include "TempoMapThinner.h"

#include "MidiFile.h"

// Skeleton for Phase 49 - the implementation lands with the v2.3 sprint.
TempoMapThinner::Result TempoMapThinner::thin(MidiFile *file, double toleranceMs,
                                              bool dryRun)
{
    Q_UNUSED(toleranceMs);
    Q_UNUSED(dryRun);
    Result r;
    if (!file) {
        r.error = QStringLiteral("No file loaded.");
        return r;
    }
    r.error = QStringLiteral("Not implemented yet.");
    return r;
}
