/*
 * MidiEditor
 * Copyright (C) 2010  Markus Schwenk
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FFXIVVOICEGAUGEWIDGET_H_
#define FFXIVVOICEGAUGEWIDGET_H_

#include <QWidget>
#include <QPointer>
#include <QTimer>

class MidiFile;

/**
 * \class FfxivVoiceGaugeWidget
 *
 * \brief Toolbar widget showing live FFXIV voice load.
 *
 * Two-region pill: a coloured LED (green/yellow/red based on the voice
 * count at the playhead) plus a numeric overflow indicator
 * (`+0` green, `+3` red). Hidden when FFXIV SoundFont Mode is off.
 *
 * Reads its data from the singleton FfxivVoiceAnalyzer, which is kept in
 * sync with file edits via the Protocol::actionFinished signal. During
 * playback the widget refreshes via its own QTimer (~30 fps).
 */
class FfxivVoiceGaugeWidget : public QWidget {
    Q_OBJECT

public:
    explicit FfxivVoiceGaugeWidget(QWidget *parent = nullptr);

    void setFile(MidiFile *file);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void refresh();
    void onAnalysisUpdated(MidiFile *file);
    void onFfxivModeChanged(bool enabled);

private:
    /// Re-evaluate visibility based on FFXIV mode + analyser enabled flag.
    void updateVisibility();

    /// Current voice count to display (0 if no file / FFXIV off).
    int currentVoiceCount() const;

    QPointer<MidiFile> _file;
    QTimer _timer;
    int _displayedVoices = 0;
    int _peakVoices = 0;
    int _peakTick = 0;
    int _overflowEvents = 0;
    bool _ffxivOn = false;
};

#endif // FFXIVVOICEGAUGEWIDGET_H_
