#include "klv_builder.h"

#include <QtTest>

using namespace misb;

static const Field *find(const Packet &p, int tag)
{
    for (const Field &f : p.fields)
        if (f.tag == tag)
            return &f;
    return nullptr;
}

class TestMisb0601 : public QObject
{
    Q_OBJECT
private slots:
    void berShortAndLong()
    {
        auto read = [](const QByteArray &b, quint64 &len) {
            const uchar *p = reinterpret_cast<const uchar *>(b.constData());
            return readBerLength(p, p + b.size(), len);
        };
        quint64 len;
        QVERIFY(read(QByteArray::fromHex("31"), len));
        QCOMPARE(len, 49u);
        QVERIFY(read(QByteArray::fromHex("8180"), len));
        QCOMPARE(len, 128u);
        QVERIFY(read(QByteArray::fromHex("820100"), len));
        QCOMPARE(len, 256u);
        QVERIFY(!read(QByteArray::fromHex("80"), len));       // indefinite form not allowed
        QVERIFY(!read(QByteArray::fromHex("8201"), len));     // long form truncated
        QVERIFY(!read(QByteArray::fromHex("89000000000000000001"), len));  // > 8 length bytes
        QVERIFY(!read(QByteArray(), len));
    }

    void berOidTags()
    {
        const QByteArray b = QByteArray::fromHex("8101");   // two-byte OID = 129
        const uchar *p = reinterpret_cast<const uchar *>(b.constData());
        quint32 tag;
        QVERIFY(readBerOid(p, p + b.size(), tag));
        QCOMPARE(tag, 129u);
    }

    // Real packets captured from the ViO beta on 2026-10-29 (tsdemux pad private_0_0042).
    void realVioPacket()
    {
        const auto pkts = decode(QByteArray::fromHex(
            "060e2b34020b01010e01030101000000310208000658c7a87c4e97410108150400000000110249ec10025e741204ffefffff1404"
            "00096d5f1304ffee6b1d010252c0"));
        QCOMPARE(pkts.size(), 1);
        const Packet &p = pkts[0];
        QVERIFY2(p.ok(), qPrintable(p.errors.join("; ")));
        QCOMPARE(p.fields.size(), 9);
        QCOMPARE(find(p, 65)->value.toInt(), 8);
        QCOMPARE(find(p, 2)->text, QString("2026-08-11T16:06:48.718Z"));
        QCOMPARE(find(p, 21)->value.toDouble(), 0.0);
        QVERIFY(qAbs(find(p, 16)->value.toDouble() - 0x5e74 * 180.0 / 65535) < 1e-9);   // ~66.4 deg HFOV
        QVERIFY(qAbs(find(p, 19)->value.toDouble() - (-0.0966)) < 1e-3);                 // int32, slightly down
        QCOMPARE(find(p, 18)->value.toDouble(), 0xffefffffu * 360.0 / 4294967295.0);
        QVERIFY(find(p, 1)->valid);
    }

    void fullSyntheticPacket()
    {
        const QByteArray pkt = buildPacket({
            {2, be(1700000000123456ull, 8)},
            {3, "MISSION-7"},
            {5, be(0x71C7, 2)},                        // 159.97 deg
            {6, be(quint16(qint16(-0x3E8)), 2)},       // signed negative pitch
            {7, be(0x08B8, 2)},                        // roll
            {11, "EO"},
            {13, be(quint32(qint32(0x0F6D1B55)), 4)},  // ~ +10.83 deg lat
            {14, be(quint32(qint32(0x4BE3C95A)), 4)},  // ~ +106.71 deg lon
            {15, be(0x0AFE, 2)},
            {23, be(quint32(qint32(-0x0F6D1B55)), 4)}, // negative frame centre lat
            {65, QByteArray(1, 8)},
        });
        const auto pkts = decode(pkt);
        QCOMPARE(pkts.size(), 1);
        const Packet &p = pkts[0];
        QVERIFY2(p.ok(), qPrintable(p.errors.join("; ")));
        QCOMPARE(find(p, 2)->text, QString("2023-11-14T22:13:20.123Z"));
        QCOMPARE(find(p, 3)->text, QString("MISSION-7"));
        QCOMPARE(find(p, 11)->text, QString("EO"));
        QCOMPARE(find(p, 5)->value.toDouble(), 0x71C7 * 360.0 / 65535);
        QCOMPARE(find(p, 6)->value.toDouble(), -1000 * 40.0 / 65534);
        QCOMPARE(find(p, 7)->value.toDouble(), 0x08B8 * 100.0 / 65534);
        QCOMPARE(find(p, 13)->value.toDouble(), 0x0F6D1B55 * 90.0 / 2147483647);
        QCOMPARE(find(p, 14)->value.toDouble(), 0x4BE3C95A * 180.0 / 2147483647);
        QCOMPARE(find(p, 15)->value.toDouble(), -900 + 0x0AFE * 19900.0 / 65535);
        QCOMPARE(find(p, 23)->value.toDouble(), -(0x0F6D1B55 * 90.0 / 2147483647));
        QVERIFY(find(p, 4) == nullptr);   // optional field absent, not zero-filled
    }

    void scalingExtremes()
    {
        QCOMPARE(decodeField(13, be(0x7FFFFFFF, 4)).value.toDouble(), 90.0);
        QCOMPARE(decodeField(13, be(0x80000001, 4)).value.toDouble(), -90.0);
        QCOMPARE(decodeField(14, be(0x7FFFFFFF, 4)).value.toDouble(), 180.0);
        QCOMPARE(decodeField(15, be(0, 2)).value.toDouble(), -900.0);
        QCOMPARE(decodeField(15, be(0xFFFF, 2)).value.toDouble(), 19000.0);
        QCOMPARE(decodeField(18, be(0xFFFFFFFF, 4)).value.toDouble(), 360.0);
        QCOMPARE(decodeField(21, be(0xFFFFFFFF, 4)).value.toDouble(), 5000000.0);
        QCOMPARE(decodeField(19, be(0x7FFFFFFF, 4)).value.toDouble(), 180.0);
    }

    void outOfRangeSentinel()
    {
        const Field f = decodeField(13, be(0x80000000, 4));
        QVERIFY(!f.valid);
        QVERIFY(f.error.contains("out of range"));
        QVERIFY(!f.value.isValid());
        QVERIFY(!decodeField(6, be(0x8000, 2)).valid);
    }

    void wrongFieldLengthIsFieldError()
    {
        const auto pkts = decode(buildPacket({{13, be(1, 3)}, {65, QByteArray(1, 8)}}));
        QVERIFY(pkts[0].ok());   // structurally fine
        QVERIFY(!find(pkts[0], 13)->valid);
        QVERIFY(find(pkts[0], 13)->error.contains("expected 4 bytes"));
        QVERIFY(find(pkts[0], 65)->valid);
    }

    void unknownTagKept()
    {
        const auto pkts = decode(buildPacket({{99, QByteArray::fromHex("deadbeef")}}));
        QVERIFY(pkts[0].ok());
        const Field *f = find(pkts[0], 99);
        QVERIFY(f);
        QCOMPARE(f->raw, QByteArray::fromHex("deadbeef"));
        QCOMPARE(f->text, QString("de ad be ef"));
    }

    void badChecksum()
    {
        const auto pkts = decode(buildPacket({{65, QByteArray(1, 8)}}, true));
        QCOMPARE(pkts.size(), 1);
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[0].errors.join("").contains("checksum mismatch"));
    }

    void missingChecksum()
    {
        const QByteArray value = QByteArray::fromHex("410108");
        const auto pkts = decode(uasLsKey() + berLength(value.size()) + value);
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[0].errors.join("").contains("missing checksum"));
    }

    void truncatedPacket()
    {
        const QByteArray good = buildPacket({{13, be(1, 4)}});
        const auto pkts = decode(good.left(good.size() - 5));
        QCOMPARE(pkts.size(), 1);
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[0].errors.join("").contains("truncated"));
        QVERIFY(!decode(uasLsKey().left(10))[0].ok());   // truncated key
    }

    void itemLengthOverrun()
    {
        QByteArray value = QByteArray::fromHex("0d7f0000");   // tag 13 claims 127 bytes
        const auto pkts = decode(uasLsKey() + berLength(value.size()) + value);
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[0].errors.join("").contains("overruns"));
    }

    void invalidBerAfterKey()
    {
        const auto pkts = decode(uasLsKey() + QByteArray::fromHex("80"));
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[0].errors.join("").contains("BER"));
    }

    void foreignKeySkipped()
    {
        QByteArray other = QByteArray::fromHex("060e2b34020b01010e01030302000000") + berLength(3) + "abc";
        const QByteArray good = buildPacket({{65, QByteArray(1, 8)}});
        const auto pkts = decode(other + good);
        QCOMPARE(pkts.size(), 2);
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[0].errors.join("").contains("not a UAS"));
        QVERIFY(pkts[1].ok());
    }

    void multiplePacketsAndGarbage()
    {
        const QByteArray a = buildPacket({{65, QByteArray(1, 8)}});
        const QByteArray b = buildPacket({{21, be(1000, 4)}});
        const auto pkts = decode(QByteArray("xyz") + a + b);
        QCOMPARE(pkts.size(), 2);
        QVERIFY(pkts[0].errors.join("").contains("skipped 3 bytes"));
        QVERIFY(pkts[1].ok());
        QVERIFY(decode(QByteArray("no key here"))[0].errors.join("").contains("without a SMPTE UL key"));
        QVERIFY(decode(QByteArray()).isEmpty());
    }

    void malformedThenValid()
    {
        const QByteArray bad = buildPacket({{65, QByteArray(1, 8)}}, true);
        const QByteArray good = buildPacket({{65, QByteArray(1, 8)}});
        const auto pkts = decode(bad + good);
        QCOMPARE(pkts.size(), 2);
        QVERIFY(!pkts[0].ok());
        QVERIFY(pkts[1].ok());
    }

    void randomInputNeverCrashes()
    {
        QRandomGenerator rng(42);
        const QByteArray good = buildPacket({{13, be(1, 4)}, {2, be(1, 8)}});
        for (int i = 0; i < 5000; ++i) {
            QByteArray b = good;
            for (int k = 0; k < 4; ++k)
                b[rng.bounded(int(b.size()))] = char(rng.bounded(256));
            b.truncate(rng.bounded(int(b.size()) + 1));
            decode(b);
        }
    }
};

QTEST_GUILESS_MAIN(TestMisb0601)
#include "test_misb0601.moc"
