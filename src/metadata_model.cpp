#include "metadata_model.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static QString timeText(qint64 ms)
{
    return ms ? QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss.zzz")) : QString();
}

MetadataModel::MetadataModel(QObject *parent) : QObject(parent)
{
    reset();
    connect(&m_timer, &QTimer::timeout, this, [this] { tick(QDateTime::currentMSecsSinceEpoch()); });
    m_timer.start(100);  // UI refresh rate, independent of the ~30 Hz KLV rate
}

void MetadataModel::reset()
{
    m_rows.clear();
    for (int tag : misb::documentedTags()) {
        const auto *info = misb::tagInfo(tag);
        Row r{tag, info->name, info->unit, {}, {}, {}};
        r.documented = true;
        m_rows << r;
    }
    m_recent.clear();
    m_recentBad.clear();
    m_lastDecodedMs = 0;
    m_buffers = m_decoded = m_malformed = m_fieldErrors = 0;
    m_duplicates = m_delayed = m_droppedTags = 0;
    m_newestTimestampUs = 0;
    m_lastPacket.clear();
    m_lastError.clear();
    m_wasStale = false;
    m_dirty = true;
}

int MetadataModel::rowFor(int tag, const QString &name, const QString &unit)
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].tag == tag)
            return i;
    if (m_rows.size() >= kMaxRows)
        return -1;
    m_rows << Row{tag, name, unit, {}, {}, {}};
    return int(m_rows.size() - 1);
}

void MetadataModel::ingestNow(const QByteArray &buffer)
{
    ingest(buffer, QDateTime::currentMSecsSinceEpoch());
}

void MetadataModel::ingest(const QByteArray &buffer, qint64 nowMs)
{
    m_now = nowMs;
    ++m_buffers;
    for (const misb::Packet &pkt : misb::decode(buffer)) {
        QVariantMap rec{{"time", timeText(nowMs)}, {"size", pkt.bytes.size()},
                        {"ok", pkt.ok()}, {"hex", QString::fromLatin1(pkt.bytes.toHex(' '))},
                        {"errors", pkt.errors}};
        QStringList fieldErrs;
        if (pkt.ok()) {
            ++m_decoded;
            quint64 timestampUs = 0;  // only packets with a valid tag 2 can be judged repeated or late
            for (const misb::Field &f : pkt.fields)
                if (f.tag == 2 && f.valid) timestampUs = f.value.toULongLong();
            const bool duplicate = timestampUs && pkt.bytes == m_lastPacket;
            const bool late = !duplicate && timestampUs < m_newestTimestampUs && timestampUs &&
                              m_newestTimestampUs - timestampUs < kDelayWindowUs;
            if (duplicate) {
                ++m_duplicates;  // carries nothing new, so it does not count as fresh data
                rec["note"] = QStringLiteral("duplicate");
            } else if (late) {
                ++m_delayed;  // never replaces newer values
                rec["note"] = QStringLiteral("delayed");
            } else {
                if (timestampUs) m_newestTimestampUs = timestampUs;
                m_lastPacket = pkt.bytes;
                m_lastDecodedMs = nowMs;
                for (const misb::Field &f : pkt.fields) {
                    const int row = rowFor(f.tag, f.name, f.unit);
                    if (row < 0) {
                        ++m_droppedTags;
                        continue;
                    }
                    Row &r = m_rows[row];
                    r.rawHex = QString::fromLatin1(f.raw.toHex(' '));
                    r.updatedMs = nowMs;
                    r.lastValid = f.valid;
                    r.error = f.error;
                    // Keep the last valid decoded text; invalid values never overwrite it with garbage.
                    if (f.valid) {
                        r.text = f.text;
                        r.everValid = true;
                    } else if (misb::tagInfo(f.tag)) {
                        ++m_fieldErrors;
                        fieldErrs << QStringLiteral("tag %1: %2").arg(f.tag).arg(f.error);
                    } else {
                        r.text = f.text;  // unknown tag: show raw hex as the value
                    }
                }
            }
        } else {
            ++m_malformed;
            m_lastError = pkt.errors.join(QStringLiteral("; "));
        }
        rec["fieldErrors"] = fieldErrs;
        m_recent.push_front(rec);
        if (m_recent.size() > size_t(kHistory))
            m_recent.pop_back();
        if (!pkt.ok() || !fieldErrs.isEmpty()) {
            m_recentBad.push_front(rec);
            if (m_recentBad.size() > size_t(kHistory))
                m_recentBad.pop_back();
        }
    }
    m_dirty = true;
}

void MetadataModel::tick(qint64 nowMs)
{
    m_now = nowMs;
    const bool stale = isStale();
    if (!m_dirty && stale == m_wasStale && !m_lastDecodedMs)
        return;  // nothing happened, and no age to move
    m_dirty = false;
    m_wasStale = stale;
    emit changed();
}

MetadataModel::Status MetadataModel::rowStatus(const Row &r) const
{
    if (!r.updatedMs)
        return NoData;
    if (!r.lastValid && misb::tagInfo(r.tag))
        return Invalid;
    return isStale() ? Stale : Valid;
}

MetadataModel::Status MetadataModel::status(int tag) const
{
    for (const Row &r : m_rows)
        if (r.tag == tag)
            return rowStatus(r);
    return NoData;
}

QString MetadataModel::freshness() const
{
    if (!m_lastDecodedMs)
        return m_buffers ? QStringLiteral("undecodable") : QStringLiteral("no data");
    return isStale() ? QStringLiteral("stale") : QStringLiteral("live");
}

QStringList MetadataModel::missingExpected() const
{
    QStringList out;
    for (const Row &r : m_rows)
        if (misb::expectedTags().contains(r.tag) && !r.everValid)
            out << r.name;
    return out;
}

static const char *statusName(MetadataModel::Status s)
{
    switch (s) {
    case MetadataModel::NoData: return "no data";
    case MetadataModel::Valid: return "valid";
    case MetadataModel::Stale: return "stale";
    case MetadataModel::Invalid: return "invalid";
    }
    return "";
}

QVariantMap MetadataModel::rowMap(const Row &r) const
{
    return {{"tag", r.tag}, {"name", r.name}, {"raw", r.rawHex}, {"value", r.text},
            {"unit", r.unit}, {"updated", timeText(r.updatedMs)},
            {"status", statusName(rowStatus(r))}, {"error", r.error}, {"documented", r.documented}};
}

QVariantMap MetadataModel::field(int tag) const
{
    for (const Row &r : m_rows)
        if (r.tag == tag)
            return rowMap(r);
    return {{"tag", tag}, {"status", "no data"}};
}

QVariantList MetadataModel::recentPackets(bool errorsOnly) const
{
    QVariantList out;
    for (const auto &p : errorsOnly ? m_recentBad : m_recent)
        out << p;
    return out;
}

QByteArray MetadataModel::toJson() const
{
    QJsonArray fields;
    for (const Row &r : m_rows)
        fields << QJsonObject::fromVariantMap(rowMap(r));
    QJsonObject o{{"exportedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                  {"freshness", freshness()},
                  {"lastDecodedPacket", m_lastDecodedMs ? QDateTime::fromMSecsSinceEpoch(m_lastDecodedMs).toString(Qt::ISODateWithMs) : QString()},
                  {"buffers", qint64(m_buffers)}, {"decoded", qint64(m_decoded)},
                  {"malformed", qint64(m_malformed)}, {"duplicates", qint64(m_duplicates)},
                  {"delayed", qint64(m_delayed)}, {"fields", fields}};
    return QJsonDocument(o).toJson();
}

QByteArray MetadataModel::toCsv() const
{
    auto q = [](QString s) { return '"' + s.replace('"', QStringLiteral("\"\"")) + '"'; };
    QByteArray out = "tag,name,raw_hex,value,unit,last_update,status,error\n";
    for (const Row &r : m_rows)
        out += QStringList{QString::number(r.tag), q(r.name), q(r.rawHex), q(r.text), q(r.unit),
                           timeText(r.updatedMs), statusName(rowStatus(r)), q(r.error)}
                   .join(',').toUtf8() + '\n';
    return out;
}
