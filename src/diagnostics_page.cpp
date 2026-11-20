#include "diagnostics_page.h"

#include "payload_link.h"
#include "theme.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QPushButton>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#ifndef SDK_COMMIT
#define SDK_COMMIT ""
#endif
static const char *const kSdkBranch = "payloadsdk_v3";  // .gitmodules
static const int kProbeMs = 2000;

DiagnosticsPage::DiagnosticsPage(std::function<DiagInput()> source, std::function<QStringList()> header, QWidget *parent)
    : QWidget(parent), m_source(std::move(source)), m_header(std::move(header)), m_nam(new QNetworkAccessManager(this))
{
    m_netBtn = new QPushButton(QStringLiteral("Run network check"));
    m_netBtn->setToolTip(QStringLiteral("Read-only: this PC's adapters, the payload's TCP services and its web app's version list"));
    m_sdkBtn = new QPushButton(QStringLiteral("Check SDK update"));
    m_sdkBtn->setToolTip(QStringLiteral("Asks GitHub whether Gremsy's PayloadSDK has commits this build does not include"));
    auto *exportBtn = new QPushButton(QStringLiteral("Export report…"));
    exportBtn->setToolTip(QStringLiteral("Save everything on this page as a text file for a support case"));
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("dim"));
    connect(m_netBtn, &QPushButton::clicked, this, &DiagnosticsPage::runNetworkCheck);
    connect(m_sdkBtn, &QPushButton::clicked, this, &DiagnosticsPage::checkSdkUpdate);
    connect(exportBtn, &QPushButton::clicked, this, &DiagnosticsPage::exportReport);

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("diagTree"));
    m_tree->setHeaderLabels({QStringLiteral("Result"), QStringLiteral("Area"), QStringLiteral("Check"), QStringLiteral("Detail")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tree->setColumnWidth(0, 70);
    m_tree->setColumnWidth(1, 110);
    m_tree->setColumnWidth(2, 170);

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_netBtn);
    bar->addWidget(m_sdkBtn);
    bar->addWidget(exportBtn);
    bar->addWidget(m_status, 1);
    auto *l = new QVBoxLayout(this);
    l->addLayout(bar);
    l->addWidget(m_tree);

    m_software.append({DiagCheck::Info, QStringLiteral("Software"), QStringLiteral("PayloadSDK in this build"),
                       QStringLiteral("%1 (%2)").arg(PayloadLink::sdkVersion(), QLatin1String(SDK_COMMIT).left(7))});
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, &DiagnosticsPage::refresh);
    t->start(1000);
    refresh();
}

QList<DiagCheck> DiagnosticsPage::checks() const { return linkChecks(m_source()) + m_net + m_software; }

QString DiagnosticsPage::report() const { return diagReport(checks(), m_header()); }

void DiagnosticsPage::refresh()
{
    if (!isVisible() && m_tree->topLevelItemCount()) return;
    const QList<DiagCheck> all = checks();
    while (m_tree->topLevelItemCount() > all.size()) delete m_tree->takeTopLevelItem(m_tree->topLevelItemCount() - 1);
    for (int i = 0; i < all.size(); ++i) {
        QTreeWidgetItem *it = m_tree->topLevelItem(i);
        if (!it) it = new QTreeWidgetItem(m_tree);
        const DiagCheck &c = all[i];
        it->setText(0, diagLevelText(c.level));
        it->setText(1, c.area);
        it->setText(2, c.name);
        it->setText(3, c.detail);
        it->setToolTip(3, c.detail);
        static const char *const colors[] = {Theme::Ok, Theme::Dim, Theme::Warn, Theme::Bad};
        it->setForeground(0, QColor(colors[c.level]));
    }
}

void DiagnosticsPage::runNetworkCheck()
{
    if (m_netPending) return;
    m_net.clear();
    const QString N = QStringLiteral("Network");
    QHostAddress payload;
    if (!payload.setAddress(m_ip) || payload.protocol() != QAbstractSocket::IPv4Protocol) {
        m_net.append({DiagCheck::Fail, N, QStringLiteral("Payload IP"), QStringLiteral("\"%1\" is not an IPv4 address").arg(m_ip)});
        refresh();
        emit networkCheckFinished();
        return;
    }

    // Which of this PC's adapters share the payload's subnet: none = unreachable, several = replies may take another one
    QStringList onSubnet;
    for (const QNetworkInterface &i : QNetworkInterface::allInterfaces()) {
        if (!(i.flags() & QNetworkInterface::IsUp)) continue;
        for (const QNetworkAddressEntry &e : i.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && e.prefixLength() > 0 && payload.isInSubnet(e.ip(), e.prefixLength()))
                onSubnet << QStringLiteral("%1 (%2/%3)").arg(i.humanReadableName(), e.ip().toString()).arg(e.prefixLength());
    }
    if (onSubnet.isEmpty())
        m_net.append({DiagCheck::Fail, N, QStringLiteral("PC adapter"),
                      QStringLiteral("No adapter of this PC is on %1's network: check the cable or USB network, or set a static IP").arg(m_ip)});
    else if (onSubnet.size() > 1)
        m_net.append({DiagCheck::Warn, N, QStringLiteral("PC adapter"),
                      QStringLiteral("Several adapters on the payload's network: %1. Traffic may leave on one and return on another").arg(onSubnet.join(QStringLiteral(", ")))});
    else
        m_net.append({DiagCheck::Pass, N, QStringLiteral("PC adapter"), onSubnet.first()});

    m_netPending = 5;
    m_netBtn->setEnabled(false);
    m_status->setText(QStringLiteral("Checking %1…").arg(m_ip));
    probePort(8000, QStringLiteral("Web settings (TCP 8000)"), DiagCheck::Warn, QStringLiteral("closed: the payload answered, its web app is not running"));
    probePort(8554, QStringLiteral("RTSP video (TCP 8554)"), DiagCheck::Info, QStringLiteral("closed: RTSP is off (the unit may stream RTP/TS instead)"));
    probePort(22, QStringLiteral("SSH (TCP 22)"), DiagCheck::Info, QStringLiteral("closed"));
    fetch(QStringLiteral("/system-info"), [this](const QByteArray &body, const QString &error) {
        if (error.isEmpty())
            for (const auto &[k, v] : parseSystemInfo(body)) m_net.append({DiagCheck::Info, QStringLiteral("Payload"), k, v});
        netStep();
    });
    fetch(QStringLiteral("/update-ota"), [this](const QByteArray &body, const QString &error) {
        const QString P = QStringLiteral("Payload");
        if (!error.isEmpty()) {
            m_net.append({DiagCheck::Warn, P, QStringLiteral("Installed software"), QStringLiteral("Web app did not answer: ") + error});
        } else {
            const PayloadVersions v = parseOtaPage(body);
            if (!v.package.isEmpty()) m_net.append({DiagCheck::Info, P, QStringLiteral("Software package"), v.package});
            for (const auto &[name, version] : v.apps) m_net.append({DiagCheck::Info, P, name, version});
            if (v.package.isEmpty() && v.apps.isEmpty())
                m_net.append({DiagCheck::Warn, P, QStringLiteral("Installed software"), QStringLiteral("The update page had no version list (other firmware?)")});
        }
        netStep();
    });
    refresh();
}

void DiagnosticsPage::probePort(quint16 port, const QString &name, DiagCheck::Level closedLevel, const QString &closedText)
{
    auto *s = new QTcpSocket(this);
    auto *timer = new QTimer(s);
    timer->setSingleShot(true);
    auto finish = [this, s, name](DiagCheck::Level level, const QString &detail) {
        if (s->property("done").toBool()) return;
        s->setProperty("done", true);
        m_net.append({level, QStringLiteral("Network"), name, detail});
        s->abort();
        s->deleteLater();
        netStep();
    };
    connect(s, &QTcpSocket::connected, s, [finish] { finish(DiagCheck::Pass, QStringLiteral("open")); });
    connect(s, &QTcpSocket::errorOccurred, s, [finish, s, closedLevel, closedText](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::ConnectionRefusedError) finish(closedLevel, closedText);
        else finish(DiagCheck::Fail, s->errorString());
    });
    connect(timer, &QTimer::timeout, s, [finish] { finish(DiagCheck::Fail, QStringLiteral("no answer in %1 s").arg(kProbeMs / 1000)); });
    timer->start(kProbeMs);
    s->connectToHost(m_ip, port);
}

void DiagnosticsPage::fetch(const QString &path, std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://%1:8000%2").arg(m_ip, path)));
    req.setTransferTimeout(5000);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [r, done] {
        r->deleteLater();
        done(r->readAll(), r->error() == QNetworkReply::NoError ? QString() : r->errorString());
    });
}

void DiagnosticsPage::netStep()
{
    if (--m_netPending > 0) return;
    m_netBtn->setEnabled(true);
    m_status->setText(QStringLiteral("Network check finished %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
    refresh();
    emit networkCheckFinished();
}

void DiagnosticsPage::checkSdkUpdate()
{
    const QString S = QStringLiteral("Software");
    const QString commit = QLatin1String(SDK_COMMIT);
    m_software.erase(std::remove_if(m_software.begin(), m_software.end(), [](const DiagCheck &c) { return c.name == QLatin1String("PayloadSDK upstream"); }),
                     m_software.end());
    if (commit.isEmpty()) {
        m_software.append({DiagCheck::Warn, S, QStringLiteral("PayloadSDK upstream"), QStringLiteral("This build does not know its SDK commit")});
        refresh();
        emit sdkCheckFinished();
        return;
    }
    m_sdkBtn->setEnabled(false);
    m_status->setText(QStringLiteral("Asking GitHub…"));
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/Gremsy/PayloadSdk/compare/%1...%2").arg(commit, QLatin1String(kSdkBranch))));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "MavlinkDebugApp");  // GitHub refuses requests without one
    req.setTransferTimeout(10000);
    QNetworkReply *r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, S] {
        r->deleteLater();
        const SdkUpdate u = parseSdkCompare(r->readAll());
        const QString name = QStringLiteral("PayloadSDK upstream");
        if (!u.error.isEmpty())
            m_software.append({DiagCheck::Warn, S, name, QStringLiteral("Could not check: %1").arg(r->error() == QNetworkReply::NoError ? u.error : r->errorString())});
        else if (u.aheadBy == 0)
            m_software.append({DiagCheck::Pass, S, name, QStringLiteral("Up to date with %1").arg(QLatin1String(kSdkBranch))});
        else
            m_software.append({DiagCheck::Warn, S, name,
                               QStringLiteral("%1 newer commit(s) on %2; updating needs a new app build. Newest: %3. %4")
                                   .arg(u.aheadBy).arg(QLatin1String(kSdkBranch), u.commits.isEmpty() ? QString() : u.commits.last(), u.url)});
        m_sdkBtn->setEnabled(true);
        m_status->clear();
        refresh();
        emit sdkCheckFinished();
    });
}

void DiagnosticsPage::exportReport()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString name = QStringLiteral("payload-diagnostic-%1.txt").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export diagnostic report"), dir + QLatin1Char('/') + name,
                                                      QStringLiteral("Text (*.txt)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text) || f.write(report().toUtf8()) < 0) {
        QMessageBox::warning(this, QStringLiteral("Report not saved"), f.errorString());
        return;
    }
    m_status->setText(QStringLiteral("Saved %1").arg(path));
}
