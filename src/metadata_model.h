#pragma once
// Latest decoded value of every KLV tag seen (plus every Gremsy-documented tag, so missing ones
// are visible), packet counters, freshness, and a bounded history of raw packets. GUI thread only.

#include "misb0601.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <deque>

class MetadataModel : public QObject
{
    Q_OBJECT
public:
    enum Status { NoData, Valid, Stale, Invalid };

    static constexpr qint64 kStaleMs = 2000;
    static constexpr int kHistory = 30;
    static constexpr int kMaxRows = 256;  // a sender inventing tags cannot grow the table without limit
    // An older tag 2 within this window is a late packet. Further back, the sender's clock was reset.
    static constexpr quint64 kDelayWindowUs = 5000000;

    explicit MetadataModel(QObject *parent = nullptr);

    int rowCount() const { return int(m_rows.size()); }
    QVariantMap fieldAt(int row) const { return rowMap(m_rows[row]); }

    // nowMs is injectable for tests; the app passes wall-clock time.
    void ingest(const QByteArray &buffer, qint64 nowMs);
    void tick(qint64 nowMs);  // recompute freshness and publish pending changes
    QVariantMap field(int tag) const;
    Status status(int tag) const;
    void reset();

    QString freshness() const;
    qint64 ageMs() const { return m_lastDecodedMs ? m_now - m_lastDecodedMs : -1; }
    quint64 buffers() const { return m_buffers; }
    quint64 decoded() const { return m_decoded; }
    quint64 malformed() const { return m_malformed; }
    quint64 fieldErrors() const { return m_fieldErrors; }
    quint64 duplicates() const { return m_duplicates; }
    quint64 delayed() const { return m_delayed; }
    quint64 droppedTags() const { return m_droppedTags; }
    QString lastError() const { return m_lastError; }
    QStringList missingExpected() const;
    bool isStale() const { return m_lastDecodedMs && m_now - m_lastDecodedMs > kStaleMs; }

    QByteArray toJson() const;
    QByteArray toCsv() const;
    QVariantList recentPackets(bool errorsOnly) const;

public slots:
    void ingestNow(const QByteArray &buffer);

signals:
    void changed();  // at most 10 Hz

private:
    struct Row {
        int tag;
        QString name, unit, rawHex, text, error;
        qint64 updatedMs = 0;
        bool everValid = false, lastValid = false, documented = false;
    };
    int rowFor(int tag, const QString &name, const QString &unit);  // -1: table full
    Status rowStatus(const Row &r) const;
    QVariantMap rowMap(const Row &r) const;

    QList<Row> m_rows;
    std::deque<QVariantMap> m_recent, m_recentBad;
    QTimer m_timer;
    qint64 m_now = 0, m_lastDecodedMs = 0;
    quint64 m_buffers = 0, m_decoded = 0, m_malformed = 0, m_fieldErrors = 0;
    quint64 m_duplicates = 0, m_delayed = 0, m_droppedTags = 0;
    quint64 m_newestTimestampUs = 0;
    QByteArray m_lastPacket;
    QString m_lastError;
    bool m_dirty = false, m_wasStale = false;
};
