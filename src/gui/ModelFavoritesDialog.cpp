#include "ModelFavoritesDialog.h"

#include "../ai/ModelFavorites.h"
#include "../ai/ModelListCache.h"
#include "../ai/ModelListFetcher.h"
#include "../ai/ProviderProfileStore.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

ModelFavoritesDialog::ModelFavoritesDialog(QWidget *parent)
    : QDialog(parent), _tabs(new QTabWidget(this))
{
    setWindowTitle(tr("Model Favorites"));
    resize(560, 480);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Pick the models you actually use per provider. If none are\n"
           "selected for a provider, every chat-capable model in the cache\n"
           "is shown. Image, audio, video and embedding models are always\n"
           "filtered out."),
        this);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    root->addWidget(hint);

    root->addWidget(_tabs, /*stretch*/ 1);

    buildTab(QStringLiteral("openai"),     tr("OpenAI"),     QString(), QString());
    buildTab(QStringLiteral("openrouter"), tr("OpenRouter"), QString(), QString());
    buildTab(QStringLiteral("gemini"),     tr("Gemini"),     QString(), QString());
    buildTab(QStringLiteral("ollama"),     tr("Ollama"),     QString(), QString());
    buildTab(QStringLiteral("custom"),     tr("Custom"),     QString(),
             tr("The Custom provider configured by hand, without a provider profile."));

    // One tab per stored CUSTOM provider profile: each is a different server
    // with a different catalogue, so its favourites must not mix with another
    // endpoint's. Profiles of a built-in provider are skipped on purpose -
    // they share that provider's catalogue and therefore its tab.
    const QStringList profiles = ProviderProfileStore::profileNames();
    for (const QString &name : profiles) {
        bool ok = false;
        const ProviderProfileStore::Profile p = ProviderProfileStore::load(name, &ok);
        if (!ok || p.provider.compare(QLatin1String("custom"), Qt::CaseInsensitive) != 0)
            continue;
        const QString scope = ProviderProfileStore::modelScopeIdForProfile(name);
        if (scope.isEmpty())
            continue;
        buildTab(scope, p.name, p.name,
                 tr("Provider profile \"%1\"\n%2").arg(p.name, p.baseUrl));
    }

    auto *btnRow = new QHBoxLayout();
    auto *selectAllBtn = new QPushButton(tr("Select all"), this);
    auto *clearAllBtn  = new QPushButton(tr("Clear current tab"), this);
    _refreshButton = new QPushButton(tr("Refresh from endpoint"), this);
    _refreshButton->setToolTip(
        tr("Fetches the model list from this profile's own endpoint.\n"
           "The connection MidiPilot currently uses is not changed."));
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(clearAllBtn);
    btnRow->addWidget(_refreshButton);
    btnRow->addStretch();
    root->addLayout(btnRow);

    _statusLabel = new QLabel(this);
    _statusLabel->setStyleSheet(QStringLiteral("color: gray;"));
    _statusLabel->setWordWrap(true);
    root->addWidget(_statusLabel);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                    this);
    root->addWidget(bb);

    connect(selectAllBtn, &QPushButton::clicked, this, &ModelFavoritesDialog::onSelectAll);
    connect(clearAllBtn,  &QPushButton::clicked, this, &ModelFavoritesDialog::onClearAll);
    connect(_refreshButton, &QPushButton::clicked, this, &ModelFavoritesDialog::onRefreshProfile);
    connect(_tabs,        &QTabWidget::currentChanged, this, &ModelFavoritesDialog::onTabChanged);
    connect(bb,           &QDialogButtonBox::accepted, this, &ModelFavoritesDialog::onAccept);
    connect(bb,           &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateRefreshButton();
}

void ModelFavoritesDialog::buildTab(const QString &scope, const QString &label,
                                    const QString &profileName,
                                    const QString &tooltip)
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    Tab tab;
    tab.scope = scope;
    tab.profileName = profileName;
    tab.label = label;

    tab.list = new QListWidget(page);
    tab.list->setSelectionMode(QAbstractItemView::NoSelection);
    lay->addWidget(tab.list);

    tab.emptyHint = new QLabel(page);
    tab.emptyHint->setStyleSheet(QStringLiteral("color: gray;"));
    tab.emptyHint->setWordWrap(true);
    tab.emptyHint->setVisible(false);
    lay->addWidget(tab.emptyHint);

    fillList(tab, ModelListCache::models(scope), ModelFavorites::favorites(scope));

    _tabMeta.append(tab);
    const int idx = _tabs->addTab(page, label);
    if (!tooltip.isEmpty())
        _tabs->setTabToolTip(idx, tooltip);
}

void ModelFavoritesDialog::fillList(const Tab &tab, const QJsonArray &models,
                                    const QSet<QString> &checked)
{
    tab.list->clear();

    for (const QJsonValue &v : models) {
        const QJsonObject m = v.toObject();
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        if (!ModelFavorites::isLikelyChatModel(id))
            continue;
        const QString display = m.value(QStringLiteral("displayName")).toString();
        const int cw = m.value(QStringLiteral("contextWindow")).toInt(0);

        QString text = display.isEmpty() ? id : display;
        if (display != id && !display.isEmpty())
            text = QStringLiteral("%1  (%2)").arg(display, id);
        if (cw > 0)
            text += tr("  — %L1 ctx").arg(cw);

        auto *item = new QListWidgetItem(text, tab.list);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked.contains(id) ? Qt::Checked : Qt::Unchecked);
    }

    const bool empty = tab.list->count() == 0;
    tab.list->setVisible(!empty);
    tab.emptyHint->setVisible(empty);
    if (empty) {
        tab.emptyHint->setText(
            tab.profileName.isEmpty()
                ? tr("No cached models for %1.\n"
                     "Open Settings → AI and click the refresh icon next to the\n"
                     "model dropdown to fetch the live list, then come back here.")
                      .arg(tab.label)
                : tr("No cached models for the provider profile \"%1\".\n"
                     "Click \"Refresh from endpoint\" to fetch its model list.")
                      .arg(tab.label));
    }
}

QSet<QString> ModelFavoritesDialog::checkedIds(const Tab &tab) const
{
    QSet<QString> out;
    for (int i = 0; i < tab.list->count(); ++i) {
        QListWidgetItem *item = tab.list->item(i);
        if (item->checkState() == Qt::Checked)
            out.insert(item->data(Qt::UserRole).toString());
    }
    return out;
}

int ModelFavoritesDialog::indexOfScope(const QString &scope) const
{
    for (int i = 0; i < _tabMeta.size(); ++i) {
        if (_tabMeta.at(i).scope == scope)
            return i;
    }
    return -1;
}

void ModelFavoritesDialog::updateRefreshButton()
{
    if (!_refreshButton)
        return;
    const int idx = _tabs->currentIndex();
    const bool isProfile = idx >= 0 && idx < _tabMeta.size()
                           && !_tabMeta.at(idx).profileName.isEmpty();
    _refreshButton->setVisible(isProfile);
    _refreshButton->setEnabled(isProfile && !_fetchInFlight);
}

void ModelFavoritesDialog::onTabChanged(int /*index*/)
{
    updateRefreshButton();
}

void ModelFavoritesDialog::onSelectAll()
{
    const int idx = _tabs->currentIndex();
    if (idx < 0 || idx >= _tabMeta.size())
        return;
    QListWidget *list = _tabMeta.at(idx).list;
    for (int i = 0; i < list->count(); ++i)
        list->item(i)->setCheckState(Qt::Checked);
}

void ModelFavoritesDialog::onClearAll()
{
    const int idx = _tabs->currentIndex();
    if (idx < 0 || idx >= _tabMeta.size())
        return;
    QListWidget *list = _tabMeta.at(idx).list;
    for (int i = 0; i < list->count(); ++i)
        list->item(i)->setCheckState(Qt::Unchecked);
}

void ModelFavoritesDialog::onRefreshProfile()
{
    const int idx = _tabs->currentIndex();
    if (_fetchInFlight || idx < 0 || idx >= _tabMeta.size())
        return;
    const Tab tab = _tabMeta.at(idx);
    if (tab.profileName.isEmpty())
        return;

    bool ok = false;
    const ProviderProfileStore::Profile p =
        ProviderProfileStore::load(tab.profileName, &ok);
    if (!ok) {
        _statusLabel->setText(
            tr("The provider profile \"%1\" no longer exists.").arg(tab.label));
        return;
    }

    _fetchInFlight = true;
    updateRefreshButton();
    _statusLabel->setText(tr("Fetching models for \"%1\"…").arg(tab.label));

    // Parented to the dialog: closing it cancels the request and disconnects
    // the lambdas below, so nothing outlives this window.
    auto *fetcher = new ModelListFetcher(this);
    connect(fetcher, &ModelListFetcher::finished, this,
            [this](const QString &scope, const QJsonArray &models) {
                _fetchInFlight = false;
                ModelListCache::store(scope, models);
                const int i = indexOfScope(scope);
                if (i >= 0) {
                    // Keep what the user ticked in this session; a model that
                    // disappeared from the endpoint simply drops out.
                    const Tab &t = _tabMeta.at(i);
                    fillList(t, models, checkedIds(t));
                    // Report what the list actually shows, not the raw
                    // response size - non-chat entries were filtered out.
                    _statusLabel->setText(tr("\"%1\": %2 chat models cached.")
                                              .arg(t.label)
                                              .arg(t.list->count()));
                }
                updateRefreshButton();
            });
    connect(fetcher, &ModelListFetcher::failed, this,
            [this](const QString &, const QString &error) {
                _fetchInFlight = false;
                _statusLabel->setText(tr("Refresh failed: %1").arg(error));
                updateRefreshButton();
            });
    // The profile's own base URL and key - the active connection is untouched.
    fetcher->fetch(p.provider, ProviderProfileStore::apiKeyFor(tab.profileName),
                   p.baseUrl, tab.scope);
}

void ModelFavoritesDialog::onAccept()
{
    for (const Tab &tab : _tabMeta) {
        // A tab with nothing to show (empty cache) must not be read as "the
        // user unticked everything" - that would silently wipe the stored
        // favourites of a provider whose cache happens to be empty.
        if (tab.list->count() == 0)
            continue;
        QStringList picked;
        for (int i = 0; i < tab.list->count(); ++i) {
            QListWidgetItem *item = tab.list->item(i);
            if (item->checkState() == Qt::Checked)
                picked.append(item->data(Qt::UserRole).toString());
        }
        ModelFavorites::setFavorites(tab.scope, picked);
    }
    accept();
}
