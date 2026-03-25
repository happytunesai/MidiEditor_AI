#ifndef FFXIVFIXERDIALOG_H
#define FFXIVFIXERDIALOG_H

#include <QDialog>
#include <QJsonObject>

class QRadioButton;
class QLabel;
class QPushButton;
class QButtonGroup;

/**
 * \class FFXIVFixerDialog
 *
 * \brief Dialog for selecting which FFXIV Channel Fixer tier to apply.
 *
 * Shows a file analysis summary and lets the user choose between:
 *   Rebuild (Full Reassignment) or Preserve (Minimal Changes).
 * The "Continue" button applies the selected mode; "Abort" cancels.
 */
class FFXIVFixerDialog : public QDialog {
    Q_OBJECT

public:
    explicit FFXIVFixerDialog(const QJsonObject &analysis, QWidget *parent = nullptr);

    /** \brief Returns the selected tier (2 or 3).  Returns 0 if none selected. */
    int selectedTier() const;

private slots:
    void onSelectionChanged();

private:
    void setupUI(const QJsonObject &analysis);

    QButtonGroup *_tierGroup;
    QRadioButton *_tier2Radio;
    QRadioButton *_tier3Radio;
    QPushButton  *_continueButton;
    QPushButton  *_abortButton;
};

#endif // FFXIVFIXERDIALOG_H
