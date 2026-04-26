#ifndef PROMPTPROFILESDIALOG_H
#define PROMPTPROFILESDIALOG_H

#include "../ai/PromptProfile.h"

#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class PromptProfileStore;

/**
 * \class PromptProfilesDialog
 *
 * \brief Lets the user manage \ref PromptProfile entries — create, edit,
 *        delete, and bind them to specific provider:model patterns.
 *
 * Layout mirrors \ref ModelFavoritesDialog:
 *   * Left list: one row per profile (checkbox = enabled, lock icon for
 *     built-ins, Add/Duplicate/Delete buttons below).
 *   * Right pane: name field, "append to default" checkbox, monospace
 *     prompt editor, model picker tree (Provider → Model with checkbox
 *     per row + a "*" wildcard row per provider), and a token-count hint.
 *
 * Save commits all in-memory changes back to the \ref PromptProfileStore.
 * Built-in profiles are read-only except for the \c enabled flag, which
 * is enforced both in the UI (fields disabled) and in the store
 * (\ref PromptProfileStore::upsert).
 */
class PromptProfilesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PromptProfilesDialog(PromptProfileStore *store,
                                  QWidget *parent = nullptr);

private slots:
    void onAccept();
    void onAddProfile();
    void onDuplicateProfile();
    void onDeleteProfile();
    void onProfileSelected();
    void onNameEdited();
    void onAppendToggled(bool checked);
    void onSystemEdited();
    void onModelTreeChanged(QTreeWidgetItem *item, int column);

private:
    void rebuildList(const QString &selectId = QString());
    void populateModelTree();
    void applyEditsToCurrent();
    void writeCurrentToStore(const PromptProfile &p);
    void updateTokenLabel();
    PromptProfile *currentProfile();

    PromptProfileStore *_store;

    // In-memory editable copy. We commit on Save.
    QList<PromptProfile> _profiles;

    QListWidget   *_list;
    QPushButton   *_addBtn;
    QPushButton   *_dupBtn;
    QPushButton   *_delBtn;

    QLineEdit     *_nameEdit;
    QCheckBox     *_appendCheck;
    QPlainTextEdit *_systemEdit;
    QLabel        *_tokenLabel;
    QTreeWidget   *_modelTree;

    // We block tree-itemChanged handling while we rebuild the tree to avoid
    // overwriting in-memory profile bindings with stale "unchecked" state.
    bool _suppressTreeSignals = false;
    bool _suppressEditSignals = false;
};

#endif // PROMPTPROFILESDIALOG_H
