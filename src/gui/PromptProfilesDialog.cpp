#include "PromptProfilesDialog.h"

#include "../ai/ModelFavorites.h"
#include "../ai/ModelListCache.h"
#include "../ai/PromptProfileStore.h"

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {
constexpr int kProfileIdRole = Qt::UserRole + 1;
constexpr int kProviderRole  = Qt::UserRole + 1;
constexpr int kModelIdRole   = Qt::UserRole + 2;
constexpr int kIsWildcardRole = Qt::UserRole + 3;

const char *kProviders[] = {"openai", "openrouter", "gemini", "custom"};
const char *kProviderLabels[] = {"OpenAI", "OpenRouter", "Gemini", "Custom"};
constexpr int kProviderCount = 4;
} // namespace

PromptProfilesDialog::PromptProfilesDialog(PromptProfileStore *store,
                                           QWidget *parent)
    : QDialog(parent), _store(store)
{
    setWindowTitle(tr("Prompt Profiles"));
    resize(900, 620);

    auto *root = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Bind a system prompt override to one or more models. The\n"
           "'GPT-5.5 Decisive' built-in adds a short 'commit-after-one-\n"
           "paragraph' rule for the GPT-5.5 family without affecting any\n"
           "other model. Built-in profiles are read-only — duplicate to\n"
           "edit. Use \"name*\" patterns to bind to a model family."),
        this);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    root->addWidget(hint);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter, 1);

    // ---------------- Left: profile list + buttons ---------------------
    auto *leftWrap = new QWidget(splitter);
    auto *leftLay = new QVBoxLayout(leftWrap);
    leftLay->setContentsMargins(0, 0, 0, 0);
    _list = new QListWidget(leftWrap);
    leftLay->addWidget(_list, 1);

    auto *btnRow = new QHBoxLayout();
    _addBtn = new QPushButton(tr("Add"), leftWrap);
    _dupBtn = new QPushButton(tr("Duplicate"), leftWrap);
    _delBtn = new QPushButton(tr("Delete"), leftWrap);
    btnRow->addWidget(_addBtn);
    btnRow->addWidget(_dupBtn);
    btnRow->addWidget(_delBtn);
    leftLay->addLayout(btnRow);

    splitter->addWidget(leftWrap);

    // ---------------- Right: edit pane --------------------------------
    // Plain widget — NO outer QScrollArea. The model tree owns its own
    // vertical scrollbar; an outer scroll area would expand the tree to
    // its full sizeHint (all rows) and hide its scrollbar.
    auto *rightWrap = new QWidget(splitter);
    auto *rightLay = new QVBoxLayout(rightWrap);
    rightLay->setContentsMargins(8, 0, 8, 0);

    auto *nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("Name:"), rightWrap));
    _nameEdit = new QLineEdit(rightWrap);
    nameRow->addWidget(_nameEdit, 1);
    rightLay->addLayout(nameRow);

    _appendCheck = new QCheckBox(tr("Append to default prompt (instead of replacing)"),
                                  rightWrap);
    _appendCheck->setToolTip(tr(
        "Checked: the editor's default system prompt is sent first, then\n"
        "this profile's text is appended. Unchecked: this profile fully\n"
        "replaces the default — use with care."));
    rightLay->addWidget(_appendCheck);

    auto *editSplitter = new QSplitter(Qt::Vertical, rightWrap);

    auto *promptWrap = new QWidget(editSplitter);
    auto *promptLay = new QVBoxLayout(promptWrap);
    promptLay->setContentsMargins(0, 0, 0, 0);

    _systemEdit = new QPlainTextEdit(promptWrap);
    _systemEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    _systemEdit->setPlaceholderText(tr(
        "System prompt body...\n"
        "Tip: keep it short and rule-based. The AI sees this on every turn."));
    _systemEdit->setMinimumHeight(120);
    promptLay->addWidget(_systemEdit, 1);

    _tokenLabel = new QLabel(promptWrap);
    _tokenLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    promptLay->addWidget(_tokenLabel);

    auto *modelWrap = new QWidget(editSplitter);
    auto *modelLay = new QVBoxLayout(modelWrap);
    modelLay->setContentsMargins(0, 0, 0, 0);

    modelLay->addWidget(new QLabel(tr("Bind to models:"), modelWrap));
    _modelTree = new QTreeWidget(modelWrap);
    _modelTree->setHeaderLabels({tr("Model"), tr("Provider:Model pattern")});
    _modelTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _modelTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    _modelTree->setRootIsDecorated(true);
    _modelTree->setItemsExpandable(true);
    _modelTree->setExpandsOnDoubleClick(true);
    _modelTree->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    _modelTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    _modelTree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    _modelTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _modelTree->setMinimumHeight(180);
    modelLay->addWidget(_modelTree, 1);

    editSplitter->addWidget(promptWrap);
    editSplitter->addWidget(modelWrap);
    editSplitter->setStretchFactor(0, 2);
    editSplitter->setStretchFactor(1, 1);
    editSplitter->setSizes({360, 220});
    rightLay->addWidget(editSplitter, 1);

    splitter->addWidget(rightWrap);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({260, 640});

    // ---------------- Footer ------------------------------------------
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                    this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &PromptProfilesDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(_addBtn, &QPushButton::clicked, this, &PromptProfilesDialog::onAddProfile);
    connect(_dupBtn, &QPushButton::clicked, this, &PromptProfilesDialog::onDuplicateProfile);
    connect(_delBtn, &QPushButton::clicked, this, &PromptProfilesDialog::onDeleteProfile);

    connect(_list, &QListWidget::itemSelectionChanged,
            this, &PromptProfilesDialog::onProfileSelected);
    connect(_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
        if (_suppressEditSignals) return;
        const QString id = item->data(kProfileIdRole).toString();
        for (PromptProfile &p : _profiles) {
            if (p.id == id) {
                p.enabled = item->checkState() == Qt::Checked;
                break;
            }
        }
    });

    connect(_nameEdit, &QLineEdit::textEdited, this,
            &PromptProfilesDialog::onNameEdited);
    connect(_appendCheck, &QCheckBox::toggled, this,
            &PromptProfilesDialog::onAppendToggled);
    connect(_systemEdit, &QPlainTextEdit::textChanged, this,
            &PromptProfilesDialog::onSystemEdited);
    connect(_modelTree, &QTreeWidget::itemChanged, this,
            &PromptProfilesDialog::onModelTreeChanged);

    // Load.
    _profiles = _store->profiles();
    rebuildList();
}

void PromptProfilesDialog::rebuildList(const QString &selectId)
{
    _suppressEditSignals = true;
    _list->clear();
    for (const PromptProfile &p : _profiles) {
        auto *item = new QListWidgetItem(_list);
        QString label = p.name.isEmpty() ? tr("(unnamed)") : p.name;
        if (p.builtin)
            label = QStringLiteral("\xF0\x9F\x94\x92 ") + label; // 🔒
        item->setText(label);
        item->setData(kProfileIdRole, p.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(p.enabled ? Qt::Checked : Qt::Unchecked);
        if (p.id == selectId)
            _list->setCurrentItem(item);
    }
    if (_list->currentRow() < 0 && _list->count() > 0)
        _list->setCurrentRow(0);
    _suppressEditSignals = false;
    onProfileSelected();
}

void PromptProfilesDialog::populateModelTree()
{
    _suppressTreeSignals = true;
    _modelTree->clear();

    PromptProfile *cur = currentProfile();
    QSet<QString> bound;
    if (cur) {
        for (const QString &m : cur->models)
            bound.insert(m);
    }
    const bool bindingsEditable = cur && !cur->builtin;

    auto applyBindingFlags = [bindingsEditable](QTreeWidgetItem *item) {
        Qt::ItemFlags flags = item->flags() | Qt::ItemIsUserCheckable;
        if (!bindingsEditable) {
            flags &= ~Qt::ItemIsEnabled;
            item->setToolTip(0, QObject::tr("Built-in profile bindings are read-only. Duplicate the profile to edit them."));
            item->setToolTip(1, QObject::tr("Built-in profile bindings are read-only. Duplicate the profile to edit them."));
        }
        item->setFlags(flags);
    };

    for (int i = 0; i < kProviderCount; ++i) {
        const QString provider = QString::fromLatin1(kProviders[i]);
        const QString label = QString::fromLatin1(kProviderLabels[i]);
        auto *root = new QTreeWidgetItem(_modelTree);
        root->setText(0, label);
        // Mark this row so the click handler can identify provider headers.
        root->setData(0, kIsWildcardRole, QVariant());
        root->setData(0, kProviderRole, provider);
        // Keep provider headers as normal tree rows. Spanning the first
        // column looks nicer, but on Windows/Qt it can make the disclosure
        // triangle stop toggling reliably.

        // Wildcard row first: "<provider>:*" — convenient one-click bind.
        auto *star = new QTreeWidgetItem(root);
        star->setText(0, tr("(All models)"));
        const QString starPattern = provider + QStringLiteral(":*");
        star->setText(1, starPattern);
        applyBindingFlags(star);
        star->setCheckState(0, bound.contains(starPattern) ? Qt::Checked : Qt::Unchecked);
        star->setData(0, kProviderRole, provider);
        star->setData(0, kModelIdRole, starPattern);
        star->setData(0, kIsWildcardRole, true);

        const QJsonArray cached = ModelListCache::models(provider);
        QStringList added;
        for (const QJsonValue &v : cached) {
            QJsonObject m = v.toObject();
            const QString id = m.value(QStringLiteral("id")).toString();
            if (id.isEmpty() || !ModelFavorites::isLikelyChatModel(id))
                continue;
            const QString display = m.value(QStringLiteral("displayName")).toString();
            const QString pattern = provider + QLatin1Char(':') + id;
            auto *child = new QTreeWidgetItem(root);
            child->setText(0, display.isEmpty() ? id : display);
            child->setText(1, pattern);
            applyBindingFlags(child);
            child->setCheckState(0, bound.contains(pattern) ? Qt::Checked : Qt::Unchecked);
            child->setData(0, kProviderRole, provider);
            child->setData(0, kModelIdRole, pattern);
            child->setData(0, kIsWildcardRole, false);
            added.append(pattern);
        }

        // Surface any patterns the profile binds that are not in the cache
        // (e.g. shipped built-in's "openai:gpt-5.5*") so the user can see
        // and uncheck them.
        for (const QString &m : bound) {
            if (!m.startsWith(provider + QLatin1Char(':')))
                continue;
            if (m == starPattern || added.contains(m))
                continue;
            auto *child = new QTreeWidgetItem(root);
            child->setText(0, m.section(QLatin1Char(':'), 1) + tr("  (custom)"));
            child->setText(1, m);
            applyBindingFlags(child);
            child->setCheckState(0, Qt::Checked);
            child->setData(0, kProviderRole, provider);
            child->setData(0, kModelIdRole, m);
            child->setData(0, kIsWildcardRole, m.endsWith(QLatin1Char('*')));
        }

        root->setExpanded(true);
    }
    _suppressTreeSignals = false;
}

PromptProfile *PromptProfilesDialog::currentProfile()
{
    QListWidgetItem *item = _list->currentItem();
    if (!item)
        return nullptr;
    const QString id = item->data(kProfileIdRole).toString();
    for (PromptProfile &p : _profiles) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

void PromptProfilesDialog::onProfileSelected()
{
    PromptProfile *cur = currentProfile();
    const bool any = (cur != nullptr);
    const bool editable = any && !cur->builtin;

    _nameEdit->setEnabled(editable);
    _appendCheck->setEnabled(editable);
    _systemEdit->setReadOnly(!editable);
    // Built-in profiles are read-only, but the model list still needs to
    // be navigable. Disabling the whole tree also disables scrolling and
    // expand/collapse handles, which makes long built-in bindings unusable.
    _modelTree->setEnabled(any);
    _delBtn->setEnabled(editable);
    _dupBtn->setEnabled(any);

    _suppressEditSignals = true;
    if (cur) {
        _nameEdit->setText(cur->name);
        _appendCheck->setChecked(cur->appendToDefault);
        _systemEdit->setPlainText(cur->system);
    } else {
        _nameEdit->clear();
        _appendCheck->setChecked(true);
        _systemEdit->clear();
    }
    _suppressEditSignals = false;

    populateModelTree();
    updateTokenLabel();
}

void PromptProfilesDialog::onNameEdited()
{
    if (_suppressEditSignals) return;
    PromptProfile *cur = currentProfile();
    if (!cur || cur->builtin) return;
    cur->name = _nameEdit->text();
    QListWidgetItem *item = _list->currentItem();
    if (item) {
        _suppressEditSignals = true;
        item->setText(cur->name.isEmpty() ? tr("(unnamed)") : cur->name);
        _suppressEditSignals = false;
    }
}

void PromptProfilesDialog::onAppendToggled(bool checked)
{
    if (_suppressEditSignals) return;
    PromptProfile *cur = currentProfile();
    if (!cur || cur->builtin) return;
    cur->appendToDefault = checked;
}

void PromptProfilesDialog::onSystemEdited()
{
    if (_suppressEditSignals) return;
    PromptProfile *cur = currentProfile();
    if (!cur || cur->builtin) return;
    cur->system = _systemEdit->toPlainText();
    updateTokenLabel();
}

void PromptProfilesDialog::onModelTreeChanged(QTreeWidgetItem *item, int column)
{
    if (_suppressTreeSignals || column != 0)
        return;
    PromptProfile *cur = currentProfile();
    if (!cur || cur->builtin) return;
    const QString pattern = item->data(0, kModelIdRole).toString();
    if (pattern.isEmpty())
        return;
    if (item->checkState(0) == Qt::Checked) {
        if (!cur->models.contains(pattern))
            cur->models.append(pattern);
    } else {
        cur->models.removeAll(pattern);
    }
}

void PromptProfilesDialog::updateTokenLabel()
{
    const int chars = _systemEdit->toPlainText().length();
    // Same rough heuristic the rest of the codebase uses (chars / 4).
    const int tokens = chars / 4;
    _tokenLabel->setText(tr("%1 chars / ~%2 tokens").arg(chars).arg(tokens));
}

void PromptProfilesDialog::onAddProfile()
{
    PromptProfile p;
    p.id = PromptProfileStore::newId();
    p.name = tr("New Profile");
    p.system.clear();
    p.appendToDefault = true;
    p.builtin = false;
    p.enabled = true;
    _profiles.append(p);
    rebuildList(p.id);
    _nameEdit->setFocus();
    _nameEdit->selectAll();
}

void PromptProfilesDialog::onDuplicateProfile()
{
    PromptProfile *cur = currentProfile();
    if (!cur) return;
    PromptProfile p = *cur;
    p.id = PromptProfileStore::newId();
    p.name = cur->name + tr(" (copy)");
    p.builtin = false;
    _profiles.append(p);
    rebuildList(p.id);
}

void PromptProfilesDialog::onDeleteProfile()
{
    PromptProfile *cur = currentProfile();
    if (!cur || cur->builtin) return;
    const QString id = cur->id;
    for (int i = 0; i < _profiles.size(); ++i) {
        if (_profiles[i].id == id) { _profiles.removeAt(i); break; }
    }
    rebuildList();
}

void PromptProfilesDialog::onAccept()
{
    // Build set of ids currently in store. Anything missing from _profiles
    // that isn't built-in must be removed; anything in _profiles must be
    // upserted.
    const QList<PromptProfile> existing = _store->profiles();
    QSet<QString> keepIds;
    for (const PromptProfile &p : _profiles)
        keepIds.insert(p.id);
    for (const PromptProfile &p : existing) {
        if (p.builtin) continue;
        if (!keepIds.contains(p.id))
            _store->remove(p.id);
    }
    QStringList order;
    for (const PromptProfile &p : _profiles) {
        _store->upsert(p);
        order.append(p.id);
    }
    _store->setOrder(order);
    accept();
}
