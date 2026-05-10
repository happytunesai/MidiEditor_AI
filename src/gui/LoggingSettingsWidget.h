/*
 * MidiEditor AI
 *
 * LoggingSettingsWidget — general Qt logging-rules controls.
 *
 * v1.7.1 redesign: replaces the original level dropdown with a stack
 * of color-coded radio buttons (one per level) plus a live preview
 * pane showing sample log lines a user would see at the selected
 * level, and a size-estimate warning that updates as the choice
 * changes. The advanced per-category rules editor and the
 * open-log-file button stay as before.
 *
 * The Collaboration tab keeps its focused "verbose collab logging"
 * checkbox that layers on top of whatever's set here.
 */

#ifndef LOGGINGSETTINGSWIDGET_H
#define LOGGINGSETTINGSWIDGET_H

#include "SettingsWidget.h"

#include "../LoggingConfig.h"

class QButtonGroup;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;

class LoggingSettingsWidget : public SettingsWidget {
    Q_OBJECT
public:
    explicit LoggingSettingsWidget(QWidget *parent = nullptr);

    bool accept() override;

private slots:
    /** \brief Refresh the preview pane + size-estimate warning when
     *  the user picks a different level. */
    void onLevelChanged();

private:
    LoggingConfig::Level currentLevel() const;
    void setCurrentLevel(LoggingConfig::Level lvl);

    /** \brief Helper that builds one row in the level stack: a
     *  radio + colored severity bar + name + one-line description. */
    QWidget *makeLevelRow(LoggingConfig::Level lvl,
                          const QString &name,
                          const QString &description);

    /** \brief Sample log lines that would be visible at \a lvl.
     *  Used to populate the preview pane. */
    QString sampleLinesFor(LoggingConfig::Level lvl) const;
    /** \brief Human-readable size estimate for \a lvl, with a colour
     *  hint (green/yellow/red) for the warning callout. */
    QString sizeEstimateFor(LoggingConfig::Level lvl) const;

    QButtonGroup *_levelGroup = nullptr;
    QRadioButton *_radioOff = nullptr;
    QRadioButton *_radioErrors = nullptr;
    QRadioButton *_radioWarnings = nullptr;
    QRadioButton *_radioInfo = nullptr;
    QRadioButton *_radioDebug = nullptr;

    QPlainTextEdit *_previewEdit = nullptr;
    QLabel *_sizeWarning = nullptr;

    QPlainTextEdit *_perCategoryEdit = nullptr;
    QPushButton *_openFileButton = nullptr;
};

#endif // LOGGINGSETTINGSWIDGET_H
