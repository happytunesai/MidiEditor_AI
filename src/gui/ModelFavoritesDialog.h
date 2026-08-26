#ifndef MODELFAVORITESDIALOG_H
#define MODELFAVORITESDIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QTabWidget;
class QListWidget;
class QPushButton;
class QLabel;

/**
 * \class ModelFavoritesDialog
 *
 * \brief Lets the user pick favourite models per endpoint via checkbox list.
 *
 * One tab per built-in provider (openai / openrouter / gemini / ollama /
 * custom), followed by one tab per stored Custom provider profile. Each tab
 * shows the cached, chat-only model list (\ref ModelFavorites::isLikelyChatModel
 * applied) with a checkbox per row. Checked rows are persisted as that tab's
 * favourite set (\ref ModelFavorites::setFavorites) under its scope
 * (ProviderProfileStore::modelScopeId).
 *
 * The plain "Custom" tab keeps representing the ad-hoc endpoint - the one
 * configured by hand without a profile - so nothing moves when profiles come
 * and go. Profiles of a non-custom provider get no tab: they share their
 * provider's catalogue and therefore its tab.
 *
 * The tab set is built in the constructor, and the dialog is created fresh on
 * every open, so adding or deleting a profile is reflected the next time it is
 * opened.
 *
 * If the cache for a tab is empty, the tab shows a hint. Profile tabs can be
 * filled without switching the application's connection: \b Refresh fetches
 * the model list straight from that profile's endpoint.
 */
class ModelFavoritesDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModelFavoritesDialog(QWidget *parent = nullptr);

private slots:
    void onAccept();
    void onSelectAll();
    void onClearAll();
    void onRefreshProfile();
    void onTabChanged(int index);

private:
    struct Tab {
        QString scope;        ///< favourites / cache key
        QString profileName;  ///< empty for the built-in provider tabs
        QString label;
        QListWidget *list = nullptr;
        QLabel *emptyHint = nullptr;
    };

    void buildTab(const QString &scope, const QString &label,
                  const QString &profileName, const QString &tooltip);
    void fillList(const Tab &tab, const QJsonArray &models,
                  const QSet<QString> &checked);
    QSet<QString> checkedIds(const Tab &tab) const;
    int indexOfScope(const QString &scope) const;
    void updateRefreshButton();

    QTabWidget *_tabs;
    QVector<Tab> _tabMeta;  ///< parallel to the tab index
    QPushButton *_refreshButton = nullptr;
    QLabel *_statusLabel = nullptr;
    bool _fetchInFlight = false;
};

#endif // MODELFAVORITESDIALOG_H
