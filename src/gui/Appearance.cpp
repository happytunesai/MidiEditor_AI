#include "Appearance.h"
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>
#include <QDateTime>
#include <QFile>
#include <QEvent>
#include <QToolBar>
#include <QToolButton>
#include <QWindow>
#include "../tool/ToolButton.h"
#include "ProtocolWidget.h"
#include "MatrixWidget.h"
#include "AppearanceSettingsWidget.h"
#include "MidiSettingsWidget.h"
#include "InstrumentSettingsWidget.h"
#include "PerformanceSettingsWidget.h"
#include "TrackListWidget.h"
#include "ChannelListWidget.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

// Helper: event filter that applies dark title bar to newly shown windows
#ifdef Q_OS_WIN
class DarkTitleBarFilter : public QObject {
public:
    explicit DarkTitleBarFilter(QObject *parent = nullptr) : QObject(parent) {}
    bool dark = false;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::Show) {
            QWidget *w = qobject_cast<QWidget *>(obj);
            if (w && w->isWindow() && w->windowHandle()) {
                const DWORD attr = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE
                BOOL value = dark ? TRUE : FALSE;
                HWND hwnd = reinterpret_cast<HWND>(w->winId());
                DwmSetWindowAttribute(hwnd, attr, &value, sizeof(value));
            }
        }
        return false;
    }
};
static DarkTitleBarFilter *s_darkTitleBarFilter = nullptr;
#endif

// Prevent rapid successive theme changes
static QDateTime lastThemeChange;

// Simple cache to prevent infinite loops in shouldUseDarkMode
static bool darkModeResult = false;
static QString cachedStyle = "";
static QDateTime cacheTime;

// Queue-based icon processing to prevent crashes
static QTimer *iconUpdateTimer = nullptr;
static QList<QPair<QAction *, QString> > iconUpdateQueue;

// PERFORMANCE: Use QHash instead of QMap for O(1) color lookups instead of O(log n)
QHash<int, QColor *> Appearance::channelColors = QHash<int, QColor *>();
QHash<int, QColor *> Appearance::trackColors = QHash<int, QColor *>();
QSet<int> Appearance::customChannelColors = QSet<int>();
QSet<int> Appearance::customTrackColors = QSet<int>();
QMap<QAction *, QString> Appearance::registeredIconActions = QMap<QAction *, QString>();
int Appearance::_opacity = 100;
Appearance::ColorPreset Appearance::_colorPreset = Appearance::PresetDefault;
Appearance::stripStyle Appearance::_strip = Appearance::onSharp;
bool Appearance::_showRangeLines = false;
bool Appearance::_showVisualizer = true;
QString Appearance::_applicationStyle = "windowsvista";
Appearance::Theme Appearance::_theme = Appearance::ThemeSystem;
int Appearance::_toolbarIconSize = 20;
bool Appearance::_ignoreSystemScaling = false;
bool Appearance::_ignoreFontScaling = false;
bool Appearance::_useRoundedScaling = false;
int Appearance::_msaaSamples = 2;
bool Appearance::_enableVSync = false;
bool Appearance::_useHardwareAcceleration = false;
bool Appearance::_toolbarTwoRowMode = false;
bool Appearance::_toolbarCustomizeEnabled = false;
QStringList Appearance::_toolbarActionOrder = QStringList();
QStringList Appearance::_toolbarEnabledActions = QStringList();
bool Appearance::_smoothPlaybackScrolling = false;
bool Appearance::_showProgramChangeMarkers = false;
bool Appearance::_showControlChangeMarkers = false;
bool Appearance::_showTextEventMarkers = true;
Appearance::MarkerColorMode Appearance::_markerColorMode = Appearance::ColorByTrack;
bool Appearance::_shuttingDown = false;
bool Appearance::_startupComplete = false;

void Appearance::init(QSettings *settings) {
    // CRITICAL: Load application style FIRST before creating any colors
    _opacity = settings->value("appearance_opacity", 100).toInt();
    _colorPreset = static_cast<ColorPreset>(settings->value("color_preset", PresetDefault).toInt());
    _strip = static_cast<Appearance::stripStyle>(settings->value("strip_style", Appearance::onSharp).toInt());
    _showRangeLines = settings->value("show_range_lines", false).toBool();
    _showVisualizer = settings->value("show_visualizer", true).toBool();
    _smoothPlaybackScrolling = settings->value("rendering/smooth_playback_scroll", false).toBool();
    _showProgramChangeMarkers = settings->value("appearance/show_pc_markers", false).toBool();
    _showControlChangeMarkers = settings->value("appearance/show_cc_markers", false).toBool();
    _showTextEventMarkers = settings->value("appearance/show_text_markers", true).toBool();
    _markerColorMode = static_cast<MarkerColorMode>(settings->value("appearance/marker_color_mode", 0).toInt());

    // Set default style with fallback
    QString defaultStyle = "windowsvista";
    QStringList availableStyles = QStyleFactory::keys();
    if (!availableStyles.contains(defaultStyle, Qt::CaseInsensitive)) {
        // Fallback order: windows -> fusion -> first available
        if (availableStyles.contains("windows", Qt::CaseInsensitive)) {
            defaultStyle = "windows";
        } else if (availableStyles.contains("fusion", Qt::CaseInsensitive)) {
            defaultStyle = "fusion";
        } else if (!availableStyles.isEmpty()) {
            defaultStyle = availableStyles.first();
        }
    }
    _applicationStyle = settings->value("application_style", defaultStyle).toString();
    _theme = static_cast<Theme>(settings->value("theme", ThemeSystem).toInt());
    _toolbarIconSize = settings->value("toolbar_icon_size", 20).toInt();
    _ignoreSystemScaling = settings->value("ignore_system_scaling", false).toBool();
    _ignoreFontScaling = settings->value("ignore_font_scaling", false).toBool();
    _useRoundedScaling = settings->value("use_rounded_scaling", false).toBool();
    _toolbarTwoRowMode = settings->value("toolbar_two_row_mode", false).toBool();
    _toolbarCustomizeEnabled = settings->value("toolbar_customize_enabled", false).toBool();
    _toolbarActionOrder = settings->value("toolbar_action_order", QStringList()).toStringList();
    _toolbarEnabledActions = settings->value("toolbar_enabled_actions", QStringList()).toStringList();

    // Load custom color tracking FIRST
    QList<QVariant> customChannels = settings->value("custom_channel_colors", QList<QVariant>()).toList();
    foreach(const QVariant& var, customChannels) {
        int channel = var.toInt();
        customChannelColors.insert(channel);
    }

    QList<QVariant> customTracks = settings->value("custom_track_colors", QList<QVariant>()).toList();
    foreach(const QVariant& var, customTracks) {
        int track = var.toInt();
        customTrackColors.insert(track);
    }

    // Load colors: Custom colors from settings, default colors for current theme
    for (int channel = 0; channel < 17; channel++) {
        if (customChannelColors.contains(channel)) {
            // User has customized this channel - load their custom color from settings
            QColor *customColor = decode("channel_color_" + QString::number(channel),
                                         settings, defaultColor(channel));
            channelColors.insert(channel, customColor);
        } else {
            // User has NOT customized this channel - use current theme's default
            QColor *themeDefaultColor = defaultColor(channel);
            channelColors.insert(channel, themeDefaultColor);
        }
    }

    for (int track = 0; track < 17; track++) {
        if (customTrackColors.contains(track)) {
            // User has customized this track - load their custom color from settings
            QColor *customColor = decode("track_color_" + QString::number(track),
                                         settings, defaultColor(track));
            trackColors.insert(track, customColor);
        } else {
            // User has NOT customized this track - use current theme's default
            QColor *themeDefaultColor = defaultColor(track);
            trackColors.insert(track, themeDefaultColor);
        }
    }

    // Validate custom color markings against actual colors
    QSet<int> invalidCustomChannels;
    foreach(int channel, customChannelColors) {
        QColor *currentColor = channelColors.value(channel, nullptr);
        QColor *defaultColor = Appearance::defaultColor(channel);
        if (currentColor && defaultColor) {
            bool actuallyCustom = (currentColor->red() != defaultColor->red() ||
                                   currentColor->green() != defaultColor->green() ||
                                   currentColor->blue() != defaultColor->blue());
            if (!actuallyCustom) {
                invalidCustomChannels.insert(channel);
            }
        }
        if (defaultColor) delete defaultColor;
    }

    QSet<int> invalidCustomTracks;
    foreach(int track, customTrackColors) {
        QColor *currentColor = trackColors.value(track, nullptr);
        QColor *defaultColor = Appearance::defaultColor(track);
        if (currentColor && defaultColor) {
            bool actuallyCustom = (currentColor->red() != defaultColor->red() ||
                                   currentColor->green() != defaultColor->green() ||
                                   currentColor->blue() != defaultColor->blue());
            if (!actuallyCustom) {
                invalidCustomTracks.insert(track);
            }
        }
        if (defaultColor) delete defaultColor;
    }

    // Remove invalid custom markings
    foreach(int channel, invalidCustomChannels) {
        customChannelColors.remove(channel);
    }
    foreach(int track, invalidCustomTracks) {
        customTrackColors.remove(track);
    }

    // Apply the style after loading settings
    qDebug() << "[STARTUP] Appearance: applyStyle...";
    applyStyle();

    // Force initial color refresh to ensure colors match current theme
    qDebug() << "[STARTUP] Appearance: autoResetDefaultColors...";
    autoResetDefaultColors();

    // Connect to system theme changes
    qDebug() << "[STARTUP] Appearance: connectToSystemThemeChanges...";
    connectToSystemThemeChanges();
    qDebug() << "[STARTUP] Appearance: init complete";
}

QColor *Appearance::channelColor(int channel) {
    int index = channelToColorIndex(channel);

    // Get existing color or create default for current theme
    QColor *color = channelColors.value(index, nullptr);
    if (!color) {
        color = defaultColor(index);
        channelColors[index] = color;
    }

    // PERFORMANCE: Apply opacity setting to the color only if it has changed
    // This avoids expensive setAlpha() calls during rendering
    int targetAlpha = _opacity * 255 / 100;
    if (color->alpha() != targetAlpha) {
        color->setAlpha(targetAlpha);
    }

    return color;
}

QColor *Appearance::trackColor(int track) {
    int index = trackToColorIndex(track);

    // Get existing color or create default for current theme
    QColor *color = trackColors.value(index, nullptr);
    if (!color) {
        color = defaultColor(index);
        trackColors[index] = color;
    }

    // PERFORMANCE: Apply opacity setting to the color only if it has changed
    // This avoids expensive setAlpha() calls during rendering
    int targetAlpha = _opacity * 255 / 100;
    if (color->alpha() != targetAlpha) {
        color->setAlpha(targetAlpha);
    }

    return color;
}

void Appearance::writeSettings(QSettings *settings) {
    for (int channel = 0; channel < 17; channel++) {
        QColor *color = channelColors.value(channel, nullptr);
        if (color) {
            write("channel_color_" + QString::number(channel), settings, color);
        }
    }
    for (int track = 0; track < 17; track++) {
        QColor *color = trackColors.value(track, nullptr);
        if (color) {
            write("track_color_" + QString::number(track), settings, color);
        }
    }
    settings->setValue("appearance_opacity", _opacity);
    settings->setValue("color_preset", static_cast<int>(_colorPreset));
    settings->setValue("strip_style", _strip);
    settings->setValue("show_range_lines", _showRangeLines);
    settings->setValue("show_visualizer", _showVisualizer);
    settings->setValue("rendering/smooth_playback_scroll", _smoothPlaybackScrolling);
    settings->setValue("appearance/show_pc_markers", _showProgramChangeMarkers);
    settings->setValue("appearance/show_cc_markers", _showControlChangeMarkers);
    settings->setValue("appearance/show_text_markers", _showTextEventMarkers);
    settings->setValue("appearance/marker_color_mode", static_cast<int>(_markerColorMode));
    settings->setValue("application_style", _applicationStyle);
    settings->setValue("theme", static_cast<int>(_theme));
    settings->setValue("toolbar_icon_size", _toolbarIconSize);
    settings->setValue("ignore_system_scaling", _ignoreSystemScaling);
    settings->setValue("ignore_font_scaling", _ignoreFontScaling);
    settings->setValue("use_rounded_scaling", _useRoundedScaling);
    settings->setValue("toolbar_two_row_mode", _toolbarTwoRowMode);
    settings->setValue("toolbar_customize_enabled", _toolbarCustomizeEnabled);
    settings->setValue("toolbar_action_order", _toolbarActionOrder);
    settings->setValue("toolbar_enabled_actions", _toolbarEnabledActions);

    // Save custom color tracking
    QList<QVariant> customChannels;
    foreach(int channel, customChannelColors) {
        customChannels.append(channel);
    }
    settings->setValue("custom_channel_colors", customChannels);

    QList<QVariant> customTracks;
    foreach(int track, customTrackColors) {
        customTracks.append(track);
    }
    settings->setValue("custom_track_colors", customTracks);
}

QColor *Appearance::defaultColor(int n) {
    bool useDarkMode = shouldUseDarkMode();

    QColor *color;

    if (useDarkMode) {
        // Darker, more muted colors for dark mode (slightly darker shade)
        switch (n) {
            case 0: {
                color = new QColor(180, 80, 60, 255);
                break;
            }
            case 1: {
                color = new QColor(130, 160, 0, 255);
                break;
            }
            case 2: {
                color = new QColor(25, 130, 5, 255);
                break;
            }
            case 3: {
                color = new QColor(60, 160, 150, 255);
                break;
            }
            case 4: {
                color = new QColor(110, 85, 140, 255);
                break;
            }
            case 5: {
                color = new QColor(160, 80, 130, 255);
                break;
            }
            case 6: {
                color = new QColor(110, 140, 110, 255);
                break;
            }
            case 7: {
                color = new QColor(150, 130, 110, 255);
                break;
            }
            case 8: {
                color = new QColor(160, 130, 5, 255);
                break;
            }
            case 9: {
                color = new QColor(120, 120, 120, 255);
                break;
            }
            case 10: {
                color = new QColor(130, 25, 80, 255);
                break;
            }
            case 11: {
                color = new QColor(70, 120, 200, 255);
                break;
            }
            case 12: {
                color = new QColor(90, 140, 70, 255);
                break;
            }
            case 13: {
                color = new QColor(160, 100, 40, 255);
                break;
            }
            case 14: {
                color = new QColor(60, 15, 60, 255);
                break;
            }
            case 15: {
                color = new QColor(25, 80, 80, 255);
                break;
            }
            default: {
                color = new QColor(80, 120, 200, 255);
                break;
            }
        }
    } else {
        // Original bright colors for light mode
        switch (n) {
            case 0: {
                color = new QColor(241, 70, 57, 255);
                break;
            }
            case 1: {
                color = new QColor(205, 241, 0, 255);
                break;
            }
            case 2: {
                color = new QColor(50, 201, 20, 255);
                break;
            }
            case 3: {
                color = new QColor(107, 241, 231, 255);
                break;
            }
            case 4: {
                color = new QColor(127, 67, 255, 255);
                break;
            }
            case 5: {
                color = new QColor(241, 127, 200, 255);
                break;
            }
            case 6: {
                color = new QColor(170, 212, 170, 255);
                break;
            }
            case 7: {
                color = new QColor(222, 202, 170, 255);
                break;
            }
            case 8: {
                color = new QColor(241, 201, 20, 255);
                break;
            }
            case 9: {
                color = new QColor(80, 80, 80, 255);
                break;
            }
            case 10: {
                color = new QColor(202, 50, 127, 255);
                break;
            }
            case 11: {
                color = new QColor(0, 132, 255, 255);
                break;
            }
            case 12: {
                color = new QColor(102, 127, 37, 255);
                break;
            }
            case 13: {
                color = new QColor(241, 164, 80, 255);
                break;
            }
            case 14: {
                color = new QColor(107, 30, 107, 255);
                break;
            }
            case 15: {
                color = new QColor(50, 127, 127, 255);
                break;
            }
            default: {
                color = new QColor(50, 50, 255, 255);
                break;
            }
        }
    }

    return color;
}

QColor *Appearance::decode(QString name, QSettings *settings, QColor *defaultColor) {
    bool ok;
    int r = settings->value(name + "_r").toInt(&ok);
    if (!ok) {
        return new QColor(*defaultColor);
    }
    int g = settings->value(name + "_g").toInt(&ok);
    if (!ok) {
        return defaultColor;
    }
    int b = settings->value(name + "_b").toInt(&ok);
    if (!ok) {
        return defaultColor;
    }
    return new QColor(r, g, b);
}

void Appearance::write(QString name, QSettings *settings, QColor *color) {
    settings->setValue(name + "_r", QVariant(color->red()));
    settings->setValue(name + "_g", QVariant(color->green()));
    settings->setValue(name + "_b", QVariant(color->blue()));
}

void Appearance::setTrackColor(int track, QColor color) {
    int index = trackToColorIndex(track);
    QColor *oldColor = trackColors.value(index, nullptr);

    // Apply current opacity setting to the new color
    color.setAlpha(_opacity * 255 / 100);
    trackColors[index] = new QColor(color);

    // Only mark as custom if the color is different from current theme default
    QColor *currentDefault = defaultColor(index);
    bool isCustomColor = (currentDefault->red() != color.red() ||
                          currentDefault->green() != color.green() ||
                          currentDefault->blue() != color.blue());

    if (isCustomColor) {
        customTrackColors.insert(index);
    } else {
        customTrackColors.remove(index);
    }

    delete currentDefault; // Clean up temporary color
    if (oldColor) {
        delete oldColor;
    }
}

void Appearance::setChannelColor(int channel, QColor color) {
    int index = channelToColorIndex(channel);
    QColor *oldColor = channelColors.value(index, nullptr);

    // Apply current opacity setting to the new color
    color.setAlpha(_opacity * 255 / 100);
    channelColors[index] = new QColor(color);

    // Only mark as custom if the color is different from current theme default
    QColor *currentDefault = defaultColor(index);
    bool isCustomColor = (currentDefault->red() != color.red() ||
                          currentDefault->green() != color.green() ||
                          currentDefault->blue() != color.blue());

    if (isCustomColor) {
        customChannelColors.insert(index);
    } else {
        customChannelColors.remove(index);
    }

    delete currentDefault; // Clean up temporary color
    if (oldColor) {
        delete oldColor;
    }
}

int Appearance::trackToColorIndex(int track) {
    int mod = (track - 1) % 17;
    if (mod < 0) {
        mod += 17;
    }
    return mod;
}

int Appearance::channelToColorIndex(int channel) {
    if (channel > 16) {
        channel = 16;
    }
    return channel;
}

void Appearance::reset() {
    // Reset to appropriate colors for current mode (light/dark)
    forceResetAllColors();
    customChannelColors.clear(); // Clear custom color tracking - all colors are now "default"
    customTrackColors.clear(); // Clear custom color tracking - all colors are now "default"
}

void Appearance::autoResetDefaultColors() {
    // Update ONLY non-custom colors to match current theme
    // Custom colors are NEVER changed - they stay exactly as the user set them
    int channelsUpdated = 0;
    for (int channel = 0; channel < 17; channel++) {
        if (!customChannelColors.contains(channel)) {
            // This is a DEFAULT color - update it to match current theme
            QColor *existingColor = channelColors.value(channel, nullptr);
            if (existingColor) {
                QColor oldColor = *existingColor; // Save for logging
                QColor *newThemeDefault = defaultColor(channel);

                // Copy the new theme default values to existing color object
                existingColor->setRgb(newThemeDefault->red(),
                                      newThemeDefault->green(),
                                      newThemeDefault->blue(),
                                      newThemeDefault->alpha());

                // Clean up the temporary color
                delete newThemeDefault;

                channelsUpdated++;
            }
        }
    }

    int tracksUpdated = 0;
    for (int track = 0; track < 17; track++) {
        if (!customTrackColors.contains(track)) {
            // This is a DEFAULT color - update it to match current theme
            QColor *existingColor = trackColors.value(track, nullptr);
            if (existingColor) {
                QColor oldColor = *existingColor; // Save for logging
                QColor *newThemeDefault = defaultColor(track);

                // Copy the new theme default values to existing color object
                existingColor->setRgb(newThemeDefault->red(),
                                      newThemeDefault->green(),
                                      newThemeDefault->blue(),
                                      newThemeDefault->alpha());

                // Clean up the temporary color
                delete newThemeDefault;

                tracksUpdated++;
            }
        }
    }
}

void Appearance::forceResetAllColors() {
    // Force reset all colors to current theme defaults
    for (int channel = 0; channel < 17; channel++) {
        QColor *existingColor = channelColors.value(channel, nullptr);
        if (existingColor) {
            QColor *newDefaultColor = defaultColor(channel);
            if (newDefaultColor) {
                // Copy the color values safely
                int r = newDefaultColor->red();
                int g = newDefaultColor->green();
                int b = newDefaultColor->blue();
                int a = newDefaultColor->alpha();

                // Delete the temporary color BEFORE modifying existing one
                delete newDefaultColor;

                // Now safely update the existing color
                existingColor->setRgb(r, g, b, a);
            }
        }
    }

    for (int track = 0; track < 17; track++) {
        QColor *existingColor = trackColors.value(track, nullptr);
        if (existingColor) {
            QColor *newDefaultColor = defaultColor(track);
            if (newDefaultColor) {
                // Copy the color values safely
                int r = newDefaultColor->red();
                int g = newDefaultColor->green();
                int b = newDefaultColor->blue();
                int a = newDefaultColor->alpha();

                // Delete the temporary color BEFORE modifying existing one
                delete newDefaultColor;

                // Now safely update the existing color
                existingColor->setRgb(r, g, b, a);
            }
        }
    }
}

QString Appearance::colorPresetName(ColorPreset preset) {
    switch (preset) {
    case PresetDefault:  return "Default";
    case PresetRainbow:  return "Rainbow";
    case PresetNeon:     return "Neon";
    case PresetFire:     return "Fire";
    case PresetOcean:    return "Ocean";
    case PresetPastel:   return "Pastel";
    case PresetSakura:   return "Sakura";
    case PresetAmoled:   return "AMOLED";
    case PresetMaterial: return "Emerald";
    case PresetPunk:     return "Punk";
    default:             return "Unknown";
    }
}

Appearance::ColorPreset Appearance::colorPreset() {
    return _colorPreset;
}

void Appearance::applyColorPreset(ColorPreset preset) {
    _colorPreset = preset;

    if (preset == PresetDefault) {
        forceResetAllColors();
        return;
    }

    QColor colors[17];
    bool dark = shouldUseDarkMode();
    float vScale = dark ? 0.75f : 1.0f;  // slightly muted for dark mode

    switch (preset) {
    case PresetRainbow:
        for (int i = 0; i < 17; i++)
            colors[i] = QColor::fromHsvF(i / 17.0f, 0.85f, 0.85f * vScale);
        break;
    case PresetNeon:
    {
        static const int neon[][3] = {
            {255,0,110}, {0,255,200}, {180,0,255}, {0,200,255},
            {255,240,0}, {255,0,220}, {0,255,100}, {140,0,255},
            {0,255,255}, {255,100,0}, {200,255,0}, {255,0,150},
            {0,150,255}, {255,50,50}, {100,255,0}, {0,220,180},
            {220,0,220}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(neon[i][0], neon[i][1], neon[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(120);
        break;
    }
    case PresetFire:
    {
        static const int fire[][3] = {
            {200,0,0}, {220,50,0}, {240,80,0}, {255,120,0},
            {255,150,0}, {255,180,0}, {255,200,0}, {255,220,30},
            {255,240,60}, {180,30,0}, {160,0,0}, {200,60,0},
            {230,100,0}, {255,160,20}, {140,20,0}, {120,10,0},
            {190,40,0}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(fire[i][0], fire[i][1], fire[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(115);
        break;
    }
    case PresetOcean:
    {
        static const int ocean[][3] = {
            {0,60,180}, {0,120,200}, {0,150,220}, {0,180,200},
            {0,180,160}, {0,200,180}, {0,160,200}, {30,100,220},
            {50,80,200}, {0,130,160}, {20,80,140}, {0,200,220},
            {60,140,200}, {0,160,130}, {0,100,180}, {40,120,160},
            {0,90,200}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(ocean[i][0], ocean[i][1], ocean[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(110);
        break;
    }
    case PresetPastel:
    {
        static const int pastel[][3] = {
            {255,150,150}, {200,240,150}, {150,230,180}, {150,220,240},
            {180,160,240}, {240,170,210}, {190,230,190}, {230,210,180},
            {240,220,150}, {180,180,180}, {220,160,190}, {150,190,240},
            {190,210,160}, {240,190,150}, {200,160,200}, {160,210,210},
            {170,180,230}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(pastel[i][0], pastel[i][1], pastel[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(130);
        break;
    }
    case PresetSakura:
    {
        // Cherry blossom pinks and soft rose tones
        static const int sakura[][3] = {
            {219,112,147}, {255,182,193}, {255,105,180}, {238,130,168},
            {255,160,180}, {205,92,92},   {255,192,203}, {240,128,128},
            {255,140,160}, {218,112,214}, {199,21,133},  {255,174,185},
            {255,110,150}, {220,150,170}, {188,143,143}, {255,130,170},
            {210,105,145}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(sakura[i][0], sakura[i][1], sakura[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(120);
        break;
    }
    case PresetAmoled:
    {
        // Warm orange/amber tones matching AMOLED theme accents
        static const int amoled[][3] = {
            {230,126,34},  {243,156,18},  {211,84,0},    {245,176,65},
            {255,195,0},   {220,118,51},  {186,74,0},    {248,196,113},
            {235,152,78},  {214,137,16},  {176,58,0},    {255,165,0},
            {200,100,20},  {250,180,60},  {190,90,10},   {240,140,40},
            {210,110,30}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(amoled[i][0], amoled[i][1], amoled[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(110);
        break;
    }
    case PresetMaterial:
    {
        // Teal/green material tones matching Material Dark theme
        static const int material[][3] = {
            {4,185,127},   {38,166,154},  {0,150,136},   {77,208,170},
            {0,188,212},   {38,198,157},  {129,199,132}, {0,230,118},
            {100,221,173}, {0,172,193},   {38,150,136},  {76,175,80},
            {0,200,83},    {102,187,106}, {56,142,60},   {0,191,165},
            {85,200,150}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(material[i][0], material[i][1], material[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(110);
        break;
    }
    case PresetPunk:
    {
        // Aggressive punk: hot pinks, electric purples, acid greens
        static const int punk[][3] = {
            {255,0,110},   {190,0,255},   {0,255,65},    {255,0,200},
            {255,255,0},   {130,0,220},   {0,255,180},   {255,50,50},
            {180,255,0},   {255,0,150},   {100,0,200},   {0,200,255},
            {255,100,0},   {200,0,180},   {0,255,120},   {255,50,200},
            {160,0,255}
        };
        for (int i = 0; i < 17; i++)
            colors[i] = QColor(punk[i][0], punk[i][1], punk[i][2]);
        if (dark) for (int i = 0; i < 17; i++)
            colors[i] = colors[i].darker(115);
        break;
    }
    default:
        forceResetAllColors();
        return;
    }

    int alpha = _opacity * 255 / 100;
    for (int i = 0; i < 17; i++) {
        colors[i].setAlpha(alpha);
        setChannelColor(i, colors[i]);
    }
    for (int i = 0; i < 16; i++) {
        setTrackColor(i, colors[i]);
    }
}

int Appearance::opacity() {
    return _opacity;
}

void Appearance::setOpacity(int opacity) {
    _opacity = opacity;

    // Update alpha values for all existing colors
    int alphaValue = _opacity * 255 / 100;

    // Update all channel colors
    for (auto it = channelColors.begin(); it != channelColors.end(); ++it) {
        if (it.value()) {
            it.value()->setAlpha(alphaValue);
        }
    }

    // Update all track colors
    for (auto it = trackColors.begin(); it != trackColors.end(); ++it) {
        if (it.value()) {
            it.value()->setAlpha(alphaValue);
        }
    }
}

Appearance::stripStyle Appearance::strip() {
    return _strip;
}

void Appearance::setStrip(Appearance::stripStyle render) {
    _strip = render;
}

bool Appearance::showRangeLines() {
    return _showRangeLines;
}

void Appearance::setShowRangeLines(bool enabled) {
    _showRangeLines = enabled;
}

bool Appearance::showVisualizer() {
    return _showVisualizer;
}

void Appearance::setShowVisualizer(bool enabled) {
    _showVisualizer = enabled;
}

bool Appearance::smoothPlaybackScrolling() {
    return _smoothPlaybackScrolling;
}

void Appearance::setSmoothPlaybackScrolling(bool enabled) {
    _smoothPlaybackScrolling = enabled;
}

bool Appearance::showProgramChangeMarkers() { return _showProgramChangeMarkers; }
void Appearance::setShowProgramChangeMarkers(bool enabled) { _showProgramChangeMarkers = enabled; }
bool Appearance::showControlChangeMarkers() { return _showControlChangeMarkers; }
void Appearance::setShowControlChangeMarkers(bool enabled) { _showControlChangeMarkers = enabled; }
bool Appearance::showTextEventMarkers() { return _showTextEventMarkers; }
void Appearance::setShowTextEventMarkers(bool enabled) { _showTextEventMarkers = enabled; }
Appearance::MarkerColorMode Appearance::markerColorMode() { return _markerColorMode; }
void Appearance::setMarkerColorMode(MarkerColorMode mode) { _markerColorMode = mode; }

QString Appearance::applicationStyle() {
    return _applicationStyle;
}

void Appearance::setApplicationStyle(const QString &style) {
    // Prevent rapid successive theme changes that might cause crashes
    QDateTime now = QDateTime::currentDateTime();
    if (lastThemeChange.isValid() && lastThemeChange.msecsTo(now) < 500) {
        return;
    }
    lastThemeChange = now;

    _applicationStyle = style;

    // Invalidate cache when style changes
    cachedStyle = "";

    applyStyle();

    // Coalesce refreshes through a single shared debounce timer
    // Implemented as a static in this translation unit
    auto queueRefreshColors = [](int delayMs) {
        static QTimer *s_refreshDebounceTimer = nullptr;
        if (!s_refreshDebounceTimer) {
            s_refreshDebounceTimer = new QTimer();
            s_refreshDebounceTimer->setSingleShot(true);
            QObject::connect(s_refreshDebounceTimer, &QTimer::timeout, []() {
                refreshColors();
            });
        }
        s_refreshDebounceTimer->stop();
        s_refreshDebounceTimer->start(delayMs);
    };

    // Request a single refresh soon (no stacked timers)
    queueRefreshColors(120);
}

int Appearance::toolbarIconSize() {
    return _toolbarIconSize;
}

void Appearance::setToolbarIconSize(int size) {
    _toolbarIconSize = size;
    notifyIconSizeChanged();
}

bool Appearance::ignoreSystemScaling() {
    return _ignoreSystemScaling;
}

void Appearance::setIgnoreSystemScaling(bool ignore) {
    qDebug() << "Appearance: Setting ignore system scaling to" << ignore;
    _ignoreSystemScaling = ignore;
    // Note: This setting requires application restart to take effect
}

bool Appearance::ignoreFontScaling() {
    return _ignoreFontScaling;
}

void Appearance::setIgnoreFontScaling(bool ignore) {
    qDebug() << "Appearance: Setting ignore font scaling to" << ignore;
    _ignoreFontScaling = ignore;
    // Note: This setting requires application restart to take effect
}

bool Appearance::useRoundedScaling() {
    return _useRoundedScaling;
}

void Appearance::setUseRoundedScaling(bool useRounded) {
    qDebug() << "Appearance: Setting use rounded scaling to" << useRounded;
    _useRoundedScaling = useRounded;
    // Note: This setting requires application restart to take effect
}

int Appearance::msaaSamples() {
    return _msaaSamples;
}

bool Appearance::enableVSync() {
    return _enableVSync;
}

bool Appearance::useHardwareAcceleration() {
    return _useHardwareAcceleration;
}

void Appearance::loadEarlySettings() {
    // Load only the settings needed before QApplication is created
    QSettings settings(QString("MidiEditor"), QString("NONE"));
    _ignoreSystemScaling = settings.value("ignore_system_scaling", false).toBool();
    _ignoreFontScaling = settings.value("ignore_font_scaling", false).toBool();
    _useRoundedScaling = settings.value("use_rounded_scaling", false).toBool();
    _msaaSamples = settings.value("rendering/msaa_samples", 2).toInt();
    _enableVSync = settings.value("rendering/enable_vsync", false).toBool();
    _useHardwareAcceleration = settings.value("rendering/hardware_acceleration", false).toBool();

    qDebug() << "Appearance::loadEarlySettings() - Loaded values:";
    qDebug() << "  ignore_system_scaling:" << _ignoreSystemScaling;
    qDebug() << "  ignore_font_scaling:" << _ignoreFontScaling;
    qDebug() << "  use_rounded_scaling:" << _useRoundedScaling;
    qDebug() << "  msaa_samples:" << _msaaSamples;
    qDebug() << "  enable_vsync:" << _enableVSync;
    qDebug() << "  use_hardware_acceleration:" << _useHardwareAcceleration;
}

QFont Appearance::improveFont(const QFont &font) {
    QFont improvedFont = font;
    improvedFont.setHintingPreference(QFont::PreferFullHinting);
    improvedFont.setStyleStrategy(QFont::PreferAntialias);
    return improvedFont;
}

bool Appearance::toolbarTwoRowMode() {
    return _toolbarTwoRowMode;
}

void Appearance::setToolbarTwoRowMode(bool twoRows) {
    _toolbarTwoRowMode = twoRows;
}

bool Appearance::toolbarCustomizeEnabled() {
    return _toolbarCustomizeEnabled;
}

void Appearance::setToolbarCustomizeEnabled(bool enabled) {
    _toolbarCustomizeEnabled = enabled;
}

QStringList Appearance::toolbarActionOrder() {
    return _toolbarActionOrder;
}

void Appearance::setToolbarActionOrder(const QStringList &order) {
    _toolbarActionOrder = order;
}

QStringList Appearance::toolbarEnabledActions() {
    return _toolbarEnabledActions;
}

void Appearance::setToolbarEnabledActions(const QStringList &enabled) {
    _toolbarEnabledActions = enabled;
}

QStringList Appearance::availableStyles() {
    // Only return QWidget styles that actually work with QApplication::setStyle()
    // Qt Quick Controls styles (Material, Universal, FluentWinUI3, etc.) don't work with QWidget applications
    QStringList styles = QStyleFactory::keys();
    styles.sort();
    return styles;
}

void Appearance::applyStyle() {
    QApplication *app = qobject_cast<QApplication *>(QApplication::instance());
    if (!app) return;

    // Apply QWidget base style first — use the user's chosen style
    QString baseStyle = _applicationStyle;
    if (QStyleFactory::keys().contains(baseStyle, Qt::CaseInsensitive)) {
        app->setStyle(baseStyle);
    }

    // Determine which QSS to load
    bool wantDark = false;
    QString qssPath;
    if (_theme == ThemeDark) {
        wantDark = true;
        qssPath = ":/src/gui/themes/dark.qss";
    } else if (_theme == ThemeAmoled) {
        wantDark = true;
        qssPath = ":/src/gui/themes/amoled.qss";
    } else if (_theme == ThemeMaterial) {
        wantDark = true;
        qssPath = ":/src/gui/themes/materialdark.qss";
    } else if (_theme == ThemePink) {
        wantDark = false;
        qssPath = ":/src/gui/themes/pink.qss";
    } else if (_theme == ThemeLight) {
        wantDark = false;
        qssPath = ":/src/gui/themes/light.qss";
    } else if (_theme == ThemeSystem) {
        wantDark = isDarkModeEnabled();
        qssPath = wantDark ? ":/src/gui/themes/dark.qss" : ":/src/gui/themes/light.qss";
    } else {
        // ThemeNone — legacy behaviour, apply only the minimal inline QSS
        if (shouldUseDarkMode()) {
            QString darkStyleSheet =
                    "QToolButton:checked { "
                    "    background-color: rgba(80, 80, 80, 150); "
                    "    border: 1px solid rgba(120, 120, 120, 150); "
                    "} "
                    "QToolBar::separator { "
                    "    background-color: rgba(120, 120, 120, 150); "
                    "    width: 1px; "
                    "    height: 1px; "
                    "    margin: 2px; "
                    "}";
            app->setStyleSheet(darkStyleSheet);
        } else {
            app->setStyleSheet("");
        }
        applyDarkTitleBar(shouldUseDarkMode());
        return;
    }

    // Load the QSS resource (qssPath was set during theme detection above)
    QFile qssFile(qssPath);
    if (qssFile.open(QFile::ReadOnly | QFile::Text)) {
        app->setStyleSheet(QString::fromUtf8(qssFile.readAll()));
        qssFile.close();
    } else {
        qWarning("Appearance: failed to load %s", qPrintable(qssPath));
        app->setStyleSheet("");
    }

    applyDarkTitleBar(wantDark);
}

void Appearance::applyDarkTitleBar(bool dark) {
#ifdef Q_OS_WIN
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 build 18985+)
    const DWORD attr = 20;
    BOOL value = dark ? TRUE : FALSE;

    // Apply to all existing top-level windows
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w->isWindow()) {
            HWND hwnd = reinterpret_cast<HWND>(w->winId());
            DwmSetWindowAttribute(hwnd, attr, &value, sizeof(value));
        }
    }

    // Install/update event filter so future dialogs also get dark title bars
    if (!s_darkTitleBarFilter) {
        s_darkTitleBarFilter = new DarkTitleBarFilter(qApp);
        qApp->installEventFilter(s_darkTitleBarFilter);
    }
    s_darkTitleBarFilter->dark = dark;
#else
    Q_UNUSED(dark);
#endif
}

Appearance::Theme Appearance::theme() {
    return _theme;
}

void Appearance::setTheme(Theme t) {
    // Prevent rapid successive theme changes
    QDateTime now = QDateTime::currentDateTime();
    if (lastThemeChange.isValid() && lastThemeChange.msecsTo(now) < 500) {
        return;
    }
    lastThemeChange = now;

    _theme = t;

    // Invalidate caches
    cachedStyle = "";

    applyStyle();

    // Defer a forced full refresh to the next event-loop iteration so the
    // stylesheet is fully processed before we rebuild toolbar widgets and
    // re-apply dark-mode icon adjustments.  Using singleShot(0) ensures
    // minimal delay while still letting Qt finish style propagation.
    if (_startupComplete) {
        QTimer::singleShot(0, []() {
            Appearance::forceColorRefresh();
        });
    }
}

void Appearance::setThemeValue(Theme t) {
    _theme = t;
}

void Appearance::notifyIconSizeChanged() {
    // Skip during startup — the toolbar is built once in createCustomToolbar()
    if (!_startupComplete) return;

    // Find the main window and trigger a toolbar rebuild to apply new icon size
    QApplication *app = qobject_cast<QApplication *>(QApplication::instance());
    if (!app) return;

    // Find all top-level widgets and look for the main window
    foreach(QWidget* widget, app->topLevelWidgets()) {
        if (widget->objectName() == "MainWindow" || widget->inherits("MainWindow")) {
            // Call the MainWindow's method to rebuild toolbar with new icon size
            QMetaObject::invokeMethod(widget, "rebuildToolbarFromSettings", Qt::QueuedConnection);
            break;
        }
    }
}

// Dark mode detection and color scheme methods
bool Appearance::isDarkModeEnabled() {
    // Use Qt's built-in dark mode detection
    QStyleHints *hints = QApplication::styleHints();
    Qt::ColorScheme scheme = hints->colorScheme();
    bool isDark = (scheme == Qt::ColorScheme::Dark);
    return isDark;
}

bool Appearance::shouldUseDarkMode() {
    // If using the new theme system, consult theme directly
    if (_theme == ThemeDark || _theme == ThemeAmoled || _theme == ThemeMaterial) {
        darkModeResult = true;
        cachedStyle = _applicationStyle.toLower();
        return true;
    }
    if (_theme == ThemeLight || _theme == ThemePink) {
        darkModeResult = false;
        cachedStyle = _applicationStyle.toLower();
        return false;
    }
    if (_theme == ThemeSystem) {
        bool sys = isDarkModeEnabled();
        darkModeResult = sys;
        cachedStyle = _applicationStyle.toLower();
        return sys;
    }

    // ThemeNone — legacy logic based on application style
    QString style = _applicationStyle.toLower();

    // Simple cache check - no expensive DateTime operations
    if (style == cachedStyle && !cachedStyle.isEmpty()) {
        return darkModeResult;
    }

    // Update cache
    cachedStyle = style;

    if (style == "windowsvista") {
        darkModeResult = false;
        return false;
    }

    if (style == "windows11" || style == "windows" || style == "fusion") {
        bool systemDarkMode = isDarkModeEnabled();
        darkModeResult = systemDarkMode;
        return systemDarkMode;
    }

    darkModeResult = false;
    return false;
}

// Color scheme methods
QColor Appearance::backgroundColor() {
    if (_theme == ThemePink) return QColor(160, 130, 140);
    if (shouldUseDarkMode()) {
        return QColor(45, 45, 45); // Dark gray
    }
    return Qt::darkGray; // Original Qt color for light mode
}

QColor Appearance::backgroundShade() {
    if (_theme == ThemePink) return QColor(200, 180, 190);
    if (shouldUseDarkMode()) {
        return QColor(60, 60, 60); // Dark gray shade
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::foregroundColor() {
    if (shouldUseDarkMode()) {
        return QColor(255, 255, 255); // White
    }
    return Qt::black; // Original Qt color for light mode
}

QColor Appearance::lightGrayColor() {
    if (shouldUseDarkMode()) {
        return QColor(150, 150, 150); // Light gray for dark mode
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::darkGrayColor() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Dark gray for dark mode
    }
    return Qt::darkGray; // Original Qt color for light mode
}

QColor Appearance::grayColor() {
    if (shouldUseDarkMode()) {
        return QColor(128, 128, 128); // Medium gray for dark mode
    }
    return Qt::gray; // Original Qt color for light mode
}

QColor Appearance::pianoWhiteKeyColor() {
    if (_theme == ThemePink) return QColor(255, 240, 245); // Lavender blush - slightly pink
    if (shouldUseDarkMode()) {
        return QColor(170, 170, 170); // Lighter gray for dark mode piano keys
    }
    return Qt::white;
}

QColor Appearance::pianoBlackKeyColor() {
    if (_theme == ThemePink) return QColor(80, 40, 55); // Dark rose for Sakura
    if (shouldUseDarkMode()) {
        return Qt::black; // Keep black keys black in dark mode
    }
    return Qt::black;
}

QColor Appearance::pianoWhiteKeyHoverColor() {
    if (_theme == ThemePink) return QColor(240, 180, 200); // Pink hover
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Dark gray for hover in dark mode
    }
    return Qt::darkGray; // Original Qt color for light mode
}

QColor Appearance::pianoBlackKeyHoverColor() {
    if (_theme == ThemePink) return QColor(180, 120, 150); // Rose hover
    if (shouldUseDarkMode()) {
        return QColor(150, 150, 150); // Light gray for hover in dark mode
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::pianoWhiteKeySelectedColor() {
    if (_theme == ThemePink) return QColor(245, 200, 215); // Pink selected
    if (shouldUseDarkMode()) {
        return QColor(150, 150, 150); // Light gray for selected in dark mode
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::pianoBlackKeySelectedColor() {
    if (_theme == ThemePink) return QColor(160, 100, 130); // Rose selected
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Dark gray for selected in dark mode
    }
    return Qt::darkGray; // Original Qt color for light mode
}

QColor Appearance::stripHighlightColor() {
    if (_theme == ThemePink) return QColor(255, 230, 240);
    if (shouldUseDarkMode()) {
        return QColor(70, 70, 70); // Dark gray
    }
    return QColor(234, 246, 255); // Light blue
}

QColor Appearance::stripNormalColor() {
    if (_theme == ThemePink) return QColor(255, 210, 225);
    if (shouldUseDarkMode()) {
        return QColor(55, 55, 55); // Darker gray
    }
    return QColor(194, 230, 255); // Light blue (original color)
}

QColor Appearance::rangeLineColor() {
    if (shouldUseDarkMode()) {
        return QColor(120, 105, 85); // Brighter cream for dark mode
    }
    return QColor(255, 239, 194); // Light cream
}

QColor Appearance::velocityBackgroundColor() {
    if (shouldUseDarkMode()) {
        return QColor(60, 75, 85); // Dark blue-gray
    }
    return QColor(234, 246, 255); // Light blue
}

QColor Appearance::velocityGridColor() {
    if (shouldUseDarkMode()) {
        return QColor(80, 95, 105); // Lighter blue-gray
    }
    return QColor(194, 230, 255); // Light blue
}

QColor Appearance::systemTextColor() {
    if (shouldUseDarkMode()) {
        return foregroundColor(); // Use main foreground color
    }
    return QApplication::palette().windowText().color();
}

QColor Appearance::systemWindowColor() {
    if (shouldUseDarkMode()) {
        return QColor(35, 35, 35); // Darker background for time area in dark mode
    }
    return QApplication::palette().window().color(); // Original system window color
}

QColor Appearance::infoBoxBackgroundColor() {
    if (shouldUseDarkMode()) {
        return QColor(60, 60, 60); // Dark gray
    }
    return Qt::white;
}

QColor Appearance::infoBoxTextColor() {
    if (shouldUseDarkMode()) {
        return QColor(200, 200, 200); // Light gray
    }
    return Qt::gray;
}

QColor Appearance::toolbarBackgroundColor() {
    if (_theme == ThemeDark || (_theme == ThemeSystem && isDarkModeEnabled())) {
        return QColor(22, 27, 34); // #161b22 matches dark.qss
    } else if (_theme == ThemeLight || (_theme == ThemeSystem && !isDarkModeEnabled())) {
        return QColor(246, 248, 250); // #f6f8fa matches light.qss
    }
    // ThemeNone / legacy
    if (shouldUseDarkMode()) {
        return QColor(70, 70, 70);
    }
    return Qt::white;
}

QString Appearance::toolbarInlineStyle() {
    if (_theme == ThemeNone) {
        return "QToolBar { border: 0px }";
    }
    if (_theme == ThemePink) {
        return "QToolBar { border: 0px; background-color: #fff0f5; } "
               "QToolButton { color: #4a2040; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; } "
               "QToolButton:hover { background-color: #ffe0eb; border: 1px solid #f0b0c8; } "
               "QToolButton:pressed { background-color: #f0b0c8; } "
               "QToolButton:checked { background-color: rgba(219, 112, 147, 0.15); border: 1px solid #db7093; }";
    }
    if (_theme == ThemeAmoled) {
        return "QToolBar { border: 0px; background-color: #000000; } "
               "QToolButton { color: #e6edf3; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; } "
               "QToolButton:hover { background-color: #111111; border: 1px solid #1a1a1a; } "
               "QToolButton:pressed { background-color: #5a3510; } "
               "QToolButton:checked { background-color: rgba(230, 126, 34, 0.15); border: 1px solid #e67e22; }";
    }
    if (_theme == ThemeMaterial) {
        return "QToolBar { border: 0px; background-color: #252430; } "
               "QToolButton { color: #e6edf3; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; } "
               "QToolButton:hover { background-color: #2a2935; border: 1px solid #35343d; } "
               "QToolButton:pressed { background-color: #0d5e42; } "
               "QToolButton:checked { background-color: rgba(4, 185, 127, 0.15); border: 1px solid #04b97f; }";
    }
    if (shouldUseDarkMode()) {
        return "QToolBar { border: 0px; background-color: #161b22; } "
               "QToolButton { color: #e6edf3; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; } "
               "QToolButton:hover { background-color: #1c2129; border: 1px solid #30363d; } "
               "QToolButton:pressed { background-color: #264f78; } "
               "QToolButton:checked { background-color: rgba(88, 166, 255, 0.15); border: 1px solid #58a6ff; }";
    }
    return "QToolBar { border: 0px; background-color: #f6f8fa; } "
           "QToolButton { color: #1f2328; background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; } "
           "QToolButton:hover { background-color: #eaeef2; border: 1px solid #d0d7de; } "
           "QToolButton:pressed { background-color: #d0d7de; } "
           "QToolButton:checked { background-color: rgba(9, 105, 218, 0.12); border: 1px solid #0969da; }";
}

QString Appearance::listBorderStyle() {
    if (_theme == ThemeNone) {
        return "QListWidget::item { border-bottom: 1px solid lightGray; }";
    }
    if (_theme == ThemePink) {
        return "QListWidget::item { border-bottom: 1px solid #f0c0d0; }";
    }
    if (_theme == ThemeAmoled) {
        return "QListWidget::item { border-bottom: 1px solid #1a1a1a; }";
    }
    if (_theme == ThemeMaterial) {
        return "QListWidget::item { border-bottom: 1px solid #35343d; }";
    }
    if (shouldUseDarkMode()) {
        return "QListWidget::item { border-bottom: 1px solid #30363d; }";
    }
    return "QListWidget::item { border-bottom: 1px solid #d0d7de; }";
}

QColor Appearance::borderColor() {
    if (shouldUseDarkMode()) {
        return QColor(80, 80, 80); // Darker gray for dark mode (matches darker track colors)
    }
    return Qt::gray;
}

QColor Appearance::borderColorAlt() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Medium gray for dark mode
    }
    return Qt::lightGray;
}

QColor Appearance::selectionBorderColor() {
    if (shouldUseDarkMode()) {
        return QColor(80, 80, 80); // Darker gray for dark mode (matches main border)
    }
    return Qt::lightGray; // Original light gray for light mode
}

QColor Appearance::errorColor() {
    if (shouldUseDarkMode()) {
        return QColor(200, 80, 80); // Lighter red for dark mode
    }
    return Qt::red;
}

QColor Appearance::cursorLineColor() {
    if (shouldUseDarkMode()) {
        return QColor(150, 150, 150); // Light gray for dark mode
    }
    return Qt::darkGray; // Original Qt color for light mode
}

QColor Appearance::cursorTriangleColor() {
    if (shouldUseDarkMode()) {
        return QColor(80, 95, 105); // Dark blue-gray for dark mode
    }
    return QColor(194, 230, 255); // Light blue for light mode
}

QColor Appearance::tempoToolHighlightColor() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Medium gray for dark mode
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::measureToolHighlightColor() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Medium gray for dark mode
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::timeSignatureToolHighlightColor() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Medium gray for dark mode
    }
    return Qt::lightGray; // Original Qt color for light mode
}

QColor Appearance::pianoKeyLineHighlightColor() {
    if (_theme == ThemePink) return QColor(219, 112, 147, 60); // Pale violet red highlight for Sakura
    if (shouldUseDarkMode()) {
        return QColor(80, 120, 160, 80); // Much brighter blue with higher opacity for dark mode
    }
    return QColor(0, 0, 100, 40); // Blue with transparency for light mode (original)
}

QColor Appearance::measureTextColor() {
    if (shouldUseDarkMode()) {
        return QColor(200, 200, 200); // Light gray for dark mode
    }
    return Qt::white; // White text on measure bars (original)
}

QColor Appearance::measureBarColor() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Medium gray for dark mode
    }
    return Qt::lightGray; // Original measure bar color
}

QColor Appearance::measureLineColor() {
    if (shouldUseDarkMode()) {
        return QColor(120, 120, 120); // Medium gray for dark mode
    }
    return Qt::gray; // Original measure line color
}

QColor Appearance::timelineGridColor() {
    if (shouldUseDarkMode()) {
        return QColor(100, 100, 100); // Medium gray for dark mode
    }
    return Qt::lightGray; // Original timeline grid color
}

QColor Appearance::playbackCursorColor() {
    if (shouldUseDarkMode()) {
        return QColor(200, 80, 80); // More muted red for dark mode
    }
    return Qt::red; // Original red playback cursor
}

QColor Appearance::recordingIndicatorColor() {
    if (shouldUseDarkMode()) {
        return QColor(255, 100, 100); // Lighter red for dark mode
    }
    return Qt::red; // Original red recording indicator
}

QColor Appearance::programEventHighlightColor() {
    if (shouldUseDarkMode()) {
        return QColor(60, 70, 90); // Darker blue for dark mode (different from strip)
    }
    return QColor(234, 246, 255); // Light blue (original)
}

QColor Appearance::programEventNormalColor() {
    if (shouldUseDarkMode()) {
        return QColor(45, 55, 70); // Darker blue-gray for dark mode (different from strip)
    }
    return QColor(194, 194, 194); // Light gray (original)
}

QColor Appearance::noteSelectionColor() {
    if (shouldUseDarkMode()) {
        // In dark mode, use a darker version of the track color with some transparency
        // This allows the track color to show through while indicating selection
        return QColor(60, 80, 120, 150); // Dark blue with transparency
    }
    return Qt::darkBlue; // Original selection color for light mode
}

QPixmap Appearance::adjustIconForDarkMode(const QPixmap &original, const QString &iconName) {
    // Prevent QPixmap operations during application shutdown
    if (_shuttingDown) {
        return QPixmap(); // Return empty pixmap to avoid crashes
    }

    if (!shouldUseDarkMode()) {
        return original.copy();
    }

    // List of icons that don't need color adjustment (they're not black or are intentionally colored)
    QStringList skipIcons = {"load", "new", "redo", "undo", "save", "saveas", "stop_record", "icon", "midieditor",
                             "explode_chords_to_tracks", "channel_split_28", "ffxiv_fix", "midipilot"};

    // Extract just the filename from the path for comparison
    QString fileName = iconName;
    if (fileName.contains("/")) {
        fileName = fileName.split("/").last();
    }
    if (fileName.contains(".")) {
        fileName = fileName.split(".").first();
    }

    // Skip adjustment for non-black icons
    if (skipIcons.contains(fileName)) {
        return original.copy();
    }

    // Force a deep detach copy with copy() to defeat any internal shared cache pollution
    QPixmap adjusted = original.copy();
    QPainter painter(&adjusted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    painter.fillRect(adjusted.rect(), QColor(180, 180, 180)); // Light gray for dark mode
    painter.end();
    return adjusted;
}

QIcon Appearance::adjustIconForDarkMode(const QString &iconPath) {
    // Prevent QPixmap operations during application shutdown
    if (_shuttingDown) {
        return QIcon(); // Return empty icon to avoid crashes
    }

    // Bypass QPixmapCache to ensure we get pristine, unmodified icon files.
    // Qt caches QPixmap loads by filename, which can poison dynamic colors on theme flips.
    QImage img;
    if (!img.load(iconPath)) {
        return QIcon(); // Return empty icon instead of crashing
    }

    QPixmap original = QPixmap::fromImage(img);

    // Extract filename from path for icon name detection
    QString fileName = iconPath;
    if (fileName.contains("/")) {
        fileName = fileName.split("/").last();
    }
    if (fileName.contains(".")) {
        fileName = fileName.split(".").first();
    }

    QPixmap adjusted = adjustIconForDarkMode(original, fileName);
    return QIcon(adjusted);
}

void Appearance::refreshAllIcons() {
    // Check if registeredIconActions is in a valid state
    if (registeredIconActions.isEmpty()) {
        return;
    }

    // Update all icons immediately - no cleanup needed since actions auto-unregister - destroyed() signal handles removal
    for (auto it = registeredIconActions.begin(); it != registeredIconActions.end(); ++it) {
        QAction *action = it.key();
        const QString &iconPath = it.value();

        if (action) {
            // Defeat QIcon/QToolButton internal caching by explicitly setting a null icon first
            action->setIcon(QIcon());
            // Update the icon immediately
            QIcon newIcon = adjustIconForDarkMode(iconPath);
            action->setIcon(newIcon);
        }
    }
}

void Appearance::startQueuedIconProcessing() {
    // Create timer for processing one icon per tick
    iconUpdateTimer = new QTimer();
    iconUpdateTimer->setSingleShot(false);
    iconUpdateTimer->setInterval(10); // Very slow timer - 10ms per icon for maximum safety

    QObject::connect(iconUpdateTimer, &QTimer::timeout, []() {
        processNextQueuedIcon();
    });

    iconUpdateTimer->start();
}

void Appearance::processNextQueuedIcon() {
    if (iconUpdateQueue.isEmpty()) {
        // All icons processed
        if (iconUpdateTimer) {
            iconUpdateTimer->stop();
            iconUpdateTimer->deleteLater();
            iconUpdateTimer = nullptr;
        }
        return;
    }

    // Process only ONE icon per timer tick to prevent crashes
    QPair<QAction *, QString> iconPair = iconUpdateQueue.takeFirst();
    QAction *action = iconPair.first;
    QString iconPath = iconPair.second;

    if (!action) {
        return; // Skip null actions
    }

    // Verify action is still valid and update icon
    try {
        // Test if action is still valid
        action->objectName();

        // Update the icon
        QIcon newIcon = adjustIconForDarkMode(iconPath);
        action->setIcon(newIcon);
    } catch (...) {
        // Action is invalid or update failed, just skip it
        // Don't crash the entire process
    }
}

void Appearance::registerIconAction(QAction *action, const QString &iconPath) {
    if (!action) return;

    // Check if already registered
    auto existing = registeredIconActions.find(action);
    if (existing != registeredIconActions.end()) {
        // Already registered - just update icon path if different
        if (existing.value() != iconPath) {
            existing.value() = iconPath;
        }
        return;
    }

    // New registration - add to map and connect destruction signal
    registeredIconActions[action] = iconPath;

    // Auto-unregister when action is destroyed
    QObject::connect(action, &QObject::destroyed, [action]() {
        registeredIconActions.remove(action);
    });
}

void Appearance::setActionIcon(QAction *action, const QString &iconPath) {
    if (action) {
        action->setIcon(adjustIconForDarkMode(iconPath));
        registerIconAction(action, iconPath);
    }
}

void Appearance::refreshColors(bool force) {
    qDebug() << "[STARTUP] refreshColors() called  force=" << force;

    // Skip heavy refresh during initial construction — the toolbar and widgets
    // are not yet fully built, so iterating them here can cause reentrancy or
    // layout issues on some system configurations.
    if (!_startupComplete) {
        qDebug() << "[STARTUP] refreshColors() skipped (startup not complete)";
        return;
    }

    // Prevent cascading toolbar updates during style changes
    static bool isRefreshingColors = false;
    static QDateTime lastRefresh;
    QDateTime now = QDateTime::currentDateTime();

    if (isRefreshingColors) {
        return; // Already refreshing, prevent recursion
    }

    // Debounce rapid refresh calls — skipped when force==true (theme switch)
    if (!force && lastRefresh.isValid() && lastRefresh.msecsTo(now) < 200) {
        return; // Too soon since last refresh
    }

    isRefreshingColors = true;
    lastRefresh = now;

    // Determine if effective color scheme (light/dark) actually changed
    bool newScheme = shouldUseDarkMode();
    static int s_lastScheme = -1; // -1 unknown, 0 light, 1 dark
    bool schemeChanged = (s_lastScheme == -1) || (s_lastScheme != (newScheme ? 1 : 0));
    s_lastScheme = newScheme ? 1 : 0;

    // Detect application style name change (e.g., windowsvista -> windows11)
    static QString s_lastStyleName = QString();
    bool styleNameChanged = (s_lastStyleName.isEmpty() || s_lastStyleName != _applicationStyle);
    s_lastStyleName = _applicationStyle;

    try {
        // Auto-reset default colors for the new theme
        autoResetDefaultColors();
    } catch (...) {
        // Color reset failed - this might be the crash source
        // Continue with other updates but skip color reset
    }

    // Update all widgets
    QApplication *app = qobject_cast<QApplication *>(QApplication::instance());
    if (!app) {
        return;
    }

    // Update all top-level widgets with crash protection
    foreach(QWidget* widget, app->topLevelWidgets()) {
        if (widget->isVisible()) {
            // Only do full style unpolish/polish if the actual application style changed
            if (styleNameChanged) {
                try {
                    widget->style()->unpolish(widget);
                    widget->style()->polish(widget);
                } catch (...) {
                    // If style repolish fails for some widget, skip it
                }
            }
            // Always update to schedule a repaint, but avoid redundant unpolish/polish
            try { widget->update(); } catch (...) {}

            // Refresh all ToolButton icons for theme changes
            QList<ToolButton *> toolButtons = widget->findChildren<ToolButton *>();
            foreach(ToolButton* toolButton, toolButtons) {
                toolButton->refreshIcon();
            }

            // Refresh all ProtocolWidget colors for theme changes
            QList<ProtocolWidget *> protocolWidgets = widget->findChildren<ProtocolWidget *>();
            foreach(ProtocolWidget* protocolWidget, protocolWidgets) {
                protocolWidget->refreshColors();
            }

            // Refresh all AppearanceSettingsWidget colors for theme changes
            QList<AppearanceSettingsWidget *> appearanceWidgets = widget->findChildren<AppearanceSettingsWidget *>();
            foreach(AppearanceSettingsWidget* appearanceWidget, appearanceWidgets) {
                appearanceWidget->refreshColors();
            }

            // Refresh all MidiSettingsWidget colors for theme changes
            QList<MidiSettingsWidget *> midiWidgets = widget->findChildren<MidiSettingsWidget *>();
            foreach(MidiSettingsWidget* midiWidget, midiWidgets) {
                midiWidget->refreshColors();
            }

            // Refresh all AdditionalMidiSettingsWidget colors for theme changes
            QList<AdditionalMidiSettingsWidget *> additionalMidiWidgets = widget->findChildren<AdditionalMidiSettingsWidget *>();
            foreach(AdditionalMidiSettingsWidget* additionalMidiWidget, additionalMidiWidgets) {
                additionalMidiWidget->refreshColors();
            }

            // Refresh all InstrumentSettingsWidget colors for theme changes
            QList<InstrumentSettingsWidget *> instrumentWidgets = widget->findChildren<InstrumentSettingsWidget *>();
            foreach(InstrumentSettingsWidget* instrumentWidget, instrumentWidgets) {
                instrumentWidget->refreshColors();
            }

            // Refresh all PerformanceSettingsWidget colors for theme changes
            QList<PerformanceSettingsWidget *> performanceWidgets = widget->findChildren<PerformanceSettingsWidget *>();
            foreach(PerformanceSettingsWidget* performanceWidget, performanceWidgets) {
                performanceWidget->refreshColors();
            }

            // Refresh all TrackListWidget colors for theme changes
            QList<TrackListWidget *> trackListWidgets = widget->findChildren<TrackListWidget *>();
            foreach(TrackListWidget* trackListWidget, trackListWidgets) {
                trackListWidget->refreshColors();
            }

            // Refresh all ChannelListWidget colors for theme changes
            QList<ChannelListWidget *> channelListWidgets = widget->findChildren<ChannelListWidget *>();
            foreach(ChannelListWidget* channelListWidget, channelListWidgets) {
                channelListWidget->refreshColors();
            }

            // Refresh all LayoutSettingsWidget icons for theme changes
            QList<QWidget *> layoutSettingsWidgets = widget->findChildren<QWidget *>("LayoutSettingsWidget");
            foreach(QWidget* layoutWidget, layoutSettingsWidgets) {
                // Call refreshIcons method if it exists
                QMetaObject::invokeMethod(layoutWidget, "refreshIcons", Qt::DirectConnection);
            }

            // Refresh toolbar icons for widgets that support it
            if (widget->objectName() == "MainWindow" || widget->inherits("MainWindow")) {
                // MainWindow has refreshToolbarIcons method
                QMetaObject::invokeMethod(widget, "refreshToolbarIcons", Qt::DirectConnection);
            } else if (widget->objectName() == "SettingsDialog" || widget->inherits("SettingsDialog")) {
                // SettingsDialog has its own refreshToolbarIcons method
                QMetaObject::invokeMethod(widget, "refreshToolbarIcons", Qt::DirectConnection);
            }

            // Schedule a repaint — avoid inline processEvents() to prevent
            // reentrant event-loop problems during layout operations.
        }
    }

    // ── Icon & Toolbar refresh ──────────────────────────────────────────
    // Order matters!  We must:
    //   1) Rebuild the toolbar (creates fresh QToolButton widgets)
    //   2) THEN refresh icons on the QActions (so the new buttons pick them up)
    //   3) THEN poke every QToolButton to re-read its action's icon
    // Doing it in any other order means stale pixmaps survive the rebuild.

    // Step 1 — Rebuild the toolbar synchronously so new widgets exist.
    if ((schemeChanged || force) && _startupComplete) {
        foreach(QWidget* widget, app->topLevelWidgets()) {
            if (widget->objectName() == "MainWindow" || widget->inherits("MainWindow")) {
                QMetaObject::invokeMethod(widget, "rebuildToolbarFromSettings", Qt::DirectConnection);
                break;
            }
        }
    }

    // Step 2 — Refresh every registered QAction icon (loads fresh from disk
    //          using QImage to bypass QPixmapCache).
    refreshAllIcons();

    // Step 3 — Force every visible QToolButton to re-read its QAction icon.
    //          This defeats Qt's internal icon-render cache.
    foreach(QWidget* widget, app->topLevelWidgets()) {
        QList<QToolButton *> toolButtons = widget->findChildren<QToolButton *>();
        foreach(QToolButton *toolButton, toolButtons) {
            if (QAction *action = toolButton->defaultAction()) {
                toolButton->setIcon(action->icon());
            }
            toolButton->update();
        }
    }

    // Update MatrixWidgets efficiently
    foreach(QWidget* widget, app->topLevelWidgets()) {
        QList<MatrixWidget *> matrixWidgets = widget->findChildren<MatrixWidget *>();
        foreach (MatrixWidget* matrixWidget, matrixWidgets) {
            if (!matrixWidget) continue;

            // Always refresh cached appearance colors (cheap)
            try { matrixWidget->updateCachedAppearanceColors(); } catch (...) {}

            // Only drop pixmaps and invalidate track color cache if scheme actually changed
            if (schemeChanged) {
                try { matrixWidget->updateRenderingSettings(); } catch (...) {}
            } else {
                // No effective scheme change; a simple update is enough
                try { matrixWidget->update(); } catch (...) {}
            }
        }
    }

    // Reset the flag to allow future refreshes
    isRefreshingColors = false;
}

void Appearance::forceColorRefresh() {
    // Public method – always bypasses the debounce guard so that a theme
    // change is guaranteed to trigger a full icon / toolbar refresh.
    refreshColors(/*force=*/true);
}

void Appearance::connectToSystemThemeChanges() {
    QApplication *app = qobject_cast<QApplication *>(QApplication::instance());
    if (!app) return;

    // Connect to system theme change detection
    QStyleHints *hints = app->styleHints();

    // Connect to colorSchemeChanged signal
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, [](Qt::ColorScheme colorScheme) {
        Q_UNUSED(colorScheme)

        // Invalidate cache when system theme changes
        cachedStyle = "";

        // Refresh colors when system theme changes using the same shared debounce
        // We defer slightly to allow the platform to finalize its palette/style
        static QTimer *s_refreshDebounceTimer = nullptr;
        if (!s_refreshDebounceTimer) {
            s_refreshDebounceTimer = new QTimer();
            s_refreshDebounceTimer->setSingleShot(true);
            QObject::connect(s_refreshDebounceTimer, &QTimer::timeout, []() {
                refreshColors();
            });
        }
        s_refreshDebounceTimer->stop();
        s_refreshDebounceTimer->start(150);
    });
}

void Appearance::cleanup() {
    qDebug() << "Appearance: Starting cleanup of static resources";

    // Set shutdown flag to prevent any new QPixmap creation
    _shuttingDown = true;

    // Clean up color maps to prevent QColor destructor issues after QApplication shutdown
    qDeleteAll(channelColors);
    channelColors.clear();

    qDeleteAll(trackColors);
    trackColors.clear();

    // Clear custom color sets
    customChannelColors.clear();
    customTrackColors.clear();

    // Clear registered icon actions map
    // Note: We don't delete the QAction objects as they're owned by other components
    registeredIconActions.clear();

    qDebug() << "Appearance: Static resource cleanup completed";
}

void Appearance::setShuttingDown(bool shuttingDown) {
    _shuttingDown = shuttingDown;
}

void Appearance::setStartupComplete() {
    _startupComplete = true;
    // Now safe to do a one-time color/icon refresh to pick up the current theme.
    // Use a short delay so the event loop has fully started.
    QTimer::singleShot(50, []() {
        refreshColors();
    });
}

bool Appearance::isStartupComplete() {
    return _startupComplete;
}