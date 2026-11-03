#include "klv_receiver.h"

#include <QDateTime>
#include <QSettings>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <arpa/inet.h>
#include <net/if.h>

static const char *const kRequiredElements[] = {"udpsrc", "rtpmp2tdepay", "tsdemux", "queue", "h264parse",
                                                "avdec_h264", "videoconvert", "appsink", "fakesink"};

KlvReceiver::KlvReceiver(QObject *parent) : QObject(parent)
{
    gst_init(nullptr, nullptr);

    const Config d = defaults();
    QSettings s;
    s.beginGroup(QStringLiteral("klv"));
    Config c;
    c.address = s.value(QStringLiteral("address"), d.address).toString();
    c.port = s.value(QStringLiteral("port"), d.port).toInt();
    c.iface = s.value(QStringLiteral("iface"), d.iface).toString();
    c.media = s.value(QStringLiteral("media"), d.media).toString();
    c.encoding = s.value(QStringLiteral("encoding"), d.encoding).toString();
    c.clockRate = s.value(QStringLiteral("clockRate"), d.clockRate).toInt();
    if (const QString err = validate(c); err.isEmpty()) {
        m_config = c;
    } else {  // e.g. the saved interface was unplugged
        m_config = d;
        addLog(QStringLiteral("saved settings ignored: %1").arg(err), true);
    }

    m_busTimer.setInterval(50);
    connect(&m_busTimer, &QTimer::timeout, this, &KlvReceiver::pollBus);
    connect(&m_statsTimer, &QTimer::timeout, this, &KlvReceiver::sampleStats);
    m_statsTimer.setInterval(1000);
    m_restartTimer.setSingleShot(true);
    connect(&m_restartTimer, &QTimer::timeout, this, [this] {
        if (++m_restarts > kMaxRestarts) {
            stopPipeline();
            setState(State::Failed);
            addLog(QStringLiteral("gave up after %1 restarts with no traffic: %2").arg(kMaxRestarts).arg(m_lastError), true);
            return;
        }
        addLog(QStringLiteral("restarting pipeline"));
        stopPipeline();
        startPipeline();
    });
}

KlvReceiver::~KlvReceiver()
{
    blockSignals(true);  // the owners are being destroyed: no stateChanged() to them from here
    stop();
}

KlvReceiver::Config KlvReceiver::defaults()
{
    Config c;
    c.address = QStringLiteral("239.255.0.1");
    c.port = 8554;
    c.media = QStringLiteral("video");
    c.encoding = QStringLiteral("MP2T");
    c.clockRate = 90000;
    return c;
}

QString KlvReceiver::validate(const Config &c)
{
    in_addr ignored;
    if (inet_pton(AF_INET, c.address.toUtf8().constData(), &ignored) != 1)
        return QStringLiteral("\"%1\" is not an IPv4 address").arg(c.address);
    if (c.port < 1 || c.port > 65535)
        return QStringLiteral("Port must be 1 to 65535");
    if (!c.iface.isEmpty() && if_nametoindex(c.iface.toUtf8().constData()) == 0)
        return QStringLiteral("No network interface named \"%1\"").arg(c.iface);
    // What this pipeline can depayload: anything else would never negotiate and would only look like silence
    if (c.media != QLatin1String("video"))
        return QStringLiteral("RTP media must be \"video\"");
    if (c.encoding != QLatin1String("MP2T"))
        return QStringLiteral("RTP encoding must be \"MP2T\"");
    if (c.clockRate <= 0)
        return QStringLiteral("RTP clock rate must be above 0");
    return {};
}

QStringList KlvReceiver::interfaces()
{
    QStringList out;
    if (struct if_nameindex *list = ::if_nameindex()) {
        for (struct if_nameindex *i = list; i->if_index; ++i)
            out << QString::fromLocal8Bit(i->if_name);
        if_freenameindex(list);
    }
    return out;
}

QStringList KlvReceiver::missingElements()
{
    gst_init(nullptr, nullptr);
    QStringList out;
    for (const char *name : kRequiredElements) {
        GstElementFactory *f = gst_element_factory_find(name);
        if (f)
            gst_object_unref(f);
        else
            out << QString::fromLatin1(name);
    }
    return out;
}

QString KlvReceiver::endpoint() const
{
    return QStringLiteral("%1:%2 (%3)").arg(m_config.address).arg(m_config.port)
        .arg(m_config.iface.isEmpty() ? QStringLiteral("kernel-chosen interface") : m_config.iface);
}

QString KlvReceiver::stateText(State s)
{
    switch (s) {
    case State::Stopped: return QStringLiteral("Stopped");
    case State::Waiting: return QStringLiteral("Waiting for data");
    case State::Receiving: return QStringLiteral("Receiving, no frames yet");
    case State::Decoding: return QStringLiteral("Decoding");
    case State::NoKlvPad: return QStringLiteral("Decoding, no KLV in this stream");
    case State::KlvOnly: return QStringLiteral("KLV only, no video in this stream");
    case State::Error: return QStringLiteral("Error, restarting");
    case State::Failed: return QStringLiteral("Failed");
    }
    return {};
}

QString KlvReceiver::apply(const Config &c)
{
    if (const QString err = validate(c); !err.isEmpty()) {
        addLog(QStringLiteral("settings rejected: %1").arg(err), true);
        return err;
    }
    const bool wasRunning = isRunning();
    stop();
    m_config = c;
    QSettings s;
    s.beginGroup(QStringLiteral("klv"));
    s.setValue(QStringLiteral("address"), c.address);
    s.setValue(QStringLiteral("port"), c.port);
    s.setValue(QStringLiteral("iface"), c.iface);
    s.setValue(QStringLiteral("media"), c.media);
    s.setValue(QStringLiteral("encoding"), c.encoding);
    s.setValue(QStringLiteral("clockRate"), c.clockRate);
    emit configChanged();
    if (wasRunning) start();
    return {};
}

void KlvReceiver::start()
{
    if (m_pipeline) return;
    m_lastError.clear();
    m_restarts = 0;
    resetCounters();
    startPipeline();
}

void KlvReceiver::stop()
{
    stopPipeline();
    if (m_state != State::Stopped) setState(State::Stopped);
}

void KlvReceiver::resetCounters()
{
    m_udpPackets = 0;
    m_udpBytes = 0;
    m_frames = 0;
    m_superseded = 0;
    m_klvBuffers = 0;
    m_klvDropped = 0;
    for (auto &p : m_pidCount) p = 0;
    m_pidPrev.fill(0);
    m_pidRows.clear();
}

void KlvReceiver::startPipeline()
{
    if (m_pipeline) return;
    if (const QStringList missing = missingElements(); !missing.isEmpty()) {
        addLog(QStringLiteral("missing GStreamer elements: %1. Install gstreamer1.0-plugins-good, -plugins-bad and -libav")
                   .arg(missing.join(QStringLiteral(", "))), true);
        setState(State::Failed);
        return;
    }
    m_prevUdp = m_udpPackets;
    m_prevBytes = m_udpBytes;
    m_prevFrames = m_frames;
    m_prevKlv = m_klvBuffers;
    m_udpRate = m_udpKbps = m_fps = m_klvRate = 0;

    m_pipeline = gst_pipeline_new("klvreceiver");
    GstElement *src = gst_element_factory_make("udpsrc", "src");
    GstElement *depay = gst_element_factory_make("rtpmp2tdepay", "depay");
    GstElement *demux = gst_element_factory_make("tsdemux", "demux");

    // Values are set as properties and caps fields, never parsed as pipeline syntax.
    const QByteArray address = m_config.address.toUtf8(), iface = m_config.iface.toUtf8();
    GstCaps *caps = gst_caps_new_simple("application/x-rtp",
        "media", G_TYPE_STRING, m_config.media.toUtf8().constData(),
        "encoding-name", G_TYPE_STRING, m_config.encoding.toUtf8().constData(),
        "clock-rate", G_TYPE_INT, m_config.clockRate, nullptr);
    g_object_set(src, "address", address.constData(), "port", m_config.port, "caps", caps, nullptr);
    if (!iface.isEmpty()) g_object_set(src, "multicast-iface", iface.constData(), nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(m_pipeline), src, depay, demux, nullptr);
    gst_element_link_many(src, depay, demux, nullptr);

    g_signal_connect(demux, "pad-added", G_CALLBACK(+[](GstElement *, GstPad *pad, gpointer self) {
        static_cast<KlvReceiver *>(self)->onPadAdded(pad);
    }), this);
    // Pads go when the stream's layout changes. Rebuilding is simpler and safer than rerouting a live
    // demuxer: a replacement pad can arrive before the old one leaves, with the same name.
    g_signal_connect(demux, "pad-removed", G_CALLBACK(+[](GstElement *, GstPad *, gpointer selfPtr) {
        auto *self = static_cast<KlvReceiver *>(selfPtr);
        QMetaObject::invokeMethod(self, [self, generation = self->m_generation.load()] {
            if (generation != self->m_generation || !self->m_pipeline || self->m_restartTimer.isActive()) return;
            self->addLog(QStringLiteral("stream layout changed, rebuilding the pipeline"));
            self->m_restartTimer.start(0);
        }, Qt::QueuedConnection);
    }), this);

    GstPad *udpPad = gst_element_get_static_pad(src, "src");
    gst_pad_add_probe(udpPad, GST_PAD_PROBE_TYPE_BUFFER, [](GstPad *, GstPadProbeInfo *info, gpointer selfPtr) {
        auto *self = static_cast<KlvReceiver *>(selfPtr);
        ++self->m_udpPackets;
        self->m_udpBytes += gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info));
        return GST_PAD_PROBE_OK;
    }, this, nullptr);
    gst_object_unref(udpPad);
    GstPad *tsPad = gst_element_get_static_pad(depay, "src");
    gst_pad_add_probe(tsPad, GST_PAD_PROBE_TYPE_BUFFER, [](GstPad *, GstPadProbeInfo *info, gpointer self) {
        GstMapInfo map;
        GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
        if (gst_buffer_map(buf, &map, GST_MAP_READ)) {
            static_cast<KlvReceiver *>(self)->countPids(map.data, map.size);
            gst_buffer_unmap(buf, &map);
        }
        return GST_PAD_PROBE_OK;
    }, this, nullptr);
    gst_object_unref(tsPad);

    addLog(QStringLiteral("listening on %1").arg(endpoint()));
    setState(State::Waiting);
    m_busTimer.start();
    m_statsTimer.start();
    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        pipelineError(QStringLiteral("GStreamer could not start the pipeline on %1").arg(endpoint()));
}

void KlvReceiver::stopPipeline()
{
    m_restartTimer.stop();
    m_busTimer.stop();
    m_statsTimer.stop();
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);  // joins the streaming threads: no callback after this
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    ++m_generation;
    m_framePending = false;
    m_klvPending = false;
    m_udpRate = m_udpKbps = m_fps = m_klvRate = 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_klvQueue.clear();
    m_pads.clear();
    m_frame = QImage();
}

void KlvReceiver::setState(State s)
{
    if (s == m_state) return;
    m_state = s;
    emit stateChanged();
}

void KlvReceiver::updateState()
{
    if (!m_pipeline || m_restartTimer.isActive()) return;  // Stopped, Failed and Error are set where they happen
    const bool traffic = m_udpRate > 0 || m_udpPackets > m_prevUdp;
    const bool frames = m_fps > 0 || m_frames > m_prevFrames;
    if (frames) setState(hasKlvPad() ? State::Decoding : State::NoKlvPad);
    else if (!traffic) setState(State::Waiting);
    else if (hasKlvPad() && !hasVideoPad()) setState(State::KlvOnly);
    else setState(State::Receiving);
}

void KlvReceiver::pipelineError(const QString &message)
{
    addLog(message, true);
    setState(State::Error);
    if (!m_restartTimer.isActive()) m_restartTimer.start(kRestartDelayMs);
}

void KlvReceiver::addLog(const QString &line, bool isError)
{
    m_log.prepend(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) +
                  (isError ? QStringLiteral("ERROR ") : QString()) + line);
    if (m_log.size() > kMaxLog) m_log.removeLast();
    if (isError) m_lastError = line;
}

void KlvReceiver::pollBus()
{
    if (!m_pipeline) return;
    GstBus *bus = gst_element_get_bus(m_pipeline);
    while (GstMessage *msg = gst_bus_pop(bus)) {
        const QString source = QString::fromUtf8(GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)));
        const auto type = GST_MESSAGE_TYPE(msg);
        if (type == GST_MESSAGE_ERROR || type == GST_MESSAGE_WARNING) {
            GError *err = nullptr;
            gchar *dbg = nullptr;
            if (type == GST_MESSAGE_ERROR) gst_message_parse_error(msg, &err, &dbg);
            else gst_message_parse_warning(msg, &err, &dbg);
            const QString text = QStringLiteral("%1: %2").arg(source, QString::fromUtf8(err ? err->message : "unknown"));
            if (err) g_error_free(err);
            g_free(dbg);
            if (type == GST_MESSAGE_ERROR) pipelineError(text);
            else addLog(text);
        } else if (type == GST_MESSAGE_EOS) {
            pipelineError(QStringLiteral("end of stream"));
        }
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
}

void KlvReceiver::sampleStats()
{
    const quint64 udp = m_udpPackets, bytes = m_udpBytes, frames = m_frames, klv = m_klvBuffers;
    m_udpRate = double(udp - m_prevUdp);
    m_udpKbps = double(bytes - m_prevBytes) * 8 / 1000;
    m_fps = double(frames - m_prevFrames);
    m_klvRate = double(klv - m_prevKlv);
    m_prevUdp = udp;
    m_prevBytes = bytes;
    m_prevFrames = frames;
    m_prevKlv = klv;
    if (m_udpRate > 0) m_restarts = 0;

    const std::vector<PadInfo> padList = pads();
    m_pidRows.clear();
    for (int pid = 0; pid < 8192; ++pid) {
        const quint32 n = m_pidCount[pid];
        if (!n) continue;
        PidRow row{pid, n, n - m_pidPrev[pid], {}, {}};
        m_pidPrev[pid] = n;
        row.label = pid == 0 ? QStringLiteral("PAT") : pid == 0x1fff ? QStringLiteral("null") : QStringLiteral("PMT, PCR or no pad");
        for (const PadInfo &p : padList)
            if (p.pid == pid) {
                row.pad = p.name;
                row.label = p.kind;
            }
        m_pidRows.push_back(row);
    }
    updateState();
    emit statsChanged();
}

void KlvReceiver::countPids(const unsigned char *data, size_t size)
{
    for (size_t i = 0; i + 188 <= size; i += 188)
        if (data[i] == 0x47) ++m_pidCount[((data[i + 1] & 0x1f) << 8) | data[i + 2]];
}

void KlvReceiver::onPadAdded(GstPad *pad)
{
    PadInfo info;
    gchar *n = gst_pad_get_name(pad);
    info.name = QString::fromUtf8(n);
    g_free(n);
    info.pid = info.name.section(QLatin1Char('_'), -1).toInt(nullptr, 16);  // tsdemux names pads <type>_<program>_<pid>
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    gchar *capsText = gst_caps_to_string(caps);
    info.caps = QString::fromUtf8(capsText);
    g_free(capsText);
    const QString media = gst_caps_is_empty(caps) ? QString() : QString::fromUtf8(gst_structure_get_name(gst_caps_get_structure(caps, 0)));
    gst_caps_unref(caps);

    bool haveVideo = false, haveKlv = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto &[name, p] : m_pads) {
            haveVideo |= p.linked && p.kind == QLatin1String("H.264 video");
            haveKlv |= p.linked && p.kind == QLatin1String("KLV metadata");
        }
    }

    QList<GstElement *> chain;
    const auto make = [&chain](const char *factory) {
        GstElement *e = gst_element_factory_make(factory, nullptr);
        chain << e;
        return e;
    };
    if (media == QLatin1String("video/x-h264") && !haveVideo) {
        info.kind = QStringLiteral("H.264 video");
        // Leaky, so a slow decoder drops video instead of blocking tsdemux, and KLV with it.
        GstElement *q = make("queue");
        make("h264parse");
        make("avdec_h264");
        make("videoconvert");
        GstElement *sink = make("appsink");
        if (q) g_object_set(q, "leaky", 2, nullptr);
        if (sink) {
            GstCaps *bgrx = gst_caps_from_string("video/x-raw,format=BGRx");
            g_object_set(sink, "caps", bgrx, "max-buffers", 1, "drop", TRUE, "sync", FALSE, "async", FALSE, nullptr);
            gst_caps_unref(bgrx);
            GstAppSinkCallbacks cb{};
            cb.new_sample = [](GstAppSink *s, gpointer self) {
                static_cast<KlvReceiver *>(self)->onVideoSample(GST_ELEMENT(s));
                return GST_FLOW_OK;
            };
            gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, this, nullptr);
        }
    } else if (media == QLatin1String("meta/x-klv") && !haveKlv) {
        info.kind = QStringLiteral("KLV metadata");
        GstElement *q = make("queue");
        GstElement *sink = make("appsink");
        if (q) g_object_set(q, "leaky", 2, "max-size-buffers", kMaxKlvQueue, nullptr);
        if (sink) {
            g_object_set(sink, "max-buffers", kMaxKlvQueue, "drop", TRUE, "sync", FALSE, "async", FALSE, nullptr);
            GstAppSinkCallbacks cb{};
            cb.new_sample = [](GstAppSink *s, gpointer self) {
                static_cast<KlvReceiver *>(self)->onKlvSample(GST_ELEMENT(s));
                return GST_FLOW_OK;
            };
            gst_app_sink_set_callbacks(GST_APP_SINK(sink), &cb, this, nullptr);
        }
    } else {
        info.kind = media.startsWith(QLatin1String("video/")) ? QStringLiteral("video, not displayed")
                  : media.startsWith(QLatin1String("audio/")) ? QStringLiteral("audio") : QStringLiteral("other data");
        GstElement *sink = make("fakesink");
        if (sink) g_object_set(sink, "sync", FALSE, "async", FALSE, nullptr);
    }

    if (chain.contains(nullptr)) {
        for (GstElement *e : chain)
            if (e) gst_object_unref(e);
        QMetaObject::invokeMethod(this, [this, name = info.name] {
            addLog(QStringLiteral("cannot build a branch for %1: missing GStreamer element").arg(name), true);
        }, Qt::QueuedConnection);
    } else {
        for (GstElement *e : chain) gst_bin_add(GST_BIN(m_pipeline), e);
        for (int i = 0; i + 1 < chain.size(); ++i) gst_element_link(chain[i], chain[i + 1]);
        for (int i = int(chain.size()) - 1; i >= 0; --i) gst_element_sync_state_with_parent(chain[i]);
        GstPad *sinkPad = gst_element_get_static_pad(chain.first(), "sink");
        info.linked = gst_pad_link(pad, sinkPad) == GST_PAD_LINK_OK;
        gst_object_unref(sinkPad);
        QMetaObject::invokeMethod(this, [this, info] {
            addLog(QStringLiteral("tsdemux pad %1 [%2]: %3%4").arg(info.name, info.caps, info.kind,
                                                                   info.linked ? QString() : QStringLiteral(" (LINK FAILED)")),
                   !info.linked);
        }, Qt::QueuedConnection);
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pads[info.name] = info;
}

void KlvReceiver::onVideoSample(GstElement *sink)
{
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) return;
    GstVideoInfo vi;
    GstVideoFrame vf;
    if (gst_video_info_from_caps(&vi, gst_sample_get_caps(sample)) &&
        gst_video_frame_map(&vf, &vi, gst_sample_get_buffer(sample), GST_MAP_READ)) {
        QImage copy = QImage(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&vf, 0)), GST_VIDEO_FRAME_WIDTH(&vf),
                             GST_VIDEO_FRAME_HEIGHT(&vf), GST_VIDEO_FRAME_PLANE_STRIDE(&vf, 0), QImage::Format_RGB32).copy();
        gst_video_frame_unmap(&vf);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_frame = std::move(copy);
        }
        ++m_frames;
        if (m_framePending.exchange(true)) {
            ++m_superseded;  // the GUI had not taken the previous frame: only the newest is kept
        } else {
            QMetaObject::invokeMethod(this, [this, generation = m_generation.load()] {
                if (generation != m_generation) return;
                m_framePending = false;
                updateState();
                emit frameReady();
            }, Qt::QueuedConnection);
        }
    }
    gst_sample_unref(sample);
}

void KlvReceiver::onKlvSample(GstElement *sink)
{
    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) return;
    GstBuffer *buf = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        ++m_klvBuffers;
        if (map.size > size_t(kMaxKlvBytes)) ++m_klvDropped;  // not even copied
        else enqueueKlv(QByteArray(reinterpret_cast<const char *>(map.data), qsizetype(map.size)));
        gst_buffer_unmap(buf, &map);
    }
    gst_sample_unref(sample);
}

void KlvReceiver::enqueueKlv(const QByteArray &bytes)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_klvQueue.size() >= size_t(kMaxKlvQueue)) {
            m_klvQueue.pop_front();  // the newest metadata is what matters
            ++m_klvDropped;
        }
        m_klvQueue.push_back(bytes);
    }
    if (!m_klvPending.exchange(true))
        QMetaObject::invokeMethod(this, &KlvReceiver::drainKlv, Qt::QueuedConnection);
}

void KlvReceiver::drainKlv()
{
    m_klvPending = false;
    std::deque<QByteArray> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        batch.swap(m_klvQueue);
    }
    for (const QByteArray &b : batch) emit klvReceived(b);
}

QImage KlvReceiver::frame() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_frame;
}

bool KlvReceiver::hasKlvPad() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &[name, p] : m_pads)
        if (p.linked && p.kind == QLatin1String("KLV metadata")) return true;
    return false;
}

bool KlvReceiver::hasVideoPad() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &[name, p] : m_pads)
        if (p.kind.contains(QLatin1String("video"))) return true;
    return false;
}

std::vector<KlvReceiver::PadInfo> KlvReceiver::pads() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PadInfo> out;
    for (const auto &[name, p] : m_pads) out.push_back(p);
    return out;
}
