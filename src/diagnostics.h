#pragma once
// Health checks for a payload under test: pure functions over what the link already observed, plus parsers for
// the payload's web app and GitHub's compare API. The page (diagnostics_page.*) gathers inputs and shows results.

#include "captured_frame.h"
#include "link_health.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

class CommandTracker;
class FlowStats;
class MavlinkDict;
class TelemetryStore;
struct PayloadProfile;

struct DiagCheck {
    enum Level { Pass, Info, Warn, Fail };
    Level level;
    QString area, name, detail;
};
QString diagLevelText(DiagCheck::Level l);  // "PASS", "INFO", "WARN", "FAIL"

// Payload core, camera, gimbal. A VIO uses 26, 100 and 154 (docs/HARDWARE_RESULTS.md); the SDK follows a camera or gimbal
// on any of these IDs, so another model may too.
extern const QList<QList<quint8>> kPayloadComponents;

struct DiagInput {
    LinkState state = LinkState::Disconnected;
    qint64 nowMs = 0;  // monotonic, as PayloadLink::monoNs() / 1e6
    int timeoutMs = 3000;
    QMap<quint16, qint64> heartbeats;  // PayloadLink::heartbeatSources()
    const TelemetryStore *telemetry = nullptr;
    TrafficStats traffic;
    quint64 captureDrops = 0;
    const FlowStats *flows = nullptr;          // optional
    const CommandTracker *tracker = nullptr;   // optional
    const MavlinkDict *dict = nullptr;         // optional: names for flags and states
    const PayloadProfile *profile = nullptr;   // optional
};
QList<DiagCheck> linkChecks(const DiagInput &in);

// Payload web app (port 8000): /update-ota lists the installed software; /system-info is JSON.
struct PayloadVersions {
    QString package;                           // "v307.59.17_build.20261102"
    QList<QPair<QString, QString>> apps;       // ("Gimbal Firmware", "v7.9.0 - OFFICIAL"), in page order
};
PayloadVersions parseOtaPage(const QByteArray &html);
QList<QPair<QString, QString>> parseSystemInfo(const QByteArray &json);  // key, value in key order; empty if not JSON

// GitHub compare API (repos/Gremsy/PayloadSdk/compare/<built>...<branch>)
struct SdkUpdate {
    QString error;                  // non-empty: the answer could not be used
    int aheadBy = 0, behindBy = 0;  // upstream commits not in this build / build commits not upstream
    QStringList commits;            // "abc1234 first line of message", oldest first
    QString url;                    // compare page for a person
};
SdkUpdate parseSdkCompare(const QByteArray &json);

QString diagReport(const QList<DiagCheck> &checks, const QStringList &header);  // plain text for a support case
