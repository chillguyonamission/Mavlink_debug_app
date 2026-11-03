// End-to-end over loopback: a synthetic MPEG-TS (H.264 and KLV muxed by mpegtsmux, RTP/MP2T over UDP) goes through the
// real KlvReceiver into MetadataModel. This proves the processing path. It is NOT proof of compatibility with any firmware.
#include "klv_builder.h"
#include "klv_page.h"
#include "klv_receiver.h"
#include "main_window.h"
#include "metadata_model.h"
#include "payload_link.h"
#include "theme.h"
#include "video_page.h"
#include "video_player.h"

#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QtTest>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include "fake_payload.h"

using State = KlvReceiver::State;

namespace {

// Each call gets its own port: a sender and a receiver of an earlier test, or of a soak run alongside, must not meet.
int nextPort()
{
    static int port = 20000 + int(QCoreApplication::applicationPid() % 10000) * 2;
    return port += 2;
}

enum class Streams { VideoAndKlv, VideoOnly, KlvOnly };

class Sender
{
public:
    Sender(int port, Streams streams)
    {
        const QString video = streams == Streams::KlvOnly ? QString() :
            QStringLiteral("videotestsrc is-live=true pattern=ball ! video/x-raw,width=320,height=240,framerate=30/1 ! "
                           "openh264enc ! h264parse config-interval=1 ! queue ! mux. ");
        const QString klvBranch = streams == Streams::VideoOnly ? QString() :
            QStringLiteral("appsrc name=klv is-live=true do-timestamp=true format=time caps=meta/x-klv,parsed=true ! queue ! mux. ");
        const QString desc = video + klvBranch +
            QStringLiteral("mpegtsmux name=mux alignment=7 ! rtpmp2tpay ! udpsink host=127.0.0.1 port=%1 sync=false").arg(port);
        GError *err = nullptr;
        pipeline = gst_parse_launch(desc.toUtf8().constData(), &err);
        if (err) qFatal("sender: %s", err->message);
        klv = gst_bin_get_by_name(GST_BIN(pipeline), "klv");
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    }
    ~Sender()
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        if (klv) gst_object_unref(klv);
        gst_object_unref(pipeline);
    }
    void push(const QByteArray &bytes)
    {
        if (klv) gst_app_src_push_buffer(GST_APP_SRC(klv), gst_buffer_new_memdup(bytes.constData(), bytes.size()));
    }
    // A packet with an advancing tag 2, as a live sender makes.
    void pushNext()
    {
        static quint64 us = 1700000000123456ull;
        us += 33333;
        push(buildPacket({{2, be(us, 8)}, {13, be(0x0F6D1B55, 4)}, {16, be(0x5e74, 2)}, {65, QByteArray(1, 8)}}));
    }

private:
    GstElement *pipeline = nullptr, *klv = nullptr;
};

KlvReceiver::Config loopback(int port)
{
    KlvReceiver::Config c = KlvReceiver::defaults();
    c.address = QStringLiteral("127.0.0.1");
    c.port = port;
    return c;
}

int threadCount() { return QDir(QStringLiteral("/proc/self/task")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(); }
int fileCount()
{
    int n = 0;
    for (const QFileInfo &f : QDir(QStringLiteral("/proc/self/fd")).entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot))
        n += !f.symLinkTarget().contains(QStringLiteral("dmabuf"));
    return n;
}

}  // namespace

class TestKlvStream : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);  // QSettings go to a test location
        QCoreApplication::setOrganizationName(QStringLiteral("GremsyTest"));
        QSettings().clear();
        gst_init(nullptr, nullptr);
        for (const char *e : {"openh264enc", "mpegtsmux", "rtpmp2tpay", "appsrc"})
            if (GstElementFactory *f = gst_element_factory_find(e)) gst_object_unref(f);
            else QSKIP(qPrintable(QStringLiteral("the synthetic sender needs %1").arg(QLatin1String(e))));
    }
    void init() { QSettings().clear(); }

    void defaultsAreValid()
    {
        QVERIFY(KlvReceiver::validate(KlvReceiver::defaults()).isEmpty());
        QVERIFY(KlvReceiver::missingElements().isEmpty());
    }

    void validationRejectsBadSettings()
    {
        const auto with = [](auto change) {
            KlvReceiver::Config c = KlvReceiver::defaults();
            change(c);
            return KlvReceiver::validate(c);
        };
        QVERIFY(!with([](auto &c) { c.address = QStringLiteral("not-an-ip ! fakesink"); }).isEmpty());
        QVERIFY(!with([](auto &c) { c.address = QStringLiteral("::1"); }).isEmpty());  // IPv4 only
        QVERIFY(!with([](auto &c) { c.port = 0; }).isEmpty());
        QVERIFY(!with([](auto &c) { c.port = 70000; }).isEmpty());
        QVERIFY(!with([](auto &c) { c.iface = QStringLiteral("nosuchnic0"); }).isEmpty());
        QVERIFY(!with([](auto &c) { c.media = QStringLiteral("audio"); }).isEmpty());
        QVERIFY(!with([](auto &c) { c.encoding = QStringLiteral("H264"); }).isEmpty());
        QVERIFY(!with([](auto &c) { c.clockRate = 0; }).isEmpty());
        QVERIFY(with([](auto &c) { c.iface = QStringLiteral("lo"); }).isEmpty());
        QVERIFY(KlvReceiver::interfaces().contains(QStringLiteral("lo")));
    }

    void settingsPersistOnlyWhenValid()
    {
        KlvReceiver::Config good = loopback(4242);
        {
            KlvReceiver r;
            QCOMPARE(r.config(), KlvReceiver::defaults());
            QVERIFY(r.apply(good).isEmpty());
            KlvReceiver::Config bad = good;
            bad.port = 70000;
            QVERIFY(!r.apply(bad).isEmpty());
            QCOMPARE(r.config(), good);
            QVERIFY(r.lastError().contains(QStringLiteral("rejected")));
            QVERIFY(!r.isRunning());  // apply() never starts a stopped receiver
        }
        KlvReceiver reloaded;
        QCOMPARE(reloaded.config(), good);

        QSettings().setValue(QStringLiteral("klv/port"), 99999);  // a damaged file falls back rather than failing later
        KlvReceiver damaged;
        QCOMPARE(damaged.config(), KlvReceiver::defaults());
    }

    void silentEndpointIsWaiting()
    {
        KlvReceiver r;
        r.apply(loopback(nextPort()));
        QCOMPARE(r.state(), State::Stopped);
        r.start();
        QCOMPARE(r.state(), State::Waiting);
        QTest::qWait(1500);
        QCOMPARE(r.state(), State::Waiting);
        QCOMPARE(r.lastError(), QString());
        r.stop();
        QCOMPARE(r.state(), State::Stopped);
    }

    void videoAndKlvThroughTsDemux()
    {
        KlvReceiver r;
        MetadataModel model;
        connect(&r, &KlvReceiver::klvReceived, &model, &MetadataModel::ingestNow);
        const int port = nextPort();
        r.apply(loopback(port));
        r.start();

        auto sender = std::make_unique<Sender>(port, Streams::VideoAndKlv);
        QTimer pusher;
        connect(&pusher, &QTimer::timeout, [&] { sender->pushNext(); });
        pusher.start(33);

        QTRY_VERIFY_WITH_TIMEOUT(model.decoded() >= 20, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(r.videoFrames() > 10, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Decoding, 3000);
        QCOMPARE(model.malformed(), 0u);
        QCOMPARE(r.frame().size(), QSize(320, 240));
        QVERIFY(r.hasKlvPad());
        QVERIFY(r.hasVideoPad());
        model.tick(QDateTime::currentMSecsSinceEpoch());
        QCOMPARE(model.status(13), MetadataModel::Valid);
        QCOMPARE(model.field(13)["value"].toString(), QString::number(0x0F6D1B55 * 90.0 / 2147483647, 'f', 7));
        QVERIFY(model.field(2)["value"].toString().startsWith(QStringLiteral("2023-11-14T22:13")));
        QCOMPARE(model.status(5), MetadataModel::NoData);  // not sent: no fake zero
        QVERIFY(model.duplicates() == 0);

        bool klvPad = false;
        for (const auto &p : r.pads()) klvPad |= p.caps.startsWith(QStringLiteral("meta/x-klv")) && p.linked;
        QVERIFY(klvPad);
        QTRY_VERIFY_WITH_TIMEOUT(r.pids().size() >= 3, 3000);  // PAT, PMT, video, KLV
        QVERIFY(r.udpRate() > 0);

        // Malformed KLV must not stop later valid packets.
        const quint64 before = model.decoded();
        sender->push(buildPacket({{65, QByteArray(1, 8)}}, true));
        sender->push(QByteArray("garbage, no key at all"));
        QTRY_VERIFY_WITH_TIMEOUT(model.malformed() >= 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(model.decoded() > before + 5, 3000);
        QVERIFY(r.isRunning());

        // Sender loss: stale metadata and Waiting, and no restart since nothing failed.
        pusher.stop();
        sender.reset();
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Waiting, 4000);
        QTest::qWait(int(MetadataModel::kStaleMs) + 300);
        QCOMPARE(model.freshness(), QString("stale"));
        QCOMPARE(model.status(13), MetadataModel::Stale);
        QVERIFY(!model.field(13)["value"].toString().isEmpty());
        QCOMPARE(r.lastError(), QString());

        // And recovery by itself when traffic resumes.
        sender = std::make_unique<Sender>(port, Streams::VideoAndKlv);
        pusher.start(33);
        const quint64 resumed = model.decoded();
        QTRY_VERIFY_WITH_TIMEOUT(model.decoded() > resumed + 10, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Decoding, 5000);
        QCOMPARE(model.freshness(), QString("live"));
        pusher.stop();
    }

    void videoWithoutKlvIsExplicit()
    {
        KlvReceiver r;
        const int port = nextPort();
        r.apply(loopback(port));
        r.start();
        Sender sender(port, Streams::VideoOnly);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::NoKlvPad, 15000);
        QVERIFY(!r.hasKlvPad());
        QVERIFY(r.videoFrames() > 0);
    }

    void klvOnlyStreamHasAnExplicitNoVideoState()
    {
        KlvReceiver r;
        MetadataModel model;
        connect(&r, &KlvReceiver::klvReceived, &model, &MetadataModel::ingestNow);
        const int port = nextPort();
        r.apply(loopback(port));
        r.start();
        Sender sender(port, Streams::KlvOnly);
        QTimer pusher;
        connect(&pusher, &QTimer::timeout, [&] { sender.pushNext(); });
        pusher.start(33);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::KlvOnly, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(model.decoded() >= 5, 5000);
        QVERIFY(!r.hasVideoPad());
        QCOMPARE(r.videoFrames(), 0u);
        QVERIFY(r.frame().isNull());
    }

    // A different stream on the same port (a firmware restart that turns KLV on) must end up fully routed.
    void layoutChangeIsPickedUp()
    {
        KlvReceiver r;
        MetadataModel model;
        connect(&r, &KlvReceiver::klvReceived, &model, &MetadataModel::ingestNow);
        const int port = nextPort();
        r.apply(loopback(port));
        r.start();
        {
            Sender videoOnly(port, Streams::VideoOnly);
            QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::NoKlvPad, 15000);
        }
        Sender both(port, Streams::VideoAndKlv);
        QTimer pusher;
        connect(&pusher, &QTimer::timeout, [&] { both.pushNext(); });
        pusher.start(33);
        QTRY_VERIFY_WITH_TIMEOUT(model.decoded() >= 5, 20000);
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Decoding, 10000);
        QVERIFY(r.videoFrames() > 0);
    }

    void missingElementIsAFailureWithAUsefulMessage()
    {
        GstRegistry *registry = gst_registry_get();
        GstPluginFeature *feature = gst_registry_lookup_feature(registry, "rtpmp2tdepay");
        QVERIFY(feature);
        gst_registry_remove_feature(registry, feature);
        const QStringList missing = KlvReceiver::missingElements();

        KlvReceiver r;
        r.start();
        const State failed = r.state();
        const QString error = r.lastError();
        const bool running = r.isRunning();

        gst_registry_add_feature(registry, feature);  // before any check can return early
        gst_object_unref(feature);

        QCOMPARE(missing, QStringList{QStringLiteral("rtpmp2tdepay")});
        QCOMPARE(failed, State::Failed);
        QVERIFY(error.contains(QStringLiteral("rtpmp2tdepay")));
        QVERIFY(!running);

        r.apply(loopback(nextPort()));
        r.start();  // retried by hand once the plugin is back
        QCOMPARE(r.state(), State::Waiting);
    }

    // Binding an address this machine does not have cannot be fixed by waiting.
    void persistentErrorRetriesThenFails()
    {
        KlvReceiver r;
        KlvReceiver::Config c = loopback(nextPort());
        c.address = QStringLiteral("203.0.113.1");  // TEST-NET-3, never local
        r.apply(c);
        r.start();
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Error, 2000);
        QVERIFY(!r.lastError().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Failed, (KlvReceiver::kMaxRestarts + 2) * KlvReceiver::kRestartDelayMs);
        QVERIFY(!r.isRunning());
        QTest::qWait(KlvReceiver::kRestartDelayMs + 300);
        QCOMPARE(r.state(), State::Failed);  // no timer left behind
    }

    void stopCancelsAPendingRestart()
    {
        KlvReceiver r;
        KlvReceiver::Config c = loopback(nextPort());
        c.address = QStringLiteral("203.0.113.1");
        r.apply(c);
        r.start();
        QTRY_COMPARE_WITH_TIMEOUT(r.state(), State::Error, 2000);
        r.stop();
        QCOMPARE(r.state(), State::Stopped);
        QTest::qWait(KlvReceiver::kRestartDelayMs + 300);
        QCOMPARE(r.state(), State::Stopped);
        QVERIFY(!r.isRunning());
    }

    void klvQueueIsBoundedAndCountsDrops()
    {
        KlvReceiver r;
        int delivered = 0;
        QByteArray newest;
        connect(&r, &KlvReceiver::klvReceived, [&](const QByteArray &b) { ++delivered; newest = b; });
        for (int i = 0; i < 1000; ++i) r.enqueueKlv(QByteArray::number(i));  // the GUI thread is not running meanwhile
        QCOMPARE(delivered, 0);  // a handoff, never a direct call
        QTRY_COMPARE_WITH_TIMEOUT(delivered, KlvReceiver::kMaxKlvQueue, 2000);
        QCOMPARE(r.klvDropped(), quint64(1000 - KlvReceiver::kMaxKlvQueue));
        QCOMPARE(newest, QByteArray("999"));  // the oldest went
    }

    void stopDiscardsQueuedKlv()
    {
        KlvReceiver r;
        int delivered = 0;
        connect(&r, &KlvReceiver::klvReceived, [&] { ++delivered; });
        r.enqueueKlv("x");
        r.stop();
        QTest::qWait(50);
        QCOMPARE(delivered, 0);
    }

    // A blocked GUI thread stands in for a slow one: the frames behind it are replaced, never queued.
    void busyGuiKeepsOnlyTheLatestFrame()
    {
        KlvReceiver r;
        const int port = nextPort();
        r.apply(loopback(port));
        r.start();
        Sender sender(port, Streams::VideoOnly);
        QTRY_VERIFY_WITH_TIMEOUT(r.videoFrames() > 5, 15000);
        int shown = 0;
        connect(&r, &KlvReceiver::frameReady, [&] { ++shown; });
        const quint64 frames = r.videoFrames(), superseded = r.framesSuperseded();
        QThread::msleep(1000);
        QVERIFY(r.videoFrames() > frames + 10);  // the streaming thread kept going
        QVERIFY(r.framesSuperseded() > superseded + 10);
        QTest::qWait(0);
        QVERIFY(shown <= 2);  // the whole second collapsed into one repaint, not 30
    }

    void repeatedStartStopLeavesNothingBehind()
    {
        KlvReceiver r;
        const int port = nextPort();
        r.apply(loopback(port));
        Sender sender(port, Streams::VideoAndKlv);
        const auto cycle = [&] {
            r.start();
            r.start();  // idempotent
            QTest::qWait(30);
            r.stop();
            r.stop();
        };
        for (int i = 0; i < 5; ++i) cycle();  // GStreamer's task pool settles first
        QTest::qWait(500);
        const int threadsBefore = threadCount(), filesBefore = fileCount();
        for (int i = 0; i < 50; ++i) cycle();
        QTRY_VERIFY_WITH_TIMEOUT(threadCount() <= threadsBefore && fileCount() <= filesBefore, 5000);
        QCOMPARE(r.state(), State::Stopped);
    }

    void reconfigureWhileRunningLeavesOnePipeline()
    {
        KlvReceiver r;
        const int a = nextPort(), b = nextPort();
        r.apply(loopback(a));
        r.start();
        Sender sender(b, Streams::VideoOnly);
        QTest::qWait(300);
        QCOMPARE(r.videoFrames(), 0u);  // listening on the wrong port
        QVERIFY(r.apply(loopback(b)).isEmpty());
        QVERIFY(r.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(r.videoFrames() > 5, 15000);
        KlvReceiver::Config bad = loopback(b);
        bad.port = -1;
        QVERIFY(!r.apply(bad).isEmpty());
        QVERIFY(r.isRunning());  // a rejected change leaves the stream alone
        const quint64 n = r.videoFrames();
        QTRY_VERIFY_WITH_TIMEOUT(r.videoFrames() > n + 5, 5000);
    }

    // --- The video page ---

    void pageOffersBothSourcesAndStartsOnlyTheSelectedOne()
    {
        VideoPage page;
        auto *source = page.findChild<QComboBox *>(QStringLiteral("videoSource"));
        auto *startStop = page.findChild<QPushButton *>(QStringLiteral("videoStartStop"));
        auto *panel = page.findChild<KlvPanel *>();
        QVERIFY(source && startStop && panel);
        QCOMPARE(source->count(), 2);
        QCOMPARE(source->currentIndex(), 0);  // RTSP: what the app did before KLV
        QVERIFY(panel->isHidden());

        const int port = nextPort();
        page.klvReceiver()->apply(loopback(port));
        Sender sender(port, Streams::VideoAndKlv);
        QTimer pusher;
        connect(&pusher, &QTimer::timeout, [&] { sender.pushNext(); });
        pusher.start(33);

        source->setCurrentIndex(1);
        QVERIFY(!panel->isHidden());
        startStop->click();
        QVERIFY(page.klvReceiver()->isRunning());
        QCOMPARE(page.player()->state(), VideoPlayer::State::Stopped);  // one pipeline for the selected source
        QVERIFY(!source->isEnabled());  // cannot change under a running stream

        QTRY_COMPARE_WITH_TIMEOUT(page.klvReceiver()->state(), State::Decoding, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(page.klvModel()->decoded() > 5, 5000);
        page.resize(800, 400);
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(page.view()->pictureRect() != page.view()->rect(), 3000);  // a 4:3 frame reached the wide view
        QCOMPARE(page.player()->frames(), 0u);  // and it was not decoded twice

        startStop->click();
        QVERIFY(!page.klvReceiver()->isRunning());
        QVERIFY(source->isEnabled());
        QCOMPARE(QSettings().value(QStringLiteral("video/source")).toInt(), 1);
    }

    void pageShowsWaitingAndClearsAFrozenPicture()
    {
        VideoPage page;
        page.findChild<QComboBox *>(QStringLiteral("videoSource"))->setCurrentIndex(1);
        const int port = nextPort();
        page.klvReceiver()->apply(loopback(port));
        page.findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        auto sender = std::make_unique<Sender>(port, Streams::VideoOnly);
        QTRY_COMPARE_WITH_TIMEOUT(page.klvReceiver()->state(), State::NoKlvPad, 15000);
        page.resize(800, 400);
        page.show();
        QTRY_VERIFY_WITH_TIMEOUT(page.view()->pictureRect() != page.view()->rect(), 3000);  // a 4:3 frame in a wide view
        sender.reset();
        QTRY_COMPARE_WITH_TIMEOUT(page.klvReceiver()->state(), State::Waiting, 4000);
        QCOMPARE(page.view()->pictureRect(), page.view()->rect());  // no image: an old frame would keep its aspect
    }

    void panelShowsValidMissingInvalidAndStale()
    {
        MetadataModel model;
        KlvPanel panel(&model);
        panel.show();
        const auto text = [&](int tag) { return panel.findChild<QLabel *>(QStringLiteral("klv_%1").arg(tag))->text(); };

        model.tick(1000);
        QVERIFY(text(13).contains(QStringLiteral("no data")));

        model.ingest(buildPacket({{13, be(0x0F6D1B55, 4)}}), 2000);
        model.tick(2100);
        const QString value = model.field(13)["value"].toString();
        QVERIFY2(text(13).contains(value), qPrintable(text(13)));
        QVERIFY(!text(13).contains(QStringLiteral("stale")));
        QVERIFY(text(14).contains(QStringLiteral("no data")));  // never sent

        model.tick(2000 + MetadataModel::kStaleMs + 500);
        QVERIFY2(text(13).contains(QStringLiteral("stale")), qPrintable(text(13)));
        QVERIFY(text(13).contains(QStringLiteral("2.5 s")));

        model.ingest(buildPacket({{13, be(0x80000000, 4)}}), 6000);  // the sentinel
        model.tick(6100);
        QVERIFY(text(13).contains(QStringLiteral("invalid")));
        QVERIFY(!text(13).contains(value));  // not presented as current

        auto *title = panel.findChild<QLabel *>(QStringLiteral("klvPanelTitle"));
        QVERIFY(title->text().contains(QStringLiteral("KLV")));
        QVERIFY(title->text().contains(QStringLiteral("not synchronised")));  // no claim about any particular frame
    }

    void settingsPanelShowsValidationErrors()
    {
        KlvReceiver r;
        KlvSettingsPanel panel(&r);
        auto *address = panel.findChild<QLineEdit *>(QStringLiteral("klvAddress"));
        auto *apply = panel.findChild<QPushButton *>(QStringLiteral("klvApply"));
        auto *message = panel.findChild<QLabel *>(QStringLiteral("klvSettingsMessage"));
        const KlvReceiver::Config before = r.config();
        QCOMPARE(address->text(), before.address);

        address->setText(QStringLiteral("not an ip"));
        apply->click();
        QVERIFY(message->text().contains(QStringLiteral("IPv4")));
        QCOMPARE(r.config(), before);

        address->setText(QStringLiteral("239.1.2.3"));
        apply->click();
        QVERIFY(!message->text().contains(QStringLiteral("IPv4")));
        QCOMPARE(r.config().address, QStringLiteral("239.1.2.3"));
        QCOMPARE(QSettings().value(QStringLiteral("klv/address")).toString(), QStringLiteral("239.1.2.3"));
    }

    void detailPageListsTagsPadsAndCounters()
    {
        KlvReceiver r;
        MetadataModel model;
        connect(&r, &KlvReceiver::klvReceived, &model, &MetadataModel::ingestNow);
        KlvPage page(&r, &model);
        page.show();
        const int port = nextPort();
        r.apply(loopback(port));
        r.start();
        Sender sender(port, Streams::VideoAndKlv);
        QTimer pusher;
        connect(&pusher, &QTimer::timeout, [&] { sender.pushNext(); });
        pusher.start(33);
        QTRY_VERIFY_WITH_TIMEOUT(model.decoded() > 5, 15000);
        sender.push(buildPacket({{65, QByteArray(1, 8)}}, true));
        QTRY_VERIFY_WITH_TIMEOUT(model.malformed() >= 1, 3000);

        auto *tags = page.findChild<QTableWidget *>(QStringLiteral("klvTags"));
        auto *packets = page.findChild<QTableWidget *>(QStringLiteral("klvPackets"));
        auto *streams = page.findChild<QTableWidget *>(QStringLiteral("klvPids"));
        auto *counters = page.findChild<QLabel *>(QStringLiteral("klvCounters"));
        QTRY_VERIFY_WITH_TIMEOUT(tags->rowCount() >= misb::documentedTags().size(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(packets->rowCount() > 0, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(streams->rowCount() >= 3, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(counters->text().contains(QStringLiteral("malformed 1")), 2000);

        QVERIFY(tags->findItems(QStringLiteral("Sensor Latitude"), Qt::MatchExactly).size() == 1);
        QVERIFY(!tags->findItems(QStringLiteral("no data"), Qt::MatchExactly).isEmpty());  // tags not sent
        QVERIFY(tags->findItems(QStringLiteral("Platform Latitude"), Qt::MatchContains).isEmpty());  // not in the parser
        QVERIFY(!streams->findItems(QStringLiteral("KLV metadata"), Qt::MatchExactly).isEmpty());
        pusher.stop();
    }

    // Not a check: KLV_SOAK_SECONDS=3600 runs the stream that long and prints memory, descriptors and threads each minute.
    // KLV_SOAK_ROLE=send, receive or headless (no widgets), with KLV_SOAK_PORT, runs one side alone, to tell which side grows.
    void soak()
    {
        const int seconds = qEnvironmentVariableIntValue("KLV_SOAK_SECONDS");
        if (!seconds) QSKIP("set KLV_SOAK_SECONDS to run");
        const QByteArray role = qgetenv("KLV_SOAK_ROLE");
        const int port = qEnvironmentVariableIntValue("KLV_SOAK_PORT") ? qEnvironmentVariableIntValue("KLV_SOAK_PORT") : nextPort();

        std::unique_ptr<VideoPage> page;
        std::unique_ptr<KlvPage> detail;
        std::unique_ptr<KlvReceiver> headless;
        std::unique_ptr<MetadataModel> headlessModel;
        KlvReceiver *rx = nullptr;
        MetadataModel *model = nullptr;
        if (role == "headless") {
            headless = std::make_unique<KlvReceiver>();
            headlessModel = std::make_unique<MetadataModel>();
            rx = headless.get();
            model = headlessModel.get();
            connect(rx, &KlvReceiver::klvReceived, model, &MetadataModel::ingestNow);
            rx->apply(loopback(port));
            rx->start();
        } else if (role != "send") {
            page = std::make_unique<VideoPage>();
            rx = page->klvReceiver();
            model = page->klvModel();
            detail = std::make_unique<KlvPage>(rx, model);
            page->resize(800, 500);
            page->show();
            detail->show();
            rx->apply(loopback(port));
            page->findChild<QComboBox *>(QStringLiteral("videoSource"))->setCurrentIndex(1);
            page->findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        }
        std::unique_ptr<Sender> sender;
        QTimer pusher;
        if (role.isEmpty() || role == "send") {
            sender = std::make_unique<Sender>(port, Streams::VideoAndKlv);
            connect(&pusher, &QTimer::timeout, [&] { sender->pushNext(); });
            pusher.start(33);
        }
        const auto rssKb = [] {
            QFile f(QStringLiteral("/proc/self/status"));
            if (!f.open(QIODevice::ReadOnly)) return QByteArray();
            for (const QByteArray &line : f.readAll().split('\n'))
                if (line.startsWith("VmRSS:")) return line.simplified();
            return QByteArray();
        };
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < seconds * 1000) {
            qInfo("soak %lld s: %s fds=%d threads=%d frames=%llu superseded=%llu klv=%llu drops=%llu malformed=%llu state=%s",
                  t.elapsed() / 1000, rssKb().constData(), fileCount(), threadCount(), rx ? rx->videoFrames() : 0,
                  rx ? rx->framesSuperseded() : 0, model ? model->decoded() : 0, rx ? rx->klvDropped() : 0,
                  model ? model->malformed() : 0, rx ? qPrintable(KlvReceiver::stateText(rx->state())) : "sender only");
            QEventLoop loop;  // not QTest::qWait, which sleeps between passes and would count as a slow GUI
            QTimer::singleShot(qMin<qint64>(60000, seconds * 1000 - t.elapsed() + 1), &loop, &QEventLoop::quit);
            loop.exec();
        }
        if (rx) QCOMPARE(rx->state(), State::Decoding);
    }

    // --- The whole window ---

    void startsWithoutPayloadOrStream()
    {
        MainWindow w;
        w.show();
        auto *video = w.findChild<VideoPage *>();
        QVERIFY(video);
        QCOMPARE(video->klvReceiver()->state(), State::Stopped);
        QCOMPARE(video->player()->state(), VideoPlayer::State::Stopped);
        QCOMPARE(w.link()->state(), LinkState::Disconnected);
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("analyzeTabs"));
        bool hasKlv = false;
        for (int i = 0; i < tabs->count(); ++i) hasKlv |= tabs->tabText(i) == QStringLiteral("KLV");
        QVERIFY(hasKlv);
        QVERIFY(w.findChild<QLineEdit *>(QStringLiteral("rtspUrl")));
        QVERIFY(w.findChild<QLineEdit *>(QStringLiteral("klvAddress")));
        QTest::qWait(1200);  // a full status refresh with nothing running
        QCOMPARE(video->klvReceiver()->state(), State::Stopped);
    }

    // Not a check: SCREENSHOT_DIR saves the KLV views against a synthetic stream.
    void screenshots()
    {
        const QByteArray dir = qgetenv("SCREENSHOT_DIR");
        if (dir.isEmpty()) QSKIP("set SCREENSHOT_DIR to run");
        applyTheme(*qApp);
        MainWindow w;
        w.resize(1600, 950);
        w.show();
        auto *video = w.findChild<VideoPage *>();
        const int port = nextPort();
        video->klvReceiver()->apply(loopback(port));
        video->findChild<QComboBox *>(QStringLiteral("videoSource"))->setCurrentIndex(1);
        video->findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        Sender sender(port, Streams::VideoAndKlv);
        QTimer pusher;
        quint64 us = 1700000000123456ull;
        connect(&pusher, &QTimer::timeout, [&] {
            us += 33333;
            sender.push(buildPacket({{2, be(us, 8)}, {3, "SYNTHETIC"}, {5, be(0x71C7, 2)}, {6, be(0x0400, 2)}, {7, be(0x0300, 2)},
                                     {13, be(0x0F6D1B55, 4)}, {14, be(0x4B4AC1A0, 4)}, {15, be(0x1F00, 2)}, {16, be(0x5e74, 2)},
                                     {17, be(0x3540, 2)}, {21, be(0x00100000, 4)}, {65, QByteArray(1, 8)}}));
        });
        pusher.start(33);
        QTRY_COMPARE_WITH_TIMEOUT(video->klvReceiver()->state(), State::Decoding, 15000);
        QTest::qWait(1500);
        const QString d = QString::fromLocal8Bit(dir);
        w.grab().save(d + QStringLiteral("/operate_klv.png"));
        w.findChild<QPushButton *>(QStringLiteral("settingsButton"))->click();
        QTest::qWait(200);
        w.grab().save(d + QStringLiteral("/operate_klv_settings.png"));
        w.findChild<QPushButton *>(QStringLiteral("settingsButton"))->click();
        w.findChild<QPushButton *>(QStringLiteral("navAnalyze"))->click();
        auto *tabs = w.findChild<QTabWidget *>(QStringLiteral("analyzeTabs"));
        tabs->setCurrentIndex(tabs->count() - 1);
        QTest::qWait(1500);
        w.grab().save(d + QStringLiteral("/analyze_klv.png"));
        pusher.stop();
    }

    // KLV garbage and a failing pipeline while a payload is connected: MAVLink keeps going, and nothing is commanded.
    void streamFailuresDoNotReachTheLink()
    {
        MainWindow w;
        auto *video = w.findChild<VideoPage *>();
        FakePayload payload;
        w.findChild<QLineEdit *>(QStringLiteral("ipEdit"))->setText(QStringLiteral("127.0.0.1"));
        w.findChild<QSpinBox *>(QStringLiteral("portSpin"))->setValue(payload.port);
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();
        QVERIFY(payload.learnAppEndpoint());
        QTimer beat;
        connect(&beat, &QTimer::timeout, [&] { payload.sendHeartbeat(MAV_COMP_ID_GIMBAL, MAV_TYPE_GIMBAL); });
        beat.start(100);
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Heartbeat, 5000);

        // Malformed and oversized input through the real stream.
        const int port = nextPort();
        video->findChild<QComboBox *>(QStringLiteral("videoSource"))->setCurrentIndex(1);
        video->klvReceiver()->apply(loopback(port));
        video->findChild<QPushButton *>(QStringLiteral("videoStartStop"))->click();
        auto sender = std::make_unique<Sender>(port, Streams::VideoAndKlv);
        QTimer garbage;
        connect(&garbage, &QTimer::timeout, [&] {
            sender->push(QByteArray("not klv"));
            sender->push(buildPacket({{13, be(0x0F6D1B55, 4)}}, true));
        });
        garbage.start(20);
        QTRY_VERIFY_WITH_TIMEOUT(video->klvModel()->malformed() > 20, 15000);
        QCOMPARE(video->klvModel()->decoded(), 0u);

        // A failing pipeline: an address this machine cannot bind.
        garbage.stop();
        sender.reset();
        KlvReceiver::Config bad = loopback(port);
        bad.address = QStringLiteral("203.0.113.1");
        video->klvReceiver()->apply(bad);
        QTRY_COMPARE_WITH_TIMEOUT(video->klvReceiver()->state(), State::Error, 2000);

        QCOMPARE(w.link()->state(), LinkState::Heartbeat);
        payload.received();  // what was sent up to now
        QTest::qWait(800);
        QCOMPARE(w.link()->state(), LinkState::Heartbeat);

        // No gimbal motion came out of any of this.
        for (const MavFrame &f : payload.received())
            QVERIFY(f.msgid != MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE);
        video->klvReceiver()->stop();
        w.findChild<QPushButton *>(QStringLiteral("connectButton"))->click();
        QTRY_COMPARE_WITH_TIMEOUT(w.link()->state(), LinkState::Disconnected, 3000);
    }
};

QTEST_MAIN(TestKlvStream)
#include "test_klv_stream.moc"
