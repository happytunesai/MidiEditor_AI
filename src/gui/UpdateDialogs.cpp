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

#include "UpdateDialogs.h"
#include "UpdateChecker.h"

#include <QApplication>
#include <QLabel>
#include <QTextBrowser>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>

// ============================================================================
//  UpdateAvailableDialog
// ============================================================================

UpdateAvailableDialog::UpdateAvailableDialog(const QString &newVersion,
                                             const QString &currentVersion,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Update Available"));
    setMinimumWidth(500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Title with icon
    _titleLabel = new QLabel(this);
    _titleLabel->setText(tr("<h2>Update Available</h2>"));
    _titleLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(_titleLabel);

    // Version info
    _versionLabel = new QLabel(this);
    _versionLabel->setText(tr("<b>Version %1</b> is available &nbsp; (current: %2)")
                               .arg(newVersion, currentVersion));
    layout->addWidget(_versionLabel);

    // Changelog area
    _changelogBrowser = new QTextBrowser(this);
    _changelogBrowser->setReadOnly(true);
    _changelogBrowser->setOpenExternalLinks(true);
    _changelogBrowser->setMinimumHeight(120);
    _changelogBrowser->setMaximumHeight(250);
    _changelogBrowser->setHtml(tr("<i>Loading changelog...</i>"));
    layout->addWidget(_changelogBrowser);

    // Links
    _linksLabel = new QLabel(this);
    _linksLabel->setOpenExternalLinks(true);
    _linksLabel->setText(
        tr("<a href='https://happytunesai.github.io/MidiEditor_AI/changelog.html'>Full Changelog</a>"
           " &nbsp; | &nbsp; "
           "<a href='https://happytunesai.github.io/MidiEditor_AI/'>Website</a>"));
    layout->addWidget(_linksLabel);

    // Info text
    auto *infoLabel = new QLabel(this);
    infoLabel->setText(tr("The app will save your work and restart automatically."));
    infoLabel->setStyleSheet("color: gray;");
    layout->addWidget(infoLabel);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    _updateNowBtn = new QPushButton(tr("Update Now"), this);
    _afterExitBtn = new QPushButton(tr("After Exit"), this);
    _downloadBtn = new QPushButton(tr("Download Manual"), this);
    _skipBtn = new QPushButton(tr("Skip"), this);

    _updateNowBtn->setDefault(true);

    btnLayout->addStretch();
    btnLayout->addWidget(_updateNowBtn);
    btnLayout->addWidget(_afterExitBtn);
    btnLayout->addWidget(_downloadBtn);
    btnLayout->addWidget(_skipBtn);

    layout->addLayout(btnLayout);

    // Connect buttons
    connect(_updateNowBtn, &QPushButton::clicked, this, [this]() {
        _choice = UpdateNow;
        accept();
    });
    connect(_afterExitBtn, &QPushButton::clicked, this, [this]() {
        _choice = AfterExit;
        accept();
    });
    connect(_downloadBtn, &QPushButton::clicked, this, [this]() {
        _choice = DownloadManual;
        accept();
    });
    connect(_skipBtn, &QPushButton::clicked, this, [this]() {
        _choice = Skip;
        reject();
    });
}

void UpdateAvailableDialog::setChangelogSummary(const ChangelogSummary &summary)
{
    QString html;

    if (!summary.title.isEmpty()) {
        html += QString("<b>%1</b><br><br>").arg(summary.title.toHtmlEscaped());
    }

    if (!summary.bullets.isEmpty()) {
        html += "<ul style='margin-left: 16px;'>";
        for (const QString &bullet : summary.bullets) {
            html += QString("<li>%1</li>").arg(bullet.toHtmlEscaped());
        }
        html += "</ul>";
    }

    if (html.isEmpty()) {
        html = tr("<i>No changelog details available for this version.</i>");
    }

    _changelogBrowser->setHtml(html);
}

void UpdateAvailableDialog::setChangelogLoading()
{
    _changelogBrowser->setHtml(tr("<i>Loading changelog...</i>"));
}

void UpdateAvailableDialog::setChangelogUnavailable()
{
    _changelogBrowser->setHtml(tr("<i>Could not load changelog. Check the "
        "<a href='https://happytunesai.github.io/MidiEditor_AI/changelog.html'>website</a> "
        "for details.</i>"));
}

// ============================================================================
//  PostUpdateDialog
// ============================================================================

PostUpdateDialog::PostUpdateDialog(const QString &currentVersion, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Update Successful"));
    setMinimumWidth(500);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    // Title
    _titleLabel = new QLabel(this);
    _titleLabel->setText(tr("<h2>%1 Update Successful</h2>").arg(
        QChar(0x2705))); // checkmark emoji
    _titleLabel->setAlignment(Qt::AlignLeft);
    layout->addWidget(_titleLabel);

    // Version info
    _versionLabel = new QLabel(this);
    _versionLabel->setText(tr("MidiEditor AI has been updated to <b>version %1</b>.")
                               .arg(currentVersion));
    layout->addWidget(_versionLabel);

    // Changelog area
    _changelogBrowser = new QTextBrowser(this);
    _changelogBrowser->setReadOnly(true);
    _changelogBrowser->setOpenExternalLinks(true);
    _changelogBrowser->setMinimumHeight(140);
    _changelogBrowser->setMaximumHeight(300);
    _changelogBrowser->setHtml(tr("<i>Loading patch notes...</i>"));
    layout->addWidget(_changelogBrowser);

    // Links
    _linksLabel = new QLabel(this);
    _linksLabel->setOpenExternalLinks(true);
    _linksLabel->setText(
        tr("<a href='https://happytunesai.github.io/MidiEditor_AI/changelog.html'>Full Changelog</a>"
           " &nbsp; | &nbsp; "
           "<a href='https://happytunesai.github.io/MidiEditor_AI/'>Website</a>"));
    layout->addWidget(_linksLabel);

    // OK button
    auto *btnLayout = new QHBoxLayout();
    _okBtn = new QPushButton(tr("OK"), this);
    _okBtn->setDefault(true);
    _okBtn->setMinimumWidth(100);
    btnLayout->addStretch();
    btnLayout->addWidget(_okBtn);
    layout->addLayout(btnLayout);

    connect(_okBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void PostUpdateDialog::setChangelogSummary(const ChangelogSummary &summary)
{
    QString html;

    if (!summary.title.isEmpty()) {
        html += QString("<b>%1</b><br><br>").arg(summary.title.toHtmlEscaped());
    }

    if (!summary.bullets.isEmpty()) {
        html += "<b>What's new:</b><ul style='margin-left: 16px;'>";
        for (const QString &bullet : summary.bullets) {
            html += QString("<li>%1</li>").arg(bullet.toHtmlEscaped());
        }
        html += "</ul>";
    }

    if (html.isEmpty()) {
        html = tr("<i>No patch notes available for this version.</i>");
    }

    _changelogBrowser->setHtml(html);
}

void PostUpdateDialog::setChangelogLoading()
{
    _changelogBrowser->setHtml(tr("<i>Loading patch notes...</i>"));
}

void PostUpdateDialog::setChangelogUnavailable()
{
    _changelogBrowser->setHtml(tr("<i>Could not load patch notes. Check the "
        "<a href='https://happytunesai.github.io/MidiEditor_AI/changelog.html'>website</a> "
        "for details.</i>"));
}
