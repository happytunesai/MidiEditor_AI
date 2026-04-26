#include "ModelFavoritesDialog.h"

#include "../ai/ModelFavorites.h"
#include "../ai/ModelListCache.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHash>
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

    buildTabForProvider(QStringLiteral("openai"),     tr("OpenAI"));
    buildTabForProvider(QStringLiteral("openrouter"), tr("OpenRouter"));
    buildTabForProvider(QStringLiteral("gemini"),     tr("Gemini"));
    buildTabForProvider(QStringLiteral("custom"),     tr("Custom"));

    auto *btnRow = new QHBoxLayout();
    auto *selectAllBtn = new QPushButton(tr("Select all"), this);
    auto *clearAllBtn  = new QPushButton(tr("Clear current tab"), this);
    btnRow->addWidget(selectAllBtn);
    btnRow->addWidget(clearAllBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                    this);
    root->addWidget(bb);

    connect(selectAllBtn, &QPushButton::clicked, this, &ModelFavoritesDialog::onSelectAll);
    connect(clearAllBtn,  &QPushButton::clicked, this, &ModelFavoritesDialog::onClearAll);
    connect(bb,           &QDialogButtonBox::accepted, this, &ModelFavoritesDialog::onAccept);
    connect(bb,           &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ModelFavoritesDialog::buildTabForProvider(const QString &provider,
                                               const QString &label)
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    auto *list = new QListWidget(page);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    lay->addWidget(list);

    const QJsonArray cached = ModelListCache::models(provider);
    const QSet<QString> favs = ModelFavorites::favorites(provider);

    int chatCount = 0;
    for (const QJsonValue &v : cached) {
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

        auto *item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(favs.contains(id) ? Qt::Checked : Qt::Unchecked);
        ++chatCount;
    }

    if (chatCount == 0) {
        auto *empty = new QLabel(
            tr("No cached models for %1.\n"
               "Open Settings → AI and click the refresh icon next to the\n"
               "model dropdown to fetch the live list, then come back here.")
                .arg(label),
            page);
        empty->setStyleSheet(QStringLiteral("color: gray;"));
        list->setVisible(false);
        lay->addWidget(empty);
    }

    _lists.insert(provider, list);
    _tabs->addTab(page, label);
}

void ModelFavoritesDialog::onSelectAll()
{
    const int idx = _tabs->currentIndex();
    QString provider;
    for (auto it = _lists.constBegin(); it != _lists.constEnd(); ++it) {
        if (_tabs->indexOf(it.value()->parentWidget()) == idx) {
            provider = it.key();
            break;
        }
    }
    if (provider.isEmpty())
        return;
    QListWidget *list = _lists.value(provider);
    for (int i = 0; i < list->count(); ++i)
        list->item(i)->setCheckState(Qt::Checked);
}

void ModelFavoritesDialog::onClearAll()
{
    const int idx = _tabs->currentIndex();
    QString provider;
    for (auto it = _lists.constBegin(); it != _lists.constEnd(); ++it) {
        if (_tabs->indexOf(it.value()->parentWidget()) == idx) {
            provider = it.key();
            break;
        }
    }
    if (provider.isEmpty())
        return;
    QListWidget *list = _lists.value(provider);
    for (int i = 0; i < list->count(); ++i)
        list->item(i)->setCheckState(Qt::Unchecked);
}

void ModelFavoritesDialog::onAccept()
{
    for (auto it = _lists.constBegin(); it != _lists.constEnd(); ++it) {
        QStringList picked;
        QListWidget *list = it.value();
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            if (item->checkState() == Qt::Checked)
                picked.append(item->data(Qt::UserRole).toString());
        }
        ModelFavorites::setFavorites(it.key(), picked);
    }
    accept();
}
