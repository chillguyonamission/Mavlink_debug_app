#pragma once
// Test helper: builds UAS LS packets with a correct (or deliberately wrong) checksum.

#include "misb0601.h"

#include <QByteArray>
#include <QList>
#include <QPair>

inline QByteArray berLength(quint64 n)
{
    if (n < 128)
        return QByteArray(1, char(n));
    QByteArray b;
    while (n) {
        b.prepend(char(n & 0xff));
        n >>= 8;
    }
    return char(0x80 | b.size()) + b;
}

inline QByteArray be(quint64 v, int n)
{
    QByteArray b(n, 0);
    for (int i = n - 1; i >= 0; --i, v >>= 8)
        b[i] = char(v & 0xff);
    return b;
}

using Items = QList<QPair<int, QByteArray>>;

inline QByteArray buildPacket(const Items &items, bool corruptChecksum = false)
{
    QByteArray value;
    for (const auto &[tag, v] : items)
        value += char(tag) + berLength(v.size()) + v;
    value += QByteArray::fromHex("0102");   // checksum tag + length; value appended below
    QByteArray pkt = misb::uasLsKey() + berLength(value.size() + 2) + value;
    quint16 sum = misb::checksum(reinterpret_cast<const uchar *>(pkt.constData()), int(pkt.size()));
    if (corruptChecksum)
        sum ^= 0x5555;
    return pkt + be(sum, 2);
}
