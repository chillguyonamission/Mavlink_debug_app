#pragma once
// MISB ST 0601 (UAS Datalink Local Set) decoder.
// Tag table follows the Gremsy ViO "Supported KLV keys (MISB 0601)" page; where that page
// is ambiguous the MISB ST 0601.8 definition is used (see README "Decoder assumptions").

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace misb {

enum class Kind { U8, U16, U32, U64, I16, I32, Str, Checksum };

struct TagInfo {
    int tag;
    const char *name;
    const char *unit;
    Kind kind;
    double min, max;   // linear mapping range; min == max means "no scaling"
    int decimals;
};

const TagInfo *tagInfo(int tag);      // nullptr for tags we have no definition for
const QList<int> &documentedTags();   // tags listed by Gremsy for ViO
const QList<int> &expectedTags();     // Gremsy priority High/Medium: should arrive every frame / on change

struct Field {
    int tag = 0;
    QString name, unit;
    QByteArray raw;
    QVariant value;   // double, qulonglong or QString; invalid when !valid
    QString text;     // human readable decoded value
    bool valid = false;
    QString error;
};

struct Packet {
    QByteArray bytes;      // the bytes this packet covers (key..end, or the unparseable remainder)
    QList<Field> fields;
    QStringList errors;    // structural errors (framing, BER, checksum); fields are untrusted if non-empty
    bool ok() const { return errors.isEmpty(); }
};

const QByteArray &uasLsKey();   // 06 0E 2B 34 02 0B 01 01 0E 01 03 01 01 00 00 00

// Splits a buffer into KLV packets and decodes every UAS LS packet in it.
// Never throws; malformed input produces a Packet with errors.
QList<Packet> decode(const QByteArray &buffer);

// Exposed for tests.
bool readBerLength(const uchar *&p, const uchar *end, quint64 &len);
bool readBerOid(const uchar *&p, const uchar *end, quint32 &tag);
quint16 checksum(const uchar *data, int n);
Field decodeField(int tag, const QByteArray &raw);

} // namespace misb
