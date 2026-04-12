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

#ifndef UPDATEDIALOGSH
#define UPDATEDIALOGSH

#include <QDialog>

class QLabel;
class QTextBrowser;
class QPushButton;
struct ChangelogSummary;

/**
 * \class UpdateAvailableDialog
 * \brief Dialog shown when a new version is available.
 *
 * Displays version info, a brief changelog summary (fetched from the website),
 * and action buttons: Update Now, After Exit, Download Manual, Skip.
 */
class UpdateAvailableDialog : public QDialog {
    Q_OBJECT

public:
    enum Result {
        UpdateNow,
        AfterExit,
        DownloadManual,
        Skip
    };

    explicit UpdateAvailableDialog(const QString &newVersion, const QString &currentVersion,
                                   QWidget *parent = nullptr);

    /** Populate the changelog area with fetched summary data. */
    void setChangelogSummary(const ChangelogSummary &summary);

    /** Show a loading indicator while changelog is being fetched. */
    void setChangelogLoading();

    /** Show fallback text if changelog fetch fails. */
    void setChangelogUnavailable();

    /** Returns which button the user clicked. */
    Result userChoice() const { return _choice; }

private:
    Result _choice = Skip;
    QLabel *_titleLabel;
    QLabel *_versionLabel;
    QTextBrowser *_changelogBrowser;
    QLabel *_linksLabel;
    QPushButton *_updateNowBtn;
    QPushButton *_afterExitBtn;
    QPushButton *_downloadBtn;
    QPushButton *_skipBtn;
};

/**
 * \class PostUpdateDialog
 * \brief Dialog shown after a successful update on app restart.
 *
 * Displays "Update Successful" title, the version info, embedded changelog
 * summary fetched from the website, and an OK button.
 */
class PostUpdateDialog : public QDialog {
    Q_OBJECT

public:
    explicit PostUpdateDialog(const QString &currentVersion, QWidget *parent = nullptr);

    /** Populate the changelog area with fetched summary data. */
    void setChangelogSummary(const ChangelogSummary &summary);

    /** Show a loading indicator. */
    void setChangelogLoading();

    /** Show fallback if fetch fails. */
    void setChangelogUnavailable();

private:
    QLabel *_titleLabel;
    QLabel *_versionLabel;
    QTextBrowser *_changelogBrowser;
    QLabel *_linksLabel;
    QPushButton *_okBtn;
};

#endif // UPDATEDIALOGSH
