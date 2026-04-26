#ifndef MODELFAVORITESDIALOG_H
#define MODELFAVORITESDIALOG_H

#include <QDialog>
#include <QStringList>

class QTabWidget;
class QListWidget;
class QPushButton;
class QLabel;

/**
 * \class ModelFavoritesDialog
 *
 * \brief Lets the user pick favourite models per provider via checkbox list.
 *
 * One tab per provider (openai / openrouter / gemini / custom). Each tab
 * shows the cached, chat-only model list (\ref ModelFavorites::isLikelyChatModel
 * applied) with a checkbox per row. Checked rows are persisted as the
 * provider's favourite set (\ref ModelFavorites::setFavorites).
 *
 * If the cache for a provider is empty the tab shows a hint pointing the user
 * at the refresh button in \ref AiSettingsWidget.
 */
class ModelFavoritesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModelFavoritesDialog(QWidget *parent = nullptr);

private slots:
    void onAccept();
    void onSelectAll();
    void onClearAll();

private:
    void buildTabForProvider(const QString &provider, const QString &label);

    QTabWidget *_tabs;
    // provider -> list widget
    QHash<QString, QListWidget *> _lists;
};

#endif // MODELFAVORITESDIALOG_H
