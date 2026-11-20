#include "klv_builder.h"
#include "metadata_model.h"

#include <QtTest>

static QByteArray stamped(quint64 timestampUs, quint32 latitudeRaw = 0x0F6D1B55)
{
    return buildPacket({{2, be(timestampUs, 8)}, {13, be(latitudeRaw, 4)}});
}

class TestMetadataModel : public QObject
{
    Q_OBJECT
private slots:
    void initialStateIsNoData()
    {
        MetadataModel m;
        QCOMPARE(m.freshness(), QString("no data"));
        QCOMPARE(m.status(13), MetadataModel::NoData);
        QCOMPARE(m.field(13)["value"].toString(), QString());  // never a fake zero
        QVERIFY(m.rowCount() >= misb::documentedTags().size());
        QVERIFY(m.missingExpected().contains("Sensor Latitude"));
    }

    void freshnessTransitions()
    {
        MetadataModel m;
        const qint64 t0 = 1'000'000;
        m.ingest(buildPacket({{13, be(0x0F6D1B55, 4)}}), t0);
        m.tick(t0 + 100);
        QCOMPARE(m.freshness(), QString("live"));
        QCOMPARE(m.status(13), MetadataModel::Valid);
        m.tick(t0 + MetadataModel::kStaleMs + 1);
        QCOMPARE(m.freshness(), QString("stale"));
        QCOMPARE(m.status(13), MetadataModel::Stale);
        QVERIFY(!m.field(13)["value"].toString().isEmpty());  // last known value kept
        m.ingest(buildPacket({{13, be(0x0F6D1B55, 4)}}), t0 + 5000);
        m.tick(t0 + 5001);
        QCOMPARE(m.status(13), MetadataModel::Valid);
    }

    void countersAndMalformedThenValid()
    {
        MetadataModel m;
        const qint64 t = 5'000;
        m.ingest("garbage", t);
        m.ingest(buildPacket({{21, be(1000, 4)}}, true), t + 10);
        m.tick(t + 20);
        QCOMPARE(m.buffers(), 2u);
        QCOMPARE(m.decoded(), 0u);
        QCOMPARE(m.malformed(), 2u);
        QCOMPARE(m.freshness(), QString("undecodable"));
        QCOMPARE(m.status(21), MetadataModel::NoData);  // fields from bad packets are not applied
        QVERIFY(m.lastError().contains("checksum"));
        QCOMPARE(m.recentPackets(true).size(), 2);

        m.ingest(buildPacket({{21, be(0xFFFFFFFF, 4)}}), t + 30);
        m.tick(t + 40);
        QCOMPARE(m.decoded(), 1u);
        QCOMPARE(m.status(21), MetadataModel::Valid);
        QCOMPARE(m.field(21)["value"].toString(), QString("5000000.0"));
        QCOMPARE(m.recentPackets(false).size(), 3);
    }

    void invalidFieldKeepsLastValidText()
    {
        MetadataModel m;
        m.ingest(buildPacket({{13, be(0x0F6D1B55, 4)}}), 1000);
        const QString good = m.field(13)["value"].toString();
        m.ingest(buildPacket({{13, be(0x80000000, 4)}}), 1100);
        m.tick(1200);
        QCOMPARE(m.status(13), MetadataModel::Invalid);
        QCOMPARE(m.field(13)["value"].toString(), good);
        QCOMPARE(m.fieldErrors(), 1u);
    }

    void missingTagStaysNoDataWhilePacketsFlow()
    {
        MetadataModel m;
        m.ingest(buildPacket({{16, be(0x5e74, 2)}}), 1000);  // what the ViO beta sends: no position tags
        m.tick(1100);
        QCOMPARE(m.freshness(), QString("live"));
        QCOMPARE(m.status(16), MetadataModel::Valid);
        QCOMPARE(m.status(13), MetadataModel::NoData);
        QCOMPARE(m.field(13)["value"].toString(), QString());
    }

    void unknownTagGetsRow()
    {
        MetadataModel m;
        const int before = m.rowCount();
        m.ingest(buildPacket({{99, QByteArray::fromHex("0102")}}), 1000);
        QCOMPARE(m.rowCount(), before + 2);  // tag 99 + checksum
        QCOMPARE(m.field(99)["raw"].toString(), QString("01 02"));
        QCOMPARE(m.status(99), MetadataModel::Valid);
    }

    void rowLimitDropsAndCounts()
    {
        MetadataModel m;
        QByteArray value;
        for (int tag = 128; tag < 528; ++tag) {  // one packet with far more distinct tags than the table holds, as two-byte BER-OIDs
            value += char(0x80 | (tag >> 7));
            value += char(tag & 0x7f);
            value += char(0);  // empty value
        }
        value += QByteArray::fromHex("0102");
        QByteArray pkt = misb::uasLsKey() + berLength(value.size() + 2) + value;
        pkt += be(misb::checksum(reinterpret_cast<const uchar *>(pkt.constData()), int(pkt.size())), 2);
        m.ingest(pkt, 1000);
        QCOMPARE(m.malformed(), 0u);
        QCOMPARE(m.rowCount(), MetadataModel::kMaxRows);
        QVERIFY(m.droppedTags() > 0);
    }

    void historyIsBounded()
    {
        MetadataModel m;
        for (int i = 0; i < 200; ++i)
            m.ingest(buildPacket({{65, QByteArray(1, 8)}}), 1000 + i);
        QCOMPARE(m.recentPackets(false).size(), MetadataModel::kHistory);
    }

    void duplicateIsCountedAndNotFresh()
    {
        MetadataModel m;
        const QByteArray p = stamped(1'000'000'000);
        m.ingest(p, 1000);
        for (int i = 1; i <= 5; ++i)
            m.ingest(p, 1000 + i * 1000);
        QCOMPARE(m.duplicates(), 5u);
        QCOMPARE(m.decoded(), 6u);
        QCOMPARE(m.malformed(), 0u);
        m.tick(6000);
        QCOMPARE(m.freshness(), QString("stale"));  // a repeating sender is not live data
        QCOMPARE(m.recentPackets(false).first().toMap()["note"].toString(), QString("duplicate"));
    }

    void identicalPacketsWithoutTimestampAreNotDuplicates()
    {
        MetadataModel m;
        const QByteArray p = buildPacket({{13, be(0x0F6D1B55, 4)}});
        m.ingest(p, 1000);
        m.ingest(p, 4000);  // nothing shows the sender repeated itself rather than the value holding steady
        m.tick(4100);
        QCOMPARE(m.duplicates(), 0u);
        QCOMPARE(m.freshness(), QString("live"));
    }

    void delayedPacketDoesNotReplaceNewerValues()
    {
        MetadataModel m;
        m.ingest(stamped(10'000'000, 0x10000000), 1000);
        const QString newest = m.field(13)["value"].toString();
        m.ingest(stamped(9'000'000, 0x20000000), 1100);  // 1 s older
        QCOMPARE(m.delayed(), 1u);
        QCOMPARE(m.field(13)["value"].toString(), newest);
        m.ingest(stamped(10'100'000, 0x30000000), 1200);
        QVERIFY(m.field(13)["value"].toString() != newest);
    }

    void delayedPacketAloneIsNotFresh()
    {
        MetadataModel m;
        m.ingest(stamped(10'000'000), 1000);
        m.ingest(stamped(9'000'000), 4000);
        m.tick(4100);
        QCOMPARE(m.freshness(), QString("stale"));
    }

    void senderClockResetIsAccepted()
    {
        MetadataModel m;
        m.ingest(stamped(100'000'000, 0x10000000), 1000);
        const QString before = m.field(13)["value"].toString();
        m.ingest(stamped(1'000'000, 0x20000000), 1100);  // far outside the window: a reboot, not a late packet
        QCOMPARE(m.delayed(), 0u);
        QVERIFY(m.field(13)["value"].toString() != before);
        m.ingest(stamped(1'100'000, 0x30000000), 1200);  // and later packets are compared with the new clock
        QCOMPARE(m.delayed(), 0u);
    }

    void resetForgetsEverything()
    {
        MetadataModel m;
        m.ingest(stamped(10'000'000), 1000);
        m.ingest("garbage", 1100);
        m.reset();
        QCOMPARE(m.freshness(), QString("no data"));
        QCOMPARE(m.malformed(), 0u);
        QCOMPARE(m.status(13), MetadataModel::NoData);
        QCOMPARE(m.recentPackets(false).size(), 0);
    }

    void exports()
    {
        MetadataModel m;
        m.ingest(buildPacket({{3, "M\"1"}}), 1000);
        QVERIFY(m.toCsv().contains("\"M\"\"1\""));
        QVERIFY(m.toJson().contains("\"Mission ID\""));
    }
};

QTEST_GUILESS_MAIN(TestMetadataModel)
#include "test_metadata_model.moc"
