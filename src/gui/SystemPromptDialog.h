#ifndef SYSTEMPROMPTDIALOG_H
#define SYSTEMPROMPTDIALOG_H

#include <QDialog>

class QTabWidget;
class QPlainTextEdit;
class QLabel;

class SystemPromptDialog : public QDialog {
    Q_OBJECT

public:
    explicit SystemPromptDialog(QWidget *parent = nullptr);

private slots:
    void onLoadDefault();
    void onResetAll();
    void onExportJson();
    void onImportJson();
    void onSave();

private:
    void loadCurrentPrompts();
    QString promptsFilePath() const;

    QTabWidget *_tabs;
    QPlainTextEdit *_simpleEdit;
    QPlainTextEdit *_agentEdit;
    QPlainTextEdit *_ffxivEdit;
    QPlainTextEdit *_ffxivCompactEdit;
    QLabel *_statusLabel;
};

#endif // SYSTEMPROMPTDIALOG_H
