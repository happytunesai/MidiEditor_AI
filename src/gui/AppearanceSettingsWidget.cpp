#include "AppearanceSettingsWidget.h"

#define ROW_HEIGHT 45

#include <QGridLayout>
#include <QListWidget>
#include <QLabel>
#include <QColorDialog>
#include <QPushButton>
#include <QList>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QMainWindow>
#include <QApplication>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Appearance.h"
#include "SettingsDialog.h"
#include "MainWindow.h"

AppearanceSettingsWidget::AppearanceSettingsWidget(QWidget *parent)
    : SettingsWidget("Appearance", parent) {

    // Outer layout: a single QScrollArea filling the whole widget
    QVBoxLayout *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(outerLayout);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    outerLayout->addWidget(scrollArea);

    QWidget *content = new QWidget(scrollArea);
    QGridLayout *layout = new QGridLayout(content);
    content->setLayout(layout);
    scrollArea->setWidget(content);

    _channelItems = new QList<NamedColorWidgetItem *>();
    _trackItems = new QList<NamedColorWidgetItem *>();

    // === Settings first (most used) ===
    int row = 0;

    // Theme selection
    layout->addWidget(new QLabel("Theme"), row, 0, 1, 1);
    QComboBox *themeCombo = new QComboBox(content);
    themeCombo->addItems({
        "System (Auto)",
        "Dark",
        "Light",
        "Classic",
        "Sakura",
        "AMOLED",
        "Material Dark"
    });
    themeCombo->setCurrentIndex(static_cast<int>(Appearance::theme()));
    connect(themeCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(themeChanged(int)));
    layout->addWidget(themeCombo, row, 1, 1, 1);
    row++;

    // UI Styling options (only visually distinct in Classic theme)
    QLabel *styleLabel = new QLabel("Application Style", content);
    layout->addWidget(styleLabel, row, 0, 1, 1);
    QComboBox *styleCombo = new QComboBox(content);
    QStringList availableStyles = Appearance::availableStyles();
    styleCombo->addItems(availableStyles);
    int currentIndex = availableStyles.indexOf(Appearance::applicationStyle());
    if (currentIndex >= 0) {
        styleCombo->setCurrentIndex(currentIndex);
    }
    connect(styleCombo, SIGNAL(currentTextChanged(QString)), this, SLOT(styleChanged(QString)));
    layout->addWidget(styleCombo, row, 1, 1, 1);

    bool isClassic = (Appearance::theme() == Appearance::ThemeNone);
    styleCombo->setEnabled(isClassic);
    styleLabel->setEnabled(isClassic);
    connect(themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [styleCombo, styleLabel](int index) {
            bool classic = (index == static_cast<int>(Appearance::ThemeNone));
            styleCombo->setEnabled(classic);
            styleLabel->setEnabled(classic);
        });
    row++;

    // Color preset
    layout->addWidget(new QLabel("Color Preset"), row, 0, 1, 1);
    QComboBox *presetCombo = new QComboBox(content);
    for (int i = 0; i < Appearance::PresetCount; i++) {
        presetCombo->addItem(Appearance::colorPresetName(
            static_cast<Appearance::ColorPreset>(i)));
    }
    presetCombo->setCurrentIndex(static_cast<int>(Appearance::colorPreset()));
    connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int index) {
            Appearance::applyColorPreset(static_cast<Appearance::ColorPreset>(index));
            refreshColors();
        });
    layout->addWidget(presetCombo, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Strip Style"), row, 0, 1, 1);
    QComboBox *strip = new QComboBox(content);
    strip->addItems({
        "Highlight between octaves",
        "Highlight notes by keys",
        "Highlight alternatively"
    });
    strip->setCurrentIndex(Appearance::strip());
    connect(strip, SIGNAL(currentIndexChanged(int)), this, SLOT(stripStyleChanged(int)));
    layout->addWidget(strip, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Event Opacity"), row, 0, 1, 1);
    QSlider *opacity = new QSlider(Qt::Horizontal, content);
    opacity->setMaximum(100);
    opacity->setValue(Appearance::opacity());
    connect(opacity, SIGNAL(valueChanged(int)), this, SLOT(opacityChanged(int)));
    layout->addWidget(opacity, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Show C3/C6 Range Lines"), row, 0, 1, 1);
    QCheckBox *rangeLines = new QCheckBox(content);
    rangeLines->setChecked(Appearance::showRangeLines());
    connect(rangeLines, SIGNAL(toggled(bool)), this, SLOT(rangeLinesChanged(bool)));
    layout->addWidget(rangeLines, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Smooth Playback Scrolling"), row, 0, 1, 1);
    QCheckBox *smoothScroll = new QCheckBox(content);
    smoothScroll->setChecked(Appearance::smoothPlaybackScrolling());
    connect(smoothScroll, &QCheckBox::toggled, [](bool on) { Appearance::setSmoothPlaybackScrolling(on); });
    layout->addWidget(smoothScroll, row, 1, 1, 1);
    row++;

    // --- Timeline Marker Settings ---
    layout->addWidget(new QLabel("Show Program Change Markers"), row, 0, 1, 1);
    QCheckBox *pcMarkers = new QCheckBox(content);
    pcMarkers->setChecked(Appearance::showProgramChangeMarkers());
    connect(pcMarkers, &QCheckBox::toggled, [](bool on) { Appearance::setShowProgramChangeMarkers(on); });
    layout->addWidget(pcMarkers, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Show Control Change Markers"), row, 0, 1, 1);
    QCheckBox *ccMarkers = new QCheckBox(content);
    ccMarkers->setChecked(Appearance::showControlChangeMarkers());
    connect(ccMarkers, &QCheckBox::toggled, [](bool on) { Appearance::setShowControlChangeMarkers(on); });
    layout->addWidget(ccMarkers, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Show Text/Marker Events"), row, 0, 1, 1);
    QCheckBox *textMarkers = new QCheckBox(content);
    textMarkers->setChecked(Appearance::showTextEventMarkers());
    connect(textMarkers, &QCheckBox::toggled, [](bool on) { Appearance::setShowTextEventMarkers(on); });
    layout->addWidget(textMarkers, row, 1, 1, 1);
    row++;

    layout->addWidget(new QLabel("Marker Color Mode"), row, 0, 1, 1);
    QComboBox *markerColorCombo = new QComboBox(content);
    markerColorCombo->addItem("By Track");
    markerColorCombo->addItem("By Channel");
    markerColorCombo->setCurrentIndex(static_cast<int>(Appearance::markerColorMode()));
    connect(markerColorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
        [](int index) { Appearance::setMarkerColorMode(static_cast<Appearance::MarkerColorMode>(index)); });
    layout->addWidget(markerColorCombo, row, 1, 1, 1);
    row++;

    // === Collapsible Channel Colors section ===
    QPushButton *channelToggle = new QPushButton(QString::fromUtf8("\u25B6  Channel Colors"), content);
    channelToggle->setFlat(true);
    channelToggle->setCursor(Qt::PointingHandCursor);
    channelToggle->setStyleSheet("QPushButton { text-align: left; padding: 6px 4px; font-weight: bold; }");
    layout->addWidget(channelToggle, row, 0, 1, 2);
    row++;

    QListWidget *channelList = new QListWidget(content);
    channelList->setSelectionMode(QAbstractItemView::NoSelection);
    channelList->setStyleSheet(Appearance::listBorderStyle());
    for (int i = 0; i < 17; i++) {
        QString name = "Channel " + QString::number(i);
        if (i == 16) {
            name = "General Events (affecting all channels)";
        }
        QColor *channelColor = Appearance::channelColor(i);
        QColor safeColor = channelColor ? *channelColor : QColor(100, 100, 100);
        NamedColorWidgetItem *channelItem = new NamedColorWidgetItem(i, name,
                                                                     safeColor, content);
        QListWidgetItem *item = new QListWidgetItem();
        item->setSizeHint(QSize(0, ROW_HEIGHT));
        channelList->addItem(item);
        channelList->setItemWidget(item, channelItem);
        _channelItems->append(channelItem);
        connect(channelItem, SIGNAL(colorChanged(int,QColor)), this, SLOT(channelColorChanged(int,QColor)));
    }
    channelList->setFixedHeight(ROW_HEIGHT * 5);
    channelList->setVisible(false); // start collapsed
    connect(channelToggle, &QPushButton::clicked, [channelToggle, channelList]() {
        bool show = !channelList->isVisible();
        channelList->setVisible(show);
        channelToggle->setText(show ? QString::fromUtf8("\u25BC  Channel Colors")
                                    : QString::fromUtf8("\u25B6  Channel Colors"));
    });
    layout->addWidget(channelList, row, 0, 1, 2);
    row++;

    // === Collapsible Track Colors section ===
    QPushButton *trackToggle = new QPushButton(QString::fromUtf8("\u25B6  Track Colors"), content);
    trackToggle->setFlat(true);
    trackToggle->setCursor(Qt::PointingHandCursor);
    trackToggle->setStyleSheet("QPushButton { text-align: left; padding: 6px 4px; font-weight: bold; }");
    layout->addWidget(trackToggle, row, 0, 1, 2);
    row++;

    QListWidget *trackList = new QListWidget(content);
    trackList->setSelectionMode(QAbstractItemView::NoSelection);
    trackList->setStyleSheet(Appearance::listBorderStyle());
    for (int i = 0; i < 16; i++) {
        QColor *trackColor = Appearance::trackColor(i);
        QColor safeColor = trackColor ? *trackColor : QColor(100, 100, 100);
        NamedColorWidgetItem *trackItem = new NamedColorWidgetItem(i, "Track " + QString::number(i),
                                                                   safeColor, content);
        QListWidgetItem *item = new QListWidgetItem();
        item->setSizeHint(QSize(0, ROW_HEIGHT));
        trackList->addItem(item);
        trackList->setItemWidget(item, trackItem);
        _trackItems->append(trackItem);
        connect(trackItem, SIGNAL(colorChanged(int,QColor)), this, SLOT(trackColorChanged(int,QColor)));
    }
    trackList->setFixedHeight(ROW_HEIGHT * 5);
    trackList->setVisible(false); // start collapsed
    connect(trackToggle, &QPushButton::clicked, [trackToggle, trackList]() {
        bool show = !trackList->isVisible();
        trackList->setVisible(show);
        trackToggle->setText(show ? QString::fromUtf8("\u25BC  Track Colors")
                                  : QString::fromUtf8("\u25B6  Track Colors"));
    });
    layout->addWidget(trackList, row, 0, 1, 2);
    row++;

    // Reset Colors button
    QPushButton *resetButton = new QPushButton("Reset Colors", content);
    connect(resetButton, SIGNAL(clicked()), this, SLOT(resetColors()));
    layout->addWidget(resetButton, row, 1, 1, 1);
    row++;

    // Stretch at bottom
    layout->setRowStretch(row, 1);
}

void AppearanceSettingsWidget::channelColorChanged(int channel, QColor c) {
    Appearance::setChannelColor(channel, c);
}

void AppearanceSettingsWidget::trackColorChanged(int track, QColor c) {
    Appearance::setTrackColor(track, c);
}

void AppearanceSettingsWidget::resetColors() {
    Appearance::reset();
    refreshColors();
}

void AppearanceSettingsWidget::refreshColors() {
    // Refresh all color widgets to show current colors
    foreach(NamedColorWidgetItem* item, *_trackItems) {
        QColor *trackColor = Appearance::trackColor(item->number());
        if (trackColor) {
            item->colorChanged(*trackColor);
        }
    }
    foreach(NamedColorWidgetItem* item, *_channelItems) {
        QColor *channelColor = Appearance::channelColor(item->number());
        if (channelColor) {
            item->colorChanged(*channelColor);
        }
    }
    update();
}


void AppearanceSettingsWidget::opacityChanged(int opacity) {
    Appearance::setOpacity(opacity);
    foreach(NamedColorWidgetItem* item, *_trackItems) {
        item->colorChanged(*Appearance::trackColor(item->number()));
    }
    foreach(NamedColorWidgetItem* item, *_channelItems) {
        item->colorChanged(*Appearance::channelColor(item->number()));
    }
    update();
}

void AppearanceSettingsWidget::stripStyleChanged(int strip) {
    Appearance::setStrip(static_cast<Appearance::stripStyle>(strip));
    update();
}

void AppearanceSettingsWidget::rangeLinesChanged(bool enabled) {
    Appearance::setShowRangeLines(enabled);
    update();
}

void AppearanceSettingsWidget::styleChanged(const QString &style) {
    Appearance::setApplicationStyle(style);

    // Force immediate color refresh for all widgets
    Appearance::forceColorRefresh();
    refreshColors(); // Also refresh this widget's colors immediately
    update();
}

void AppearanceSettingsWidget::themeChanged(int index) {
    Appearance::Theme newTheme = static_cast<Appearance::Theme>(index);
    if (newTheme == Appearance::theme()) {
        return; // No change
    }

    // Ask the user before restarting
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Theme Change"));
    msgBox.setText(tr("Changing the theme requires restarting the application.\n"
                      "Your current file will be saved and reopened automatically."));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Yes);
    msgBox.button(QMessageBox::Yes)->setText(tr("Restart"));
    msgBox.button(QMessageBox::Cancel)->setText(tr("Cancel"));
    msgBox.setIcon(QMessageBox::Information);

    if (msgBox.exec() != QMessageBox::Yes) {
        // Revert the combo box to the current theme
        QComboBox *combo = qobject_cast<QComboBox*>(sender());
        if (combo) {
            combo->blockSignals(true);
            combo->setCurrentIndex(static_cast<int>(Appearance::theme()));
            combo->blockSignals(false);
        }
        return;
    }

    // Save the new theme to settings immediately (without applying style)
    Appearance::setThemeValue(newTheme);

    // Find the MainWindow via the SettingsDialog parent chain and restart
    SettingsDialog *dlg = qobject_cast<SettingsDialog*>(window());
    if (dlg && dlg->mainWindow()) {
        dlg->mainWindow()->restartForThemeChange();
    }
}


NamedColorWidgetItem::NamedColorWidgetItem(int number, QString name, QColor color, QWidget *parent) : QWidget(parent) {
    this->_number = number;
    this->color = color;

    setContentsMargins(0, 0, 0, 0);
    QGridLayout *layout = new QGridLayout(this);
    setLayout(layout);
    layout->setVerticalSpacing(1);

    colored = new ColoredWidget(color, this);
    colored->setFixedSize(ROW_HEIGHT - 15, ROW_HEIGHT - 15);
    layout->addWidget(colored, 0, 0, 1, 1);

    QLabel *text = new QLabel(name, this);
    text->setFixedHeight(15);
    layout->addWidget(text, 0, 1, 1, 1);
    setContentsMargins(5, 1, 5, 0);
    setFixedHeight(ROW_HEIGHT);
}

void NamedColorWidgetItem::mousePressEvent(QMouseEvent *event) {
    QColor newColor = QColorDialog::getColor(color, this);
    // Only apply the color if user didn't cancel (valid color returned)
    if (newColor.isValid()) {
        // Emit signal with raw color (Appearance::setChannelColor will apply opacity)
        emit colorChanged(_number, newColor);

        // But display the color with current opacity applied
        QColor displayColor = newColor;
        displayColor.setAlpha(Appearance::opacity() * 255 / 100);
        colored->setColor(displayColor);
        colored->update();
    }
    // If user canceled, do nothing - keep the original color
}

void NamedColorWidgetItem::colorChanged(QColor color) {
    // This slot is called when refreshing colors from the appearance system
    // The color already has opacity applied, so display it as-is
    colored->setColor(color);
    update();
    // Don't emit signal here - this is for display updates only
}

int NamedColorWidgetItem::number() {
    return _number;
}
