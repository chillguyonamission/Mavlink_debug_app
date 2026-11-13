#include "diagnostics.h"

#include "command_tracker.h"
#include "flow_stats.h"
#include "mavlink_dict.h"
#include "payload_profile.h"
#include "telemetry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

const QList<QList<quint8>> kPayloadComponents{
    {TelemetryStore::kPayloadCore},
    {MAV_COMP_ID_CAMERA, MAV_COMP_ID_CAMERA2, MAV_COMP_ID_CAMERA3, MAV_COMP_ID_CAMERA4, MAV_COMP_ID_CAMERA5, MAV_COMP_ID_CAMERA6},
    {MAV_COMP_ID_GIMBAL, MAV_COMP_ID_GIMBAL2, MAV_COMP_ID_GIMBAL3, MAV_COMP_ID_GIMBAL4, MAV_COMP_ID_GIMBAL5, MAV_COMP_ID_GIMBAL6}};

QString diagLevelText(DiagCheck::Level l)
{
    switch (l) {
    case DiagCheck::Pass: return QStringLiteral("PASS");
    case DiagCheck::Info: return QStringLiteral("INFO");
    case DiagCheck::Warn: return QStringLiteral("WARN");
    case DiagCheck::Fail: return QStringLiteral("FAIL");
    }
    return {};
}

static QString seconds(qint64 ms) { return QStringLiteral("%1 s").arg(double(ms) / 1000.0, 0, 'f', 1); }

QList<DiagCheck> linkChecks(const DiagInput &in)
{
    QList<DiagCheck> out;
    const auto add = [&](DiagCheck::Level l, const QString &area, const QString &name, const QString &detail) {
        out.append({l, area, name, detail});
    };

    // --- Link ---
    const QString L = QStringLiteral("Link");
    switch (in.state) {
    case LinkState::Disconnected:
    case LinkState::Connecting:
    case LinkState::Disconnecting:
        add(DiagCheck::Info, L, QStringLiteral("Connection"), linkStateText(in.state) + QStringLiteral(": connect to check the payload"));
        return out;  // nothing below means anything without a link
    case LinkState::Failed: add(DiagCheck::Fail, L, QStringLiteral("Connection"), QStringLiteral("The SDK could not open its socket")); return out;
    case LinkState::SocketOpen:
        add(DiagCheck::Fail, L, QStringLiteral("Connection"),
            QStringLiteral("Nothing received. Check the payload is powered, the IP and port, and the network check below"));
        return out;
    case LinkState::BytesOnly:
        add(DiagCheck::Fail, L, QStringLiteral("Connection"),
            QStringLiteral("Datagrams arrive but none is valid MAVLink: wrong port, or another protocol on it"));
        break;
    case LinkState::Mavlink: add(DiagCheck::Warn, L, QStringLiteral("Connection"), QStringLiteral("Valid MAVLink, but no recent HEARTBEAT")); break;
    case LinkState::Heartbeat: add(DiagCheck::Pass, L, QStringLiteral("Connection"), QStringLiteral("Heartbeats arriving")); break;
    case LinkState::Stale:
        add(DiagCheck::Fail, L, QStringLiteral("Connection"),
            QStringLiteral("Traffic stopped (nothing for over %1): cable, power or a payload reboot").arg(seconds(in.timeoutMs)));
        break;
    }

    // --- Components: each part of the payload must heartbeat ---
    const QString C = QStringLiteral("Components");
    static const char *const groupNames[] = {"Payload core", "Camera", "Gimbal"};
    QSet<quint8> expected;
    for (int g = 0; g < kPayloadComponents.size(); ++g) {
        qint64 newest = -1;
        quint16 key = 0;
        for (auto it = in.heartbeats.cbegin(); it != in.heartbeats.cend(); ++it) {
            if (kPayloadComponents[g].contains(quint8(it.key() & 0xFF)) && it.value() > newest) {
                newest = it.value();
                key = it.key();
            }
        }
        for (quint8 id : kPayloadComponents[g]) expected.insert(id);
        const QString name = QLatin1String(groupNames[g]);
        if (newest < 0) {
            add(DiagCheck::Fail, C, name, QStringLiteral("No HEARTBEAT received this connection"));
            continue;
        }
        const qint64 age = in.nowMs - newest;
        const QString who = QStringLiteral("sys %1 comp %2").arg(key >> 8).arg(key & 0xFF);
        if (age > in.timeoutMs) {
            add(DiagCheck::Fail, C, name, QStringLiteral("%1: last HEARTBEAT %2 ago").arg(who, seconds(age)));
            continue;
        }
        QString detail = QStringLiteral("%1: HEARTBEAT %2 ago").arg(who, seconds(age));
        DiagCheck::Level level = DiagCheck::Pass;
        if (in.telemetry) {
            if (const CapturedFrame *hb = in.telemetry->latest(MAVLINK_MSG_ID_HEARTBEAT, quint8(key & 0xFF))) {
                const int st = int(fieldNumber(hb->frame, "system_status").value_or(-1));
                const QString stName = in.dict ? in.dict->enumLabel(QStringLiteral("MAV_STATE"), st) : QString();
                detail += QStringLiteral(", state %1%2").arg(st).arg(stName.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(stName));
                if (st == MAV_STATE_CRITICAL || st == MAV_STATE_EMERGENCY) level = DiagCheck::Fail;
                else if (st != MAV_STATE_ACTIVE && st != MAV_STATE_STANDBY) level = DiagCheck::Warn;
            }
        }
        add(level, C, name, detail);
    }
    QStringList others;
    for (auto it = in.heartbeats.cbegin(); it != in.heartbeats.cend(); ++it)
        if (!expected.contains(quint8(it.key() & 0xFF)) && in.nowMs - it.value() <= in.timeoutMs)
            others << QStringLiteral("%1/%2").arg(it.key() >> 8).arg(it.key() & 0xFF);
    if (!others.isEmpty()) add(DiagCheck::Info, C, QStringLiteral("Other senders"), QStringLiteral("Also heartbeating: ") + others.join(QStringLiteral(", ")));

    // --- Gimbal ---
    const QString G = QStringLiteral("Gimbal");
    if (in.telemetry) {
        if (const CapturedFrame *c = in.telemetry->latest(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS)) {
            GimbalAttitude a;
            if (gimbalAttitude(c->frame, &a)) {
                if (!a.failureFlags) {
                    add(DiagCheck::Pass, G, QStringLiteral("Failure flags"), QStringLiteral("None reported"));
                } else {
                    const MavlinkDict::Field *f = in.dict ? in.dict->field(MAVLINK_MSG_ID_GIMBAL_DEVICE_ATTITUDE_STATUS, QStringLiteral("failure_flags")) : nullptr;
                    const QString names = f ? in.dict->label(*f, a.failureFlags) : QString();
                    add(DiagCheck::Fail, G, QStringLiteral("Failure flags"),
                        QStringLiteral("0x%1%2").arg(a.failureFlags, 0, 16).arg(names.isEmpty() ? QString() : QStringLiteral(": ") + names));
                }
                add(DiagCheck::Info, G, QStringLiteral("Mode"), GimbalFlags::modeName(a.flags));
            }
        } else {
            add(DiagCheck::Warn, G, QStringLiteral("Attitude"), QStringLiteral("No GIMBAL_DEVICE_ATTITUDE_STATUS received"));
        }

        // --- Model ---
        if (const CapturedFrame *c = in.telemetry->latest(MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC)) {
            const QString reported = fieldText(c->frame, "model_name");
            if (in.profile && !reported.isEmpty() && !profileMatchesReport(*in.profile, reported))
                add(DiagCheck::Warn, QStringLiteral("Payload"), QStringLiteral("Model"),
                    QStringLiteral("Payload reports %1, the app is set to %2").arg(reported, QString::fromLatin1(in.profile->name)));
            else
                add(DiagCheck::Pass, QStringLiteral("Payload"), QStringLiteral("Model"), reported.isEmpty() ? QStringLiteral("(no name reported)") : reported);
            const QString fw = fieldText(c->frame, "software_version");
            if (!fw.isEmpty()) add(DiagCheck::Info, QStringLiteral("Payload"), QStringLiteral("Software (MAVLink)"), fw);
        }

        // --- DEBUG value stream: stops after a payload reboot until it is requested again ---
        const auto &params = in.telemetry->payloadParams();
        if (!params.isEmpty()) {
            const qint64 nowNs = in.nowMs * 1000000;
            int stale = 0;
            for (const auto &v : params) stale += (nowNs - v.monoNs) > 5000000000LL;
            if (stale == params.size())
                add(DiagCheck::Warn, QStringLiteral("Streams"), QStringLiteral("Payload values"),
                    QStringLiteral("None of %1 updated in 5 s: request payload info again").arg(params.size()));
            else
                add(DiagCheck::Pass, QStringLiteral("Streams"), QStringLiteral("Payload values"),
                    QStringLiteral("%1 of %2 updating").arg(params.size() - stale).arg(params.size()));
        }
    }

    // --- Data quality ---
    const QString Q = QStringLiteral("Data");
    const TrafficStats &t = in.traffic;
    const quint64 bad = t.badCrc + t.truncated + t.garbage;
    add(bad ? DiagCheck::Warn : DiagCheck::Pass, Q, QStringLiteral("Frame errors"),
        QStringLiteral("%1 bad CRC, %2 truncated, %3 garbage bytes, %4 unknown IDs").arg(t.badCrc).arg(t.truncated).arg(t.garbage).arg(t.unknownId));
    add(t.sendErrors ? DiagCheck::Fail : DiagCheck::Pass, Q, QStringLiteral("Send errors"),
        t.sendErrors ? QStringLiteral("%1 frames failed to leave this PC").arg(t.sendErrors) : QStringLiteral("None"));
    if (in.captureDrops) add(DiagCheck::Warn, Q, QStringLiteral("App capture"), QStringLiteral("%1 datagrams dropped by the app (GUI too slow)").arg(in.captureDrops));
    if (in.flows) {
        quint64 frames = 0, missing = 0, backwards = 0;
        for (const auto &[key, s] : in.flows->rxSources) {
            frames += s.frames;
            missing += s.missing;
            backwards += s.backwards;
        }
        const double pct = frames ? 100.0 * double(missing) / double(frames + missing) : 0;
        // A payload reboot restarts the counters, which also shows as jumps
        add(pct > 1.0 ? DiagCheck::Warn : DiagCheck::Pass, Q, QStringLiteral("Sequence gaps"),
            QStringLiteral("At most %1 of %2 frames lost (%3%), %4 restarts").arg(missing).arg(frames + missing).arg(pct, 0, 'f', 2).arg(backwards));
    }

    // --- Commands ---
    if (in.tracker) {
        QStringList failed;
        int total = 0;
        for (const TrackedCommand &c : in.tracker->commands()) {
            ++total;
            if (c.state == TrackedCommand::Rejected || c.state == TrackedCommand::AckTimeout || c.state == TrackedCommand::NotSent
                || c.state == TrackedCommand::SendFailed)
                failed << c.action;
        }
        if (total) {
            if (failed.isEmpty())
                add(DiagCheck::Pass, QStringLiteral("Commands"), QStringLiteral("Answers"), QStringLiteral("%1 commands, none failed").arg(total));
            else
                add(DiagCheck::Warn, QStringLiteral("Commands"), QStringLiteral("Answers"),
                    QStringLiteral("%1 of %2 failed or unanswered, latest: %3").arg(failed.size()).arg(total).arg(failed.mid(qMax(0, failed.size() - 3)).join(QStringLiteral("; "))));
        }
    }
    return out;
}

PayloadVersions parseOtaPage(const QByteArray &html)
{
    PayloadVersions v;
    const QString s = QString::fromUtf8(html);
    static const QRegularExpression pkg(QStringLiteral("Software Package\\s*-\\s*([^<\\s]+)"));
    if (const auto m = pkg.match(s); m.hasMatch()) v.package = m.captured(1);
    static const QRegularExpression app(QStringLiteral("class=\"app-name\"[^>]*>([^<]*)<.*?class=\"app-version\"[^>]*>([^<]*)<"),
                                        QRegularExpression::DotMatchesEverythingOption);
    for (auto it = app.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        v.apps.append({m.captured(1).trimmed(), m.captured(2).trimmed()});
    }
    return v;
}

QList<QPair<QString, QString>> parseSystemInfo(const QByteArray &json)
{
    QList<QPair<QString, QString>> out;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    for (auto it = o.begin(); it != o.end(); ++it) out.append({it.key(), it.value().toVariant().toString()});
    return out;
}

SdkUpdate parseSdkCompare(const QByteArray &json)
{
    SdkUpdate u;
    QJsonParseError err{};
    const QJsonObject o = QJsonDocument::fromJson(json, &err).object();
    if (err.error != QJsonParseError::NoError) {
        u.error = QStringLiteral("Not JSON: %1").arg(err.errorString());
        return u;
    }
    if (!o.contains(QStringLiteral("ahead_by"))) {
        u.error = o.value(QStringLiteral("message")).toString(QStringLiteral("Unexpected answer"));
        return u;
    }
    u.aheadBy = o.value(QStringLiteral("ahead_by")).toInt();
    u.behindBy = o.value(QStringLiteral("behind_by")).toInt();
    u.url = o.value(QStringLiteral("html_url")).toString();
    for (const QJsonValue &c : o.value(QStringLiteral("commits")).toArray()) {
        const QJsonObject co = c.toObject();
        const QString msg = co.value(QStringLiteral("commit")).toObject().value(QStringLiteral("message")).toString().section(QLatin1Char('\n'), 0, 0);
        u.commits << co.value(QStringLiteral("sha")).toString().left(7) + QLatin1Char(' ') + msg;
    }
    return u;
}

QString diagReport(const QList<DiagCheck> &checks, const QStringList &header)
{
    QString r = header.join(QLatin1Char('\n')) + QStringLiteral("\n\n");
    QString area;
    for (const DiagCheck &c : checks) {
        if (c.area != area) {
            area = c.area;
            r += QStringLiteral("\n[%1]\n").arg(area);
        }
        r += QStringLiteral("  %1  %2: %3\n").arg(diagLevelText(c.level), c.name, c.detail);
    }
    return r;
}
