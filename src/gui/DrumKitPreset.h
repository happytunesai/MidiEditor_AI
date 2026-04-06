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
};

#endif // DRUMKITPRESET_H_
