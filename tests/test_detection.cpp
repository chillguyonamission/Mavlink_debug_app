// Onboard object detection: packet validation, states, overlay alignment, selection, and the real SDK callback path.
#include "payload_link.h"
#include "payload_profile.h"
#include "theme.h"
#include "video_page.h"

#include <QApplication>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <cstring>
#include <functional>

#include "fake_payload.h"

static det_packet_t packetOf(std::initializer_list<det_box_t> boxes, uint32_t frameId = 7)
{
    det_packet_t p{};
    p.magic = DET_PACKET_MAGIC;
    p.version = DET_PACKET_VERSION;
    p.frame_id = frameId;
    for (const det_box_t &b : boxes) p.boxes[p.num_boxes++] = b;
    return p;
}

// A real frame as a payload's MAVLink library sends it: trailing zero bytes trimmed
static mavlink_message_t frameOf(const det_packet_t &p, uint16_t type = DET_MSG_TYPE)
{
    uint8_t payload[MAVLINK_MSG_V2_EXTENSION_FIELD_PAYLOAD_LEN] = {0};
    memcpy(payload, &p, sizeof p);
    mavlink_message_t m;
    mavlink_msg_v2_extension_pack_chan(1, TelemetryStore::kPayloadCore, MAVLINK_COMM_0, &m, 0, 1, 193, type, payload);
    return m;
}

static QImage solid(int w, int h)
{
    QImage i(w, h, QImage::Format_RGB32);
    i.fill(Qt::black);
    return i;
}

// True when a pixel of the colour lies within 1 px of (x, y): painting rounds fractional coordinates
static bool painted(const QImage &img, const QColor &color, double x, double y)
{
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            if (img.pixelColor(int(x) + dx, int(y) + dy) == color) return true;
    return false;
}

// The outline is at these widget coordinates, worked out by hand, and nothing is drawn just outside its left edge
static void checkOutline(VideoView &v, const QRectF &r)
{
    const QImage img = v.grab().toImage();
    const QColor warn(Theme::Warn);
    QVERIFY2(painted(img, warn, r.left(), r.center().y()), "left edge");
    QVERIFY2(painted(img, warn, r.right(), r.center().y()), "right edge");
    QVERIFY2(painted(img, warn, r.center().x(), r.bottom()), "bottom edge");
    QCOMPARE(img.pixelColor(int(r.left()) - 4, int(r.center().y())), QColor(Qt::black));
}

class TestDetection : public QObject
{
    Q_OBJECT

    static bool waitTx(FakePayload &fake, const std::function<bool(const MavFrame &)> &pred, MavFrame *out = nullptr, int ms = 3000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            for (MavFrame &f : fake.received()) {
                if (pred(f)) {
                    if (out) *out = f;
                    return true;
                }
            }
            QTest::qWait(10);
        }
        return false;
    }

    static const TrackedCommand *find(const PayloadLink &link, const QString &action)
    {
        for (const TrackedCommand &t : link.tracker().commands())
            if (t.action == action) return &t;
        return nullptr;
    }

private slots:
    void wireFormatMatchesTheHeader()
    {
        QCOMPARE(sizeof(det_box_t), size_t(12));
        QCOMPARE(sizeof(det_packet_t), size_t(248));
        QVERIFY(sizeof(det_packet_t) <= size_t(MAVLINK_MSG_V2_EXTENSION_FIELD_PAYLOAD_LEN));
    }

    void parsesEveryValidBox()
    {
        const DetectionFrame f = parseDetectionPacket(packetOf({{100, 200, 30, 40, 5, 3, 255}, {1800, 1000, 120, 80, 0, 0, 0}}, 99));
        QCOMPARE(f.frameId, quint32(99));
        QCOMPARE(f.rejected, 0);
        QCOMPARE(f.boxes.size(), size_t(2));
        QCOMPARE(f.boxes[0].rect, QRect(100, 200, 30, 40));
        QCOMPARE(f.boxes[0].trackId, quint16(5));
        QCOMPARE(f.boxes[0].classId, quint8(3));
        QCOMPARE(f.boxes[0].confidence, quint8(255));
        QCOMPARE(detectionLabel(f.boxes[0]), QStringLiteral("#5 class 3 100%"));
        QCOMPARE(f.boxes[1].rect, QRect(1800, 1000, 120, 80));          // exactly reaches the frame's corner
        QCOMPARE(detectionLabel(f.boxes[1]), QStringLiteral("class 0 0%"));  // no track yet, no invented label
    }

    void rejectsBadGeometryAndClipsOverruns()
    {
        const DetectionFrame f = parseDetectionPacket(packetOf({
            {0, 0, 0, 10, 1, 0, 9},        // no width
            {0, 0, 10, 0, 2, 0, 9},        // no height
            {1920, 0, 10, 10, 3, 0, 9},    // starts past the right edge
            {0, 1080, 10, 10, 4, 0, 9},    // starts past the bottom edge
            {0, 0, 1921, 10, 5, 0, 9},     // wider than the frame
            {0, 0, 10, 65535, 6, 0, 9},    // taller than the frame
            {1900, 1070, 100, 100, 7, 0, 9},  // overruns the corner: clipped, not lost
        }));
        QCOMPARE(f.rejected, 6);
        QCOMPARE(f.boxes.size(), size_t(1));
        QCOMPARE(f.boxes[0].trackId, quint16(7));
        QCOMPARE(f.boxes[0].rect, QRect(1900, 1070, 20, 10));
    }

    void claimedBoxesBeyondTheLimitAreCounted()
    {
        det_packet_t p = packetOf({{1, 1, 1, 1, 1, 0, 0}});
        p.num_boxes = 200;  // the SDK clamps this, the parser must not rely on it
        const DetectionFrame f = parseDetectionPacket(p);
        QCOMPARE(f.boxes.size(), size_t(1));
        QCOMPARE(f.rejected, 199);
    }

    void classNamesAreMarkedGuesses()
    {
        QCOMPARE(detectionClassGuess(0), QStringLiteral("0 (person?)"));
        QCOMPARE(detectionClassGuess(3), QStringLiteral("3 (motorcycle?)"));
        QCOMPARE(detectionClassGuess(79), QStringLiteral("79 (toothbrush?)"));
        QCOMPARE(detectionClassGuess(80), QStringLiteral("80"));  // outside the table: only the number
        QCOMPARE(detectionClassGuess(255), QStringLiteral("255"));
        QCOMPARE(detectionLabel(DetectionBox{{}, 0, 3, 255}), QStringLiteral("class 3 100%"));  // the video overlay does not guess
    }

    void packetRateCountsTheLastTwoSeconds()
    {
        std::deque<DetectionFrame> log;
        QCOMPARE(detectionRate(log, 10000), 0.0);
        for (qint64 ms = 5000; ms <= 10000; ms += 100) {
            log.push_back({});
            log.back().monoMs = ms;
        }
        QCOMPARE(detectionRate(log, 10000), 10.0);   // 20 packets in 2 s
        QCOMPARE(detectionRate(log, 11000), 5.0);    // the stream stopped 1 s ago
        QCOMPARE(detectionRate(log, 13000), 0.0);
    }

    void everyPacketIsLoggedAndTheLogIsBounded()
    {
        PayloadLink link;
        FakePayload fake;
        link.connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QTRY_COMPARE_WITH_TIMEOUT(link.state(), LinkState::SocketOpen, 5000);
        QVERIFY(fake.learnAppEndpoint());

        // Several between two polls: all kept, in order, each with its receive time
        for (uint32_t id = 1; id <= 5; ++id) fake.send(frameOf(packetOf({{1, 1, 9, 9, 1, 0, 0}}, id)));
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionLogTotal(), quint64(5), 3000);
        for (size_t i = 0; i < 5; ++i) QCOMPARE(link.detectionLog()[i].frameId, quint32(i + 1));
        QVERIFY(link.detectionLog().back().wallMs > 0 && link.detectionLog().back().monoMs > 0);

        // A stale state clears the boxes, not the log
        QTRY_VERIFY_WITH_TIMEOUT(link.detections().boxes.empty(), int(kDetectionStaleMs) + 1000);
        QCOMPARE(link.detectionLog().size(), size_t(5));

        // Batches below the pending limit, one per poll, past the log's limit
        const size_t target = PayloadLink::kDetectionLogSize + 10;
        for (size_t id = 6; id <= target; ) {
            for (int k = 0; k < 40 && id <= target; ++k, ++id) fake.send(frameOf(packetOf({}, uint32_t(id))));
            QTest::qWait(120);
        }
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionLogTotal(), quint64(target), 3000);
        QCOMPARE(link.detectionLog().size(), PayloadLink::kDetectionLogSize);
        QCOMPARE(link.detectionLog().back().frameId, quint32(target));
        QCOMPARE(link.detectionLog().front().frameId, quint32(11));

        link.disconnectLink();
        QTRY_COMPARE_WITH_TIMEOUT(link.state(), LinkState::Disconnected, 3000);
        QCOMPARE(link.detectionLogTotal(), quint64(target));  // still readable after a disconnect
        FakePayload fake2;
        link.connectTo(QStringLiteral("127.0.0.1"), fake2.port);
        QCOMPARE(link.detectionLogTotal(), quint64(0));       // a new connection starts clean
        QVERIFY(link.detectionLog().empty());
        QTRY_COMPARE_WITH_TIMEOUT(link.state(), LinkState::SocketOpen, 5000);
    }

    void states()
    {
        using S = DetectionState;
        QCOMPARE(detectionState(-1, -1, 0, false, 1000), S::Off);
        QCOMPARE(detectionState(1000, -1, 0, false, 1000), S::Waiting);
        QCOMPARE(detectionState(1000, -1, 0, false, 1000 + kDetectionNoResponseMs), S::Waiting);
        QCOMPARE(detectionState(1000, -1, 0, false, 1001 + kDetectionNoResponseMs), S::NoResponse);
        QCOMPARE(detectionState(1000, -1, 0, true, 1001), S::Rejected);
        QCOMPARE(detectionState(1000, 2000, 0, false, 2000 + kDetectionStaleMs), S::Empty);
        QCOMPARE(detectionState(1000, 2000, 2, false, 2000 + kDetectionStaleMs), S::Boxes);
        QCOMPARE(detectionState(1000, 2000, 2, false, 2001 + kDetectionStaleMs), S::Stale);
        QCOMPARE(detectionState(1000, 2000, 2, true, 2100), S::Boxes);  // packets beat an old rejection
        QCOMPARE(detectionState(-1, 2000, 1, false, 2100), S::Boxes);   // someone else enabled it: still the truth
        QCOMPARE(detectionState(-1, 2000, 1, false, 2001 + kDetectionStaleMs), S::Off);
    }

    void mappingUsesThePictureRectangle()
    {
        const QRect box(1440, 810, 192, 108);
        QCOMPARE(detectionRectInPicture(box, QRectF(0, 0, 1920, 1080)), QRectF(box));
        QCOMPARE(detectionRectInPicture(box, QRectF(0, 0, 1280, 720)), QRectF(960, 540, 128, 72));  // the VIO's stream
        QCOMPARE(detectionRectInPicture(box, QRectF(0, 180, 640, 360)), QRectF(480, 450, 64, 36));  // letterboxed
        QCOMPARE(detectionRectInPicture(box, QRectF(100, 0, 400, 300)), QRectF(400, 225, 40, 30));  // pillarboxed, 4:3
    }

    void overlayStaysAlignedThroughResizesAndFrameSizes()
    {
        const QRect box(1440, 810, 192, 108);
        VideoView v;
        v.setDetections({{box, 7, 0, 200}});
        v.setImage(solid(1280, 720));

        v.resize(640, 720);  // letterboxed: bars above and below
        QCOMPARE(v.pictureRect(), QRect(0, 180, 640, 360));
        checkOutline(v, QRectF(480, 450, 64, 36));
        if (QTest::currentTestFailed()) return;

        v.resize(1200, 300);  // pillarboxed: bars left and right
        QCOMPARE(v.pictureRect(), QRect(333, 0, 533, 300));
        checkOutline(v, QRectF(732.75, 225, 53.3, 30));
        if (QTest::currentTestFailed()) return;

        v.resize(1280, 720);  // 1:1 with the frame
        checkOutline(v, QRectF(960, 540, 128, 72));
        if (QTest::currentTestFailed()) return;

        v.setImage(solid(640, 512));  // another stream size: the same source box follows the new picture
        v.resize(800, 800);
        QCOMPARE(v.pictureRect(), QRect(0, 80, 800, 640));
        checkOutline(v, QRectF(600, 560, 80, 64));
    }

    void overlayIsRemovedWithTheData()
    {
        const QRect box(1440, 810, 192, 108);
        VideoView v;
        v.resize(1280, 720);
        v.setImage(solid(1280, 720));
        v.setDetections({{box, 7, 0, 200}});
        const QRectF r = detectionRectInPicture(box, v.pictureRect());
        QVERIFY(painted(v.grab().toImage(), QColor(Theme::Warn), r.left(), r.center().y()));
        v.setDetections({});
        QVERIFY(!painted(v.grab().toImage(), QColor(Theme::Warn), r.left(), r.center().y()));

        v.setDetections({{box, 7, 0, 200}});  // no video: nothing to place a box on
        v.clear(QStringLiteral("Video stopped"));
        QVERIFY(!painted(v.grab().toImage(), QColor(Theme::Warn), r.left(), r.center().y()));
    }

    void clickSelectsByTrackIdAndNeverPicks()
    {
        VideoView v;
        v.resize(640, 720);  // letterboxed: picture at y 180..540
        v.show();
        v.setImage(solid(1280, 720));
        const QRect big(0, 0, 960, 540), small(240, 135, 100, 100), noTrack(1400, 800, 200, 200);
        v.setDetections({{big, 1, 0, 200}, {small, 2, 0, 200}, {noTrack, 0, 0, 200}});
        QSignalSpy selected(&v, &VideoView::detectionSelected), picked(&v, &VideoView::picked);
        const QRect pic = v.pictureRect();
        auto click = [&](const QRect &source) {
            QTest::mouseClick(&v, Qt::LeftButton, {}, detectionRectInPicture(source, pic).center().toPoint());
        };

        click(small);  // inside both: the smaller one
        QCOMPARE(v.selectedDetection(), quint16(2));
        click(big.adjusted(0, 0, -600, -300));
        QCOMPARE(v.selectedDetection(), quint16(1));
        click(noTrack);  // no ID to follow it by across packets
        QCOMPARE(v.selectedDetection(), quint16(0));
        QCOMPARE(selected.size(), 3);
        QVERIFY(picked.isEmpty());

        // While the tracker is armed a click is a tracker target, as before, and selects nothing
        v.setPickMode(true);
        click(small);
        QCOMPARE(picked.size(), 1);
        QCOMPARE(v.selectedDetection(), quint16(0));
    }

    void boxSemanticsDifferFromTrackerTargets()
    {
        // The packet's x, y is the top-left corner; PayloadLink::trackingTarget() takes the centre
        const DetectionBox b = parseDetectionPacket(packetOf({{896, 476, 128, 128, 1, 0, 0}})).boxes.at(0);
        QCOMPARE(QRectF(b.rect).center(), QPointF(960, 540));  // the SDK's default tracker target
        QCOMPARE(QRectF(QRect(0, 0, 3, 3)).center(), QPointF(1.5, 1.5));  // QRect::center() would round this down
    }

    // The real path: frames on a UDP socket -> SDK parser -> SDK callback -> PayloadLink, with the exact commands
    void sdkPath()
    {
        const PayloadProfile &vio = vioProfile();
        QCOMPARE(QString::fromLatin1(vio.trackingModeId), QStringLiteral("TRACK_MODE"));
        for (const PayloadProfile *p : payloadProfiles()) {  // the same parameter and values in all four SDK headers
            QCOMPARE(QString::fromLatin1(p->trackingModeId), QStringLiteral("TRACK_MODE"));
            QCOMPARE(p->trackingObjectMode, 0u);
            QCOMPARE(p->trackingDetectionMode, 1u);
        }

        PayloadLink link;
        QSignalSpy changed(&link, &PayloadLink::detectionsChanged);
        QVERIFY(!link.detectionEnable(vio.trackingModeId, vio.trackingDetectionMode));  // not connected
        QVERIFY(!link.detectionStop());
        QCOMPARE(link.detectionStatus(), DetectionState::Off);

        FakePayload fake;
        link.connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QTRY_COMPARE_WITH_TIMEOUT(link.state(), LinkState::SocketOpen, 5000);
        QVERIFY(fake.learnAppEndpoint());
        QCOMPARE(link.detectionStatus(), DetectionState::Off);

        // Enable: PARAM_EXT_SET TRACK_MODE = 1 first ...
        MavFrame f;
        QVERIFY(link.detectionEnable(vio.trackingModeId, vio.trackingDetectionMode));
        QCOMPARE(link.detectionStatus(), DetectionState::Waiting);
        QVERIFY(link.detectionEnabled());
        QVERIFY(waitTx(fake, [](const MavFrame &x) { return x.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET; }, &f));
        char id[17] = {0}, value[129] = {0};
        mavlink_msg_param_ext_set_get_param_id(&f.msg, id);
        mavlink_msg_param_ext_set_get_param_value(&f.msg, value);
        QCOMPARE(QString::fromLatin1(id), QStringLiteral("TRACK_MODE"));
        QCOMPARE(int(value[0]), 1);
        QCOMPARE(int(mavlink_msg_param_ext_set_get_param_type(&f.msg)), int(MAV_PARAM_EXT_TYPE_UINT32));
        char pid[16] = {0}, pval[128] = {0};
        strncpy(pid, "TRACK_MODE", sizeof pid);
        mavlink_message_t ack;
        mavlink_msg_param_ext_ack_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &ack, pid, pval, MAV_PARAM_EXT_TYPE_UINT32, PARAM_ACK_ACCEPTED);
        fake.send(ack);
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("Detection mode (TRACK_MODE = 1)"))->state == TrackedCommand::Accepted, 3000);
        // ... then the stream request: V2_EXTENSION (248), 100 ms in microseconds, to the payload core
        auto streamRequest = [](const MavFrame &x) {
            return x.msgid == MAVLINK_MSG_ID_COMMAND_LONG && mavlink_msg_command_long_get_command(&x.msg) == MAV_CMD_SET_MESSAGE_INTERVAL &&
                   int(mavlink_msg_command_long_get_param1(&x.msg)) == MAVLINK_MSG_ID_V2_EXTENSION;
        };
        QVERIFY(waitTx(fake, streamRequest, &f));
        QCOMPARE(mavlink_msg_command_long_get_param2(&f.msg), 100000.f);
        QCOMPARE(int(f.targetComponent), int(TelemetryStore::kPayloadCore));
        QTRY_VERIFY_WITH_TIMEOUT(find(link, QStringLiteral("Detection stream every 100 ms")) &&
                                 find(link, QStringLiteral("Detection stream every 100 ms"))->state == TrackedCommand::Sent, 3000);

        // Malformed: none of these may reach the state, whatever else they carry
        det_packet_t badMagic = packetOf({{1, 1, 9, 9, 1, 0, 0}}), badVersion = badMagic;
        badMagic.magic = 0x1234;
        badVersion.version = DET_PACKET_VERSION + 1;
        fake.send(frameOf(badMagic));
        fake.send(frameOf(badVersion));
        fake.send(frameOf(packetOf({{1, 1, 9, 9, 1, 0, 0}}), DET_MSG_TYPE + 1));  // another V2_EXTENSION user
        det_packet_t cut{};                                                       // trimmed inside the header
        cut.magic = DET_PACKET_MAGIC & 0x00FF;
        fake.send(frameOf(cut));
        QTest::qWait(300);
        QVERIFY(link.detections().boxes.empty());
        QCOMPARE(link.detectionStatus(), DetectionState::Waiting);

        // Trimmed by MAVLink 2: the last box has zero track, class and confidence, so its tail is not on the wire.
        // It is still a good box.
        const mavlink_message_t trimmed = frameOf(packetOf({{100, 200, 30, 40, 5, 3, 200}, {300, 400, 50, 60, 0, 0, 0}}, 41));
        QVERIFY(trimmed.len < MAVLINK_MSG_ID_V2_EXTENSION_LEN);
        fake.send(trimmed);
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Boxes, 3000);
        QCOMPARE(link.detections().frameId, quint32(41));
        QCOMPARE(link.detections().boxes.size(), size_t(2));
        QCOMPARE(link.detections().boxes[1].rect, QRect(300, 400, 50, 60));
        QVERIFY(!changed.isEmpty());

        // Claims three boxes but carries one: the missing two read as zero and are counted, not drawn
        det_packet_t claims = packetOf({{100, 200, 30, 40, 5, 3, 200}}, 42);
        claims.num_boxes = 3;
        fake.send(frameOf(claims));
        QTRY_COMPARE_WITH_TIMEOUT(link.detections().frameId, quint32(42), 3000);
        QCOMPARE(link.detections().boxes.size(), size_t(1));
        QCOMPARE(link.detections().rejected, 2);

        // A claim far above the limit: the SDK's clamp holds
        det_packet_t huge = packetOf({{100, 200, 30, 40, 5, 3, 200}}, 43);
        huge.num_boxes = 255;
        fake.send(frameOf(huge));
        QTRY_COMPARE_WITH_TIMEOUT(link.detections().frameId, quint32(43), 3000);
        QCOMPARE(link.detections().boxes.size(), size_t(1));
        QCOMPARE(link.detections().rejected, DET_MAX_BOXES - 1);

        // Twenty boxes, the most one packet holds
        det_packet_t full = packetOf({}, 44);
        for (int i = 0; i < DET_MAX_BOXES; ++i) full.boxes[i] = {uint16_t(i * 10), 0, 5, 5, uint16_t(i + 1), 1, 1};
        full.num_boxes = DET_MAX_BOXES;
        fake.send(frameOf(full));
        QTRY_COMPARE_WITH_TIMEOUT(link.detections().frameId, quint32(44), 3000);
        QCOMPARE(link.detections().boxes.size(), size_t(DET_MAX_BOXES));
        QCOMPARE(link.detections().boxes.back().rect.x(), (DET_MAX_BOXES - 1) * 10);

        // Packets with nothing detected are not silence
        fake.send(frameOf(packetOf({}, 45)));
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Empty, 3000);
        QVERIFY(link.detections().boxes.empty());

        // Silence: boxes do not outlive the packets
        fake.send(frameOf(packetOf({{100, 200, 30, 40, 5, 3, 200}}, 46)));
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Boxes, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Stale, int(kDetectionStaleMs) + 1000);
        QVERIFY(link.detections().boxes.empty());
        fake.send(frameOf(packetOf({{100, 200, 30, 40, 5, 3, 200}}, 47)));  // and it recovers
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Boxes, 3000);

        // Stop: rate 0, the state and boxes clear at once
        fake.received();
        QVERIFY(link.detectionStop());
        QCOMPARE(link.detectionStatus(), DetectionState::Off);
        QVERIFY(link.detections().boxes.empty());
        QVERIFY(!link.detectionEnabled());
        QVERIFY(waitTx(fake, streamRequest, &f));
        QCOMPARE(mavlink_msg_command_long_get_param2(&f.msg), 0.f);

        // Refused: the payload's ACK says so, unlike silence
        QVERIFY(link.detectionEnable(vio.trackingModeId, vio.trackingDetectionMode));
        QVERIFY(waitTx(fake, [](const MavFrame &x) { return x.msgid == MAVLINK_MSG_ID_PARAM_EXT_SET; }, &f));
        mavlink_msg_param_ext_ack_pack_chan(1, MAV_COMP_ID_CAMERA, MAVLINK_COMM_0, &ack, pid, pval, MAV_PARAM_EXT_TYPE_UINT32, PARAM_ACK_FAILED);
        fake.send(ack);
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Rejected, 3000);

        // A stop before the 500 ms delay ends cancels the delayed stream request
        QVERIFY(link.detectionStop());
        fake.received();
        QVERIFY(link.detectionEnable(vio.trackingModeId, vio.trackingDetectionMode));
        QVERIFY(link.detectionStop());
        QTest::qWait(800);
        for (const MavFrame &x : fake.received())
            if (streamRequest(x)) QCOMPARE(mavlink_msg_command_long_get_param2(&x.msg), 0.f);

        // Disconnect drops everything
        QVERIFY(link.detectionEnable(vio.trackingModeId, vio.trackingDetectionMode));
        fake.send(frameOf(packetOf({{100, 200, 30, 40, 5, 3, 200}}, 48)));
        QTRY_COMPARE_WITH_TIMEOUT(link.detectionStatus(), DetectionState::Boxes, 3000);
        link.disconnectLink();
        QTRY_COMPARE_WITH_TIMEOUT(link.state(), LinkState::Disconnected, 3000);
        QCOMPARE(link.detectionStatus(), DetectionState::Off);
        QVERIFY(link.detections().boxes.empty());
    }

    // A selected box's centre through the unchanged trackingTarget(): rate 0, ACTIVE, then centre and size in param4-7
    void selectedBoxEncodesAsTrackerTarget()
    {
        const DetectionBox b = parseDetectionPacket(packetOf({{100, 200, 30, 41, 5, 3, 200}})).boxes.at(0);
        const QRectF box(b.rect);
        PayloadLink link;
        FakePayload fake;
        link.connectTo(QStringLiteral("127.0.0.1"), fake.port);
        QTRY_COMPARE_WITH_TIMEOUT(link.state(), LinkState::SocketOpen, 5000);
        QVERIFY(fake.learnAppEndpoint());
        mavlink_message_t m;
        float q[4];
        mavlink_euler_to_quaternion(0, 0, 0, q);
        mavlink_msg_gimbal_device_attitude_status_pack_chan(1, MAV_COMP_ID_GIMBAL, MAVLINK_COMM_0, &m, 0, 0, 0, 0, q, 0, 0, 0, 0, NAN, NAN, 0);
        fake.send(m);
        QTRY_VERIFY_WITH_TIMEOUT(link.telemetry().latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS), 3000);
        fake.received();

        QVERIFY(link.trackingTarget(box.center().x(), box.center().y(), box.width(), box.height()));
        std::vector<MavFrame> sent;
        QElapsedTimer t;
        t.start();
        auto isUser4 = [](const MavFrame &x) { return x.msgid == MAVLINK_MSG_ID_COMMAND_LONG && mavlink_msg_command_long_get_command(&x.msg) == MAV_CMD_USER_4; };
        while (t.elapsed() < 3000 && std::count_if(sent.begin(), sent.end(), isUser4) < 2) {
            for (MavFrame &x : fake.received())
                if (isUser4(x) || x.msgid == MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE) sent.push_back(std::move(x));
            QTest::qWait(10);
        }
        QCOMPARE(sent.size(), size_t(3));
        QCOMPARE(sent[0].msgid, quint32(MAVLINK_MSG_ID_GIMBAL_DEVICE_SET_ATTITUDE));
        QCOMPARE(mavlink_msg_gimbal_device_set_attitude_get_angular_velocity_z(&sent[0].msg), 0.f);
        QCOMPARE(mavlink_msg_command_long_get_param3(&sent[1].msg), 0.f);  // mode ...
        QCOMPARE(mavlink_msg_command_long_get_param4(&sent[1].msg), 1.f);  // ... ACTIVE
        QCOMPARE(mavlink_msg_command_long_get_param1(&sent[2].msg), 4.f);
        QCOMPARE(mavlink_msg_command_long_get_param2(&sent[2].msg), 0.f);
        QCOMPARE(mavlink_msg_command_long_get_param3(&sent[2].msg), 1.f);  // position
        QCOMPARE(mavlink_msg_command_long_get_param4(&sent[2].msg), 115.f);
        QCOMPARE(mavlink_msg_command_long_get_param5(&sent[2].msg), 220.5f);
        QCOMPARE(mavlink_msg_command_long_get_param6(&sent[2].msg), 30.f);
        QCOMPARE(mavlink_msg_command_long_get_param7(&sent[2].msg), 41.f);
    }
};

QTEST_MAIN(TestDetection)
#include "test_detection.moc"
