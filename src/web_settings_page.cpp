#include "web_settings_page.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#ifdef HAVE_WEBENGINE
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QWebEngineDownloadRequest>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif

WebSettingsPage::WebSettingsPage(QWidget *parent) : QWidget(parent)
{
    m_address = new QLabel;
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *home = new QPushButton(QStringLiteral("Home"));
    auto *browser = new QPushButton(QStringLiteral("Open in browser"));
    browser->setToolTip(QStringLiteral("Open the payload's web app in the system browser"));
    connect(home, &QPushButton::clicked, this, &WebSettingsPage::goHome);
    connect(browser, &QPushButton::clicked, this, [this] {
        if (homeUrl().isValid()) QDesktopServices::openUrl(homeUrl());
    });

    auto *bar = new QHBoxLayout;
    bar->addWidget(home);
#ifdef HAVE_WEBENGINE
    auto *back = new QPushButton(QStringLiteral("Back"));
    auto *reload = new QPushButton(QStringLiteral("Reload"));
    connect(back, &QPushButton::clicked, this, [this] { if (m_view) m_view->back(); });
    connect(reload, &QPushButton::clicked, this, [this] { if (m_view) m_view->reload(); });
    bar->addWidget(back);
    bar->addWidget(reload);
#endif
    bar->addWidget(m_address, 1);
    bar->addWidget(browser);

    m_body = new QVBoxLayout(this);
    m_body->addLayout(bar);
#ifndef HAVE_WEBENGINE
    auto *note = new QLabel(QStringLiteral("This build has no embedded browser (Qt WebEngine). Use \"Open in browser\".\n"
                                           "The payload's web app lists its software versions, settings, files and the update page."));
    note->setAlignment(Qt::AlignCenter);
    note->setObjectName(QStringLiteral("dim"));
    m_body->addWidget(note, 1);
#endif
}

QUrl WebSettingsPage::homeUrl() const
{
    QHostAddress a;
    if (!a.setAddress(m_ip) || a.protocol() != QAbstractSocket::IPv4Protocol) return {};
    return QUrl(QStringLiteral("http://%1:8000/").arg(a.toString()));
}

void WebSettingsPage::setPayloadIp(const QString &ip)
{
    const QUrl before = homeUrl();
    m_ip = ip.trimmed();
    m_address->setText(homeUrl().isValid() ? homeUrl().toString() : QStringLiteral("Set a valid payload IP"));
#ifdef HAVE_WEBENGINE
    if (m_view && homeUrl() != before) goHome();
#else
    Q_UNUSED(before)
#endif
}

void WebSettingsPage::goHome()
{
#ifdef HAVE_WEBENGINE
    if (m_view && homeUrl().isValid()) m_view->setUrl(homeUrl());
#endif
}

void WebSettingsPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
#ifdef HAVE_WEBENGINE
    if (m_view) return;
    // Off the record: a customer's payload leaves no cookies or cache on this PC
    auto *profile = new QWebEngineProfile;  // deleted after the view: a profile must outlive its pages
    connect(profile, &QWebEngineProfile::downloadRequested, this, [this](QWebEngineDownloadRequest *d) {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save file from the payload"),
                                                          QDir::home().filePath(d->downloadFileName()));
        if (path.isEmpty()) {
            d->cancel();
            return;
        }
        d->setDownloadDirectory(QFileInfo(path).absolutePath());
        d->setDownloadFileName(QFileInfo(path).fileName());
        d->accept();
    });
    m_view = new QWebEngineView(this);
    m_view->setPage(new QWebEnginePage(profile, m_view));
    connect(m_view, &QObject::destroyed, profile, &QObject::deleteLater);
    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &u) { m_address->setText(u.toString()); });
    m_body->addWidget(m_view, 1);
    goHome();
#endif
}
