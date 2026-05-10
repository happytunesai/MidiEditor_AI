/*
 * MidiEditor AI - WelcomeBackDialog implementation.
 */

#include "WelcomeBackDialog.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

WelcomeBackDialog::WelcomeBackDialog(const QString &hostName,
                                      int acceptedHunkCount,
                                      int rejectedCommitCount,
                                      const QString &divergedFilePath,
                                      QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("Welcome back"));
    setModal(false);
    resize(440, 240);

    QVBoxLayout *root = new QVBoxLayout(this);

    QString headline = tr(
        "<b>Welcome back to the session hosted by %1.</b>")
        .arg(hostName.toHtmlEscaped());
    QLabel *headlineLabel = new QLabel(headline, this);
    headlineLabel->setTextFormat(Qt::RichText);
    headlineLabel->setWordWrap(true);
    root->addWidget(headlineLabel);

    QString summary;
    if (acceptedHunkCount > 0) {
        summary = tr("Applied <b>%1 change(s)</b> from the host into your file.")
                      .arg(acceptedHunkCount);
    } else {
        summary = tr("No host-side changes to apply.");
    }
    QLabel *summaryLabel = new QLabel(summary, this);
    summaryLabel->setTextFormat(Qt::RichText);
    summaryLabel->setWordWrap(true);
    root->addWidget(summaryLabel);

    if (rejectedCommitCount > 0) {
        QString rejTxt = tr(
            "<span style='color:#c66;'>The host turned down %1 of your "
            "commit(s).</span> Your pre-merge state has been saved to "
            "the same folder so you can recover any rejected work "
            "manually:")
            .arg(rejectedCommitCount);
        QLabel *rejLabel = new QLabel(rejTxt, this);
        rejLabel->setTextFormat(Qt::RichText);
        rejLabel->setWordWrap(true);
        root->addWidget(rejLabel);

        if (!divergedFilePath.isEmpty()) {
            QLabel *pathLabel = new QLabel(
                QStringLiteral("<a href=\"file:///%1\">%2</a>")
                    .arg(divergedFilePath, divergedFilePath.toHtmlEscaped()),
                this);
            pathLabel->setTextFormat(Qt::RichText);
            pathLabel->setOpenExternalLinks(false);
            pathLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
            connect(pathLabel, &QLabel::linkActivated, this,
                    [](const QString &link) {
                        QDesktopServices::openUrl(QUrl(link));
                    });
            root->addWidget(pathLabel);
        }
    }

    root->addStretch(1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    QPushButton *okBtn = new QPushButton(tr("OK, got it"), this);
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(okBtn);
    root->addLayout(btnRow);
}
