#pragma once
// RTP/MPEG-TS receiver, independent of the MAVLink link and of VideoPlayer:
//   udpsrc ! rtpmp2tdepay ! tsdemux, with dynamic tsdemux pads routed by caps to
//     video/x-h264 ! queue ! h264parse ! avdec_h264 ! videoconvert ! appsink  (latest frame only)
//     meta/x-klv   ! queue ! appsink                                           (bounded queue to the GUI thread)
//     anything else ! fakesink                                                 (listed in the diagnostics)
// Streaming threads only copy and count. klvReceived() and every other signal fire on the owner's thread.

#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <vector>

typedef struct _GstElement GstElement;
typedef struct _GstPad GstPad;

class KlvReceiver : public QObject
{
    Q_OBJECT
public:
    enum class State {
        Stopped,
        Waiting,    // listening, no UDP traffic: a silent multicast group looks exactly like this
        Receiving,  // UDP traffic, no decoded frames yet
        Decoding,   // frames, and the stream has a KLV pad
        NoKlvPad,   // frames, and the stream carries no KLV
        KlvOnly,    // a KLV pad and no video stream
        Error,      // pipeline error, restart pending
        Failed,     // cannot run: nothing retries until the next start()
    };

    struct Config {
        QString address, iface;  // iface empty: the kernel chooses
        int port = 0;
        QString media, encoding;  // RTP caps
        int clockRate = 0;
        bool operator==(const Config &o) const
        {
            return address == o.address && iface == o.iface && port == o.port && media == o.media &&
                   encoding == o.encoding && clockRate == o.clockRate;
        }
    };

    struct PadInfo {
        QString name, caps, kind;
        int pid = -1;
        bool linked = false;
    };
    struct PidRow {
        int pid;
        quint32 packets, rate;
        QString pad, label;
    };

    static constexpr int kMaxKlvQueue = 64;
    static constexpr int kMaxKlvBytes = 64 * 1024;  // KLV packets are tens of bytes; PES can claim far more
    static constexpr int kMaxLog = 100;
    static constexpr int kRestartDelayMs = 2000;
    static constexpr int kMaxRestarts = 5;  // in a row with no traffic, then Failed

    explicit KlvReceiver(QObject *parent = nullptr);  // loads the saved settings
    ~KlvReceiver() override;

    static Config defaults();  // the KLV monitor's defaults, seen on ViO beta firmware: not universal payload settings
    static QString validate(const Config &c);  // empty when usable
    static QStringList interfaces();
    static QStringList missingElements();  // of those this pipeline needs

    Config config() const { return m_config; }
    QString endpoint() const;
    QString apply(const Config &c);  // validates, saves and restarts if running. Returns the error, or empty

    void start();  // idempotent
    void stop();   // idempotent. No callback runs and no restart is pending after this returns

    bool isRunning() const { return m_pipeline != nullptr; }
    State state() const { return m_state; }
    static QString stateText(State s);
    QString lastError() const { return m_lastError; }
    QStringList log() const { return m_log; }

    QImage frame() const;
    quint64 videoFrames() const { return m_frames; }
    quint64 framesSuperseded() const { return m_superseded; }  // replaced by a newer frame before the GUI took them
    double videoFps() const { return m_fps; }
    double udpRate() const { return m_udpRate; }
    double udpKbps() const { return m_udpKbps; }
    quint64 klvDropped() const { return m_klvDropped; }
    double klvRate() const { return m_klvRate; }
    bool hasKlvPad() const;
    bool hasVideoPad() const;
    std::vector<PadInfo> pads() const;
    std::vector<PidRow> pids() const { return m_pidRows; }

    // Streaming thread side of the KLV handoff; public so tests can load it without a stream.
    void enqueueKlv(const QByteArray &bytes);

signals:
    void configChanged();
    void stateChanged();
    void statsChanged();  // 1 Hz while running
    void frameReady();    // coalesced: at most one in flight
    void klvReceived(const QByteArray &bytes);

private:
    void startPipeline();
    void stopPipeline();
    void resetCounters();
    void setState(State s);
    void updateState();
    void pipelineError(const QString &message);
    void addLog(const QString &line, bool isError = false);
    void pollBus();
    void sampleStats();
    void drainKlv();
    void onPadAdded(GstPad *pad);        // tsdemux streaming thread
    void onVideoSample(GstElement *sink);  // streaming thread
    void onKlvSample(GstElement *sink);    // streaming thread
    void countPids(const unsigned char *data, size_t size);  // streaming thread

    Config m_config;
    GstElement *m_pipeline = nullptr;
    QTimer m_busTimer, m_statsTimer, m_restartTimer;
    State m_state = State::Stopped;
    int m_restarts = 0;
    QStringList m_log;
    QString m_lastError;

    mutable std::mutex m_mutex;  // the members below that streaming threads share; held only to copy or swap
    QImage m_frame;
    std::map<QString, PadInfo> m_pads;
    std::deque<QByteArray> m_klvQueue;

    std::atomic<quint64> m_generation{0};  // queued work from a pipeline that has since been stopped is ignored
    std::atomic<bool> m_framePending{false}, m_klvPending{false};
    std::atomic<quint64> m_udpPackets{0}, m_udpBytes{0}, m_frames{0}, m_superseded{0}, m_klvBuffers{0}, m_klvDropped{0};
    std::array<std::atomic<quint32>, 8192> m_pidCount{};

    std::array<quint32, 8192> m_pidPrev{};
    std::vector<PidRow> m_pidRows;
    quint64 m_prevUdp = 0, m_prevBytes = 0, m_prevFrames = 0, m_prevKlv = 0;
    double m_udpRate = 0, m_udpKbps = 0, m_fps = 0, m_klvRate = 0;
};
