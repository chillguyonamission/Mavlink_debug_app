#include "misb0601.h"

#include <QDateTime>
#include <cmath>
#include <limits>

namespace misb {

const QByteArray &uasLsKey()
{
    static const QByteArray key = QByteArray::fromHex("060e2b34020b01010e01030101000000");
    return key;
}

static const TagInfo kTags[] = {
    {1, "Checksum", "", Kind::Checksum, 0, 0, 0},
    {2, "Precision Time Stamp", "UTC", Kind::U64, 0, 0, 0},
    {3, "Mission ID", "", Kind::Str, 0, 0, 0},
    {4, "Platform Tail Number", "", Kind::Str, 0, 0, 0},
    {5, "Platform Heading Angle", "deg", Kind::U16, 0, 360, 2},
    {6, "Platform Pitch Angle", "deg", Kind::I16, -20, 20, 2},
    {7, "Platform Roll Angle", "deg", Kind::I16, -50, 50, 2},
    {10, "Platform Designation", "", Kind::Str, 0, 0, 0},
    {11, "Image Source Sensor", "", Kind::Str, 0, 0, 0},
    {12, "Image Coordinate System", "", Kind::Str, 0, 0, 0},
    {13, "Sensor Latitude", "deg", Kind::I32, -90, 90, 7},
    {14, "Sensor Longitude", "deg", Kind::I32, -180, 180, 7},
    {15, "Sensor True Altitude", "m", Kind::U16, -900, 19000, 1},
    {16, "Sensor Horizontal FOV", "deg", Kind::U16, 0, 180, 3},
    {17, "Sensor Vertical FOV", "deg", Kind::U16, 0, 180, 3},
    {18, "Sensor Relative Azimuth", "deg", Kind::U32, 0, 360, 4},
    // Gremsy lists uint32 but a -180..180 signed formula; ST 0601 defines int32. Live ViO data
    // (0xffee6b1d) only makes sense as int32 (-0.097 deg), so int32 is used.
    {19, "Sensor Relative Elevation", "deg", Kind::I32, -180, 180, 4},
    {20, "Sensor Relative Roll", "deg", Kind::U32, 0, 360, 4},
    {21, "Slant Range", "m", Kind::U32, 0, 5000000, 1},
    {22, "Target Width", "m", Kind::U16, 0, 10000, 1},
    {23, "Frame Center Latitude", "deg", Kind::I32, -90, 90, 7},
    {24, "Frame Center Longitude", "deg", Kind::I32, -180, 180, 7},
    {25, "Frame Center Elevation", "m", Kind::U16, -900, 19000, 1},
    // 57/58 appear in Gremsy's priority table without definitions; these are the ST 0601 definitions.
    {57, "Ground Range (ST 0601)", "m", Kind::U32, 0, 5000000, 1},
    {58, "Platform Fuel Remaining (ST 0601)", "kg", Kind::U16, 0, 10000, 1},
    {65, "UAS Datalink LS Version", "", Kind::U8, 0, 0, 0},
};

const TagInfo *tagInfo(int tag)
{
    for (const auto &t : kTags)
        if (t.tag == tag)
            return &t;
    return nullptr;
}

const QList<int> &documentedTags()
{
    static const QList<int> t = {2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17,
                                 18, 19, 20, 21, 22, 23, 24, 25, 65};
    return t;
}

const QList<int> &expectedTags()
{
    static const QList<int> t = {2, 5, 6, 7, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 65};
    return t;
}

bool readBerLength(const uchar *&p, const uchar *end, quint64 &len)
{
    if (p >= end)
        return false;
    const uchar b = *p++;
    if (b < 0x80) {
        len = b;
        return true;
    }
    const int n = b & 0x7f;
    if (n == 0 || n > 8 || end - p < n)   // 0x80 = indefinite length, not allowed in KLV
        return false;
    len = 0;
    for (int i = 0; i < n; ++i)
        len = (len << 8) | *p++;
    return true;
}

bool readBerOid(const uchar *&p, const uchar *end, quint32 &tag)
{
    tag = 0;
    for (int i = 0; i < 4; ++i) {
        if (p >= end)
            return false;
        const uchar b = *p++;
        tag = (tag << 7) | (b & 0x7f);
        if (!(b & 0x80))
            return true;
    }
    return false;
}

// ST 0601: running 16-bit sum of big-endian words, from the key through the checksum length byte.
quint16 checksum(const uchar *data, int n)
{
    quint16 sum = 0;
    for (int i = 0; i < n; ++i)
        sum += quint16(data[i]) << (8 * ((i + 1) % 2));
    return sum;
}

static int sizeOf(Kind k)
{
    switch (k) {
    case Kind::U8: return 1;
    case Kind::U16: case Kind::I16: case Kind::Checksum: return 2;
    case Kind::U32: case Kind::I32: return 4;
    case Kind::U64: return 8;
    case Kind::Str: return -1;
    }
    return -1;
}

Field decodeField(int tag, const QByteArray &raw)
{
    Field f;
    f.tag = tag;
    f.raw = raw;
    const TagInfo *info = tagInfo(tag);
    if (!info) {
        f.name = QStringLiteral("Unknown tag %1").arg(tag);
        f.text = raw.toHex(' ');
        f.error = QStringLiteral("no definition for this tag; raw bytes kept");
        return f;
    }
    f.name = info->name;
    f.unit = info->unit;

    if (info->kind == Kind::Str) {
        f.value = QString::fromUtf8(raw);
        f.text = f.value.toString();
        f.valid = true;
        return f;
    }
    const int n = sizeOf(info->kind);
    if (raw.size() != n) {
        f.error = QStringLiteral("expected %1 bytes, got %2").arg(n).arg(raw.size());
        return f;
    }
    quint64 u = 0;
    for (char c : raw)
        u = (u << 8) | uchar(c);

    switch (info->kind) {
    case Kind::Checksum:
        f.value = qulonglong(u);
        f.text = QStringLiteral("0x%1").arg(u, 4, 16, QLatin1Char('0'));
        f.valid = true;   // verified against the packet in decode()
        return f;
    case Kind::U8:
        f.value = qulonglong(u);
        f.text = QString::number(u);
        f.valid = true;
        return f;
    case Kind::U64:   // only tag 2: microseconds since 1970-01-01 UTC
        f.value = qulonglong(u);
        f.text = QDateTime::fromMSecsSinceEpoch(qint64(u / 1000), Qt::UTC)
                     .toString(Qt::ISODateWithMs);
        f.valid = true;
        return f;
    case Kind::U16:
    case Kind::U32: {
        const double full = n == 2 ? 65535.0 : 4294967295.0;
        const double v = info->min + double(u) * (info->max - info->min) / full;
        f.value = v;
        f.text = QString::number(v, 'f', info->decimals);
        f.valid = true;
        return f;
    }
    case Kind::I16:
    case Kind::I32: {
        const qint64 s = n == 2 ? qint64(qint16(u)) : qint64(qint32(u));
        const qint64 sentinel = n == 2 ? std::numeric_limits<qint16>::min()
                                       : std::numeric_limits<qint32>::min();
        if (s == sentinel) {
            f.error = QStringLiteral("\"out of range\" indicator (0x%1)").arg(u, n * 2, 16, QLatin1Char('0'));
            return f;
        }
        const double steps = n == 2 ? 65534.0 : 4294967294.0;
        const double v = double(s) * (info->max - info->min) / steps;
        f.value = v;
        f.text = QString::number(v, 'f', info->decimals);
        f.valid = true;
        return f;
    }
    case Kind::Str:
        break;
    }
    return f;
}

static void decodeLocalSet(Packet &pkt, const uchar *keyStart, const uchar *p, const uchar *end)
{
    bool sawChecksum = false;
    while (p < end) {
        const uchar *itemStart = p;
        quint32 tag;
        quint64 len;
        if (!readBerOid(p, end, tag)) {
            pkt.errors << QStringLiteral("bad tag encoding at offset %1").arg(itemStart - keyStart);
            return;
        }
        if (!readBerLength(p, end, len) || len > quint64(end - p)) {
            pkt.errors << QStringLiteral("tag %1: length overruns packet at offset %2").arg(tag).arg(itemStart - keyStart);
            return;
        }
        const QByteArray raw(reinterpret_cast<const char *>(p), int(len));
        Field f = decodeField(int(tag), raw);
        if (tag == 1) {
            sawChecksum = true;
            if (p + len != end)
                pkt.errors << QStringLiteral("checksum (tag 1) is not the last item");
            else if (f.valid) {
                const quint16 calc = checksum(keyStart, int(p - keyStart));
                if (calc != f.value.toULongLong()) {
                    f.valid = false;
                    f.error = QStringLiteral("computed 0x%1").arg(calc, 4, 16, QLatin1Char('0'));
                    pkt.errors << QStringLiteral("checksum mismatch: packet %1, computed 0x%2")
                                      .arg(f.text).arg(calc, 4, 16, QLatin1Char('0'));
                }
            } else {
                pkt.errors << QStringLiteral("checksum: %1").arg(f.error);
            }
        }
        pkt.fields << f;
        p += len;
    }
    if (!sawChecksum)
        pkt.errors << QStringLiteral("missing checksum (tag 1)");
}

QList<Packet> decode(const QByteArray &buffer)
{
    QList<Packet> out;
    const uchar *base = reinterpret_cast<const uchar *>(buffer.constData());
    const uchar *end = base + buffer.size();
    const QByteArray prefix = uasLsKey().left(4);   // SMPTE UL designator 06 0E 2B 34
    qsizetype pos = 0;

    while (pos < buffer.size()) {
        const qsizetype k = buffer.indexOf(prefix, pos);
        if (k < 0) {
            Packet bad;
            bad.bytes = buffer.mid(pos);
            bad.errors << QStringLiteral("%1 bytes without a SMPTE UL key (06 0E 2B 34)").arg(bad.bytes.size());
            out << bad;
            break;
        }
        Packet pkt;
        if (k > pos)
            pkt.errors << QStringLiteral("skipped %1 bytes before key").arg(k - pos);
        const uchar *keyStart = base + k;
        const uchar *p = keyStart + 16;
        quint64 len = 0;
        if (end - keyStart < 16) {
            pkt.bytes = buffer.mid(k);
            pkt.errors << QStringLiteral("truncated key (%1 of 16 bytes)").arg(pkt.bytes.size());
            out << pkt;
            break;
        }
        if (!readBerLength(p, end, len)) {
            pkt.bytes = buffer.mid(k);
            pkt.errors << QStringLiteral("invalid BER length after key");
            out << pkt;
            break;
        }
        if (len > quint64(end - p)) {
            pkt.bytes = buffer.mid(k);
            pkt.errors << QStringLiteral("truncated: length says %1 bytes, only %2 present").arg(len).arg(end - p);
            out << pkt;
            break;
        }
        const uchar *valueEnd = p + len;
        pkt.bytes = buffer.mid(k, valueEnd - keyStart);
        if (QByteArray::fromRawData(reinterpret_cast<const char *>(keyStart), 16) != uasLsKey())
            pkt.errors << QStringLiteral("not a UAS Datalink LS key: %1")
                              .arg(QString::fromLatin1(pkt.bytes.left(16).toHex(' ')));
        else
            decodeLocalSet(pkt, keyStart, p, valueEnd);
        out << pkt;
        pos = valueEnd - base;
    }
    return out;
}

} // namespace misb
