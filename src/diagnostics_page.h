#pragma once
// DIAGNOSE > Health check: live link checks (1 Hz), an on-demand network check of the payload (adapters on its subnet,
// its TCP services, the web app's version list), an SDK update check against GitHub, and a text report for a support case.

#include "diagnostics.h"

#include <QWidget>

#include <functional>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QTreeWidget;

class DiagnosticsPage : public QWidget
{
    Q_OBJECT
public:
    // source: the current inputs (pointers inside stay owned by the caller); header: report context lines
    DiagnosticsPage(std::function<DiagInput()> source, std::function<QStringList()> header, QWidget *parent = nullptr);
    void setPayloadIp(const QString &ip) { m_ip = ip.trimmed(); }

    void runNetworkCheck();
    void checkSdkUpdate();
    QList<DiagCheck> checks() const;  // everything shown, in display order
    QString report() const;

signals:
    void networkCheckFinished();
    void sdkCheckFinished();

private:
    void refresh();
    void exportReport();
    void probePort(quint16 port, const QString &name, DiagCheck::Level closedLevel, const QString &closedText);
    void fetch(const QString &path, std::function<void(const QByteArray &body, const QString &error)> done);
    void netStep();  // one network probe finished

    std::function<DiagInput()> m_source;
    std::function<QStringList()> m_header;
    QString m_ip;
    QList<DiagCheck> m_net, m_software;
    int m_netPending = 0;
    QNetworkAccessManager *m_nam;
    QTreeWidget *m_tree;
    QLabel *m_status;
    QPushButton *m_netBtn, *m_sdkBtn;
};
