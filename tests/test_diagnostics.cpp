// Health checks, the payload web app parsers, the SDK compare parser, and the page's network check on loopback.
#include "diagnostics.h"
#include "diagnostics_page.h"
#include "flow_stats.h"
#include "mavlink_dict.h"
#include "telemetry.h"
#include "web_settings_page.h"

#include <QtTest>

static CapturedFrame rx(const mavlink_message_t &m, qint64 ms)
{
    uint8_t b[MAVLINK_MAX_PACKET_LEN];
    CapturedFrame c;
    c.dir = CapturedFrame::Rx;
    c.monoNs = ms * 1000000;
    c.frame = parseDatagram(b, mavlink_msg_to_send_buffer(b, &m)).at(0);
    return c;
}

static const DiagCheck *find(const QList<DiagCheck> &l, const QString &name)
{
    for (const DiagCheck &c : l)
        if (c.name == name) return &c;
    return nullptr;
}

class TestDiagnostics : public QObject
{
    Q_OBJECT
private slots:
    void disconnectedSaysOnlyThat()
    {
        const QList<DiagCheck> l = linkChecks(DiagInput{});
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].level, DiagCheck::Info);
    }

    void silentSocketFailsWithAHint()
    {
        DiagInput in;
        in.state = LinkState::SocketOpen;
        const QList<DiagCheck> l = linkChecks(in);
        QCOMPARE(l.size(), 1);
        QCOMPARE(l[0].level, DiagCheck::Fail);
    }

    void liveLinkChecks()
    {
        MavlinkDict dict;
        QString err;
        QVERIFY2(dict.load(MavlinkDict::defaultDir(), QStringLiteral("ardupilotmega.xml"), &err), qPrintable(err));
        TelemetryStore tel;
        const qint64 now = 100000;
        mavlink_message_t m;
        mavlink_msg_heartbeat_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &m, MAV_TYPE_CAMERA, MAV_AUTOPILOT_INVALID, 0, 0, MAV_STATE_CRITICAL);
        tel.add(rx(m, now - 500));
        float q[4] = {1, 0, 0, 0};
        mavlink_msg_gimbal_device_attitude_status_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 0, 0, 0, q, 0, 0, 0,
                                                            GIMBAL_DEVICE_ERROR_FLAGS_AT_ROLL_LIMIT | GIMBAL_DEVICE_ERROR_FLAGS_MOTOR_ERROR, NAN, NAN, 0);
        tel.add(rx(m, now - 100));

        DiagInput in;
        in.state = LinkState::Heartbeat;
        in.nowMs = now;
        in.timeoutMs = 3000;
        in.heartbeats[quint16(1 << 8 | TelemetryStore::kPayloadCore)] = now - 200;
        in.heartbeats[quint16(1 << 8 | MAV_COMP_ID_CAMERA)] = now - 500;
        in.heartbeats[quint16(1 << 8 | MAV_COMP_ID_GIMBAL)] = now - 9000;  // stale
        in.heartbeats[quint16(1 << 8 | 99)] = now - 100;                   // seen during a VIO boot
        in.telemetry = &tel;
        in.dict = &dict;
        in.traffic.sendErrors = 2;
        FlowStats flows;
        flows.rxSources[1 << 8 | 26].frames = 990;
        flows.rxSources[1 << 8 | 26].missing = 10;
        in.flows = &flows;

        const QList<DiagCheck> l = linkChecks(in);
        QCOMPARE(find(l, QStringLiteral("Connection"))->level, DiagCheck::Pass);
        QCOMPARE(find(l, QStringLiteral("Payload core"))->level, DiagCheck::Pass);
        const DiagCheck *cam = find(l, QStringLiteral("Camera"));
        QCOMPARE(cam->level, DiagCheck::Fail);  // MAV_STATE_CRITICAL
        QVERIFY2(cam->detail.contains(QStringLiteral("MAV_STATE_CRITICAL")), qPrintable(cam->detail));
        const DiagCheck *gimbal = find(l, QStringLiteral("Gimbal"));
        QCOMPARE(gimbal->level, DiagCheck::Fail);
        QVERIFY2(gimbal->detail.contains(QStringLiteral("9.0 s ago")), qPrintable(gimbal->detail));
        const DiagCheck *flags = find(l, QStringLiteral("Failure flags"));
        QCOMPARE(flags->level, DiagCheck::Fail);
        QVERIFY2(flags->detail.contains(QStringLiteral("AT_ROLL_LIMIT")) && flags->detail.contains(QStringLiteral("MOTOR_ERROR")), qPrintable(flags->detail));
        QVERIFY(find(l, QStringLiteral("Other senders"))->detail.contains(QStringLiteral("1/99")));
        QCOMPARE(find(l, QStringLiteral("Send errors"))->level, DiagCheck::Fail);
        const DiagCheck *seq = find(l, QStringLiteral("Sequence gaps"));
        QCOMPARE(seq->level, DiagCheck::Pass);  // 1.00 %, at the limit
        QVERIFY2(seq->detail.contains(QStringLiteral("10 of 1000")), qPrintable(seq->detail));
    }

    void otaPageVersions()
    {
        // Trimmed from a VIO's /update-ota (Setting App v2.7.0.12)
        const QByteArray html = "<h1>Software Package -  v307.59.17_build.20261102<span id=\"ubuntu-version-inline\"></span></h1>\n"
"            <li class=\"app-version-item\"><div class=\"app-version-info\">\n"
"              <span class=\"app-name\">Setting App</span>\n"
"              <span class=\"app-version\">v2.7.0.12</span><span class=\"app-date\">28102026</span></div></li>\n"
"            <li class=\"app-version-item\"><div class=\"app-version-info\">\n"
"              <span class=\"app-name\">Gimbal Firmware</span>\n"
"              <span class=\"app-version\">v7.9.0 - OFFICIAL</span></div></li>";
        const PayloadVersions v = parseOtaPage(html);
        QCOMPARE(v.package, QStringLiteral("v307.59.17_build.20261102"));
        QCOMPARE(v.apps.size(), 2);
        QCOMPARE(v.apps[1].first, QStringLiteral("Gimbal Firmware"));
        QCOMPARE(v.apps[1].second, QStringLiteral("v7.9.0 - OFFICIAL"));
        QVERIFY(parseOtaPage("<html>login</html>").apps.isEmpty());
    }

    void systemInfo()
    {
        const auto kv = parseSystemInfo("{\"payload_type\":\"VIO\",\"ubuntu_version\":\"Ubuntu 18.04\"}");
        QCOMPARE(kv.size(), 2);
        QCOMPARE(kv[0], qMakePair(QStringLiteral("payload_type"), QStringLiteral("VIO")));
        QVERIFY(parseSystemInfo("not json").isEmpty());
    }

    void sdkCompare()
    {
        const SdkUpdate u = parseSdkCompare("{\"status\":\"ahead\",\"ahead_by\":2,\"behind_by\":0,\n"
"            \"html_url\":\"https://github.com/Gremsy/PayloadSdk/compare/a...b\",\n"
"            \"commits\":[{\"sha\":\"1111111aaaa\",\"commit\":{\"message\":\"First\\n\\nbody\"}},{\"sha\":\"2222222bbbb\",\"commit\":{\"message\":\"Second\"}}]}");
        QVERIFY(u.error.isEmpty());
        QCOMPARE(u.aheadBy, 2);
        QCOMPARE(u.commits, (QStringList{QStringLiteral("1111111 First"), QStringLiteral("2222222 Second")}));
        QCOMPARE(parseSdkCompare("{\"message\":\"Not Found\"}").error, QStringLiteral("Not Found"));
        QVERIFY(!parseSdkCompare("<html>").error.isEmpty());
    }

    void reportGroupsByArea()
    {
        const QString r = diagReport({{DiagCheck::Pass, QStringLiteral("Link"), QStringLiteral("Connection"), QStringLiteral("ok")},
                                      {DiagCheck::Fail, QStringLiteral("Link"), QStringLiteral("Other"), QStringLiteral("bad")},
                                      {DiagCheck::Info, QStringLiteral("Payload"), QStringLiteral("Model"), QStringLiteral("Vio")}},
                                     {QStringLiteral("Header")});
        QVERIFY(r.startsWith(QStringLiteral("Header\n")));
        QCOMPARE(r.count(QStringLiteral("[Link]")), 1);
        QVERIFY(r.contains(QStringLiteral("  FAIL  Other: bad\n")));
    }

    void webSettingsUrl()
    {
        WebSettingsPage web;  // never shown: no browser starts
        web.setPayloadIp(QStringLiteral(" 192.168.55.1 "));
        QCOMPARE(web.homeUrl(), QUrl(QStringLiteral("http://192.168.55.1:8000/")));
        web.setPayloadIp(QStringLiteral("payload.local; rm"));
        QVERIFY(!web.homeUrl().isValid());
    }

    void networkCheckOnLoopback()
    {
        DiagnosticsPage page([] { return DiagInput{}; }, [] { return QStringList{QStringLiteral("h")}; });
        page.setPayloadIp(QStringLiteral("127.0.0.1"));
        QSignalSpy done(&page, &DiagnosticsPage::networkCheckFinished);
        page.runNetworkCheck();
        QVERIFY(done.wait(15000));
        const QList<DiagCheck> l = page.checks();
        QCOMPARE(find(l, QStringLiteral("PC adapter"))->level, DiagCheck::Pass);  // lo is on 127.0.0.0/8
        for (const char *n : {"Web settings (TCP 8000)", "RTSP video (TCP 8554)", "SSH (TCP 22)", "Installed software"})
            QVERIFY2(find(l, QLatin1String(n)), n);
        QVERIFY(page.report().contains(QStringLiteral("[Network]")));

        page.setPayloadIp(QStringLiteral("not-an-ip"));
        page.runNetworkCheck();
        QCOMPARE(find(page.checks(), QStringLiteral("Payload IP"))->level, DiagCheck::Fail);
    }
};

QTEST_MAIN(TestDiagnostics)
#include "test_diagnostics.moc"
