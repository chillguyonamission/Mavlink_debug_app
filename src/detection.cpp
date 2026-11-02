#include "detection.h"

#include <algorithm>
#include <cmath>
#include <iterator>

DetectionFrame parseDetectionPacket(const det_packet_t &pkt)
{
    const QRect frame(0, 0, DET_REF_WIDTH, DET_REF_HEIGHT);
    DetectionFrame out;
    out.frameId = pkt.frame_id;
    const int n = std::min<int>(pkt.num_boxes, DET_MAX_BOXES);
    out.rejected = pkt.num_boxes - n;
    for (int i = 0; i < n; ++i) {
        const det_box_t b = pkt.boxes[i];  // a copy: the packed struct's fields may be unaligned
        // A box wider than the frame is garbage, not an object at the edge; one that only overruns the edge is clipped
        if (!b.w || !b.h || b.x >= DET_REF_WIDTH || b.y >= DET_REF_HEIGHT || b.w > DET_REF_WIDTH || b.h > DET_REF_HEIGHT) {
            ++out.rejected;
            continue;
        }
        out.boxes.push_back({QRect(b.x, b.y, b.w, b.h) & frame, b.track_id, b.class_id, b.confidence});
    }
    return out;
}

DetectionState detectionState(qint64 enabledMs, qint64 lastPacketMs, int boxes, bool rejected, qint64 nowMs)
{
    // Packets count first, even if not requested from here: they are what the payload is doing
    if (lastPacketMs >= 0 && nowMs - lastPacketMs <= kDetectionStaleMs) return boxes ? DetectionState::Boxes : DetectionState::Empty;
    if (enabledMs < 0) return DetectionState::Off;
    if (lastPacketMs >= 0) return DetectionState::Stale;
    if (rejected) return DetectionState::Rejected;
    return nowMs - enabledMs <= kDetectionNoResponseMs ? DetectionState::Waiting : DetectionState::NoResponse;
}

QString detectionStateText(DetectionState s)
{
    switch (s) {
    case DetectionState::Off: return QStringLiteral("Off (support unknown until enabled)");
    case DetectionState::Waiting: return QStringLiteral("Waiting for the first packet");
    case DetectionState::NoResponse: return QStringLiteral("No response: no packets from the payload");
    case DetectionState::Empty: return QStringLiteral("Active, nothing detected");
    case DetectionState::Boxes: return QStringLiteral("Active");
    case DetectionState::Stale: return QStringLiteral("Stale: packets stopped");
    case DetectionState::Rejected: return QStringLiteral("Refused by the payload (unsupported?)");
    }
    return {};
}

QRectF detectionRectInPicture(const QRect &box, const QRectF &picture)
{
    const double sx = picture.width() / DET_REF_WIDTH, sy = picture.height() / DET_REF_HEIGHT;
    return QRectF(picture.x() + box.x() * sx, picture.y() + box.y() * sy, box.width() * sx, box.height() * sy);
}

const DetectionBox *detectionAt(const std::vector<DetectionBox> &boxes, QPointF pos, const QRectF &picture)
{
    const DetectionBox *best = nullptr;
    for (const DetectionBox &b : boxes)
        if (b.trackId && detectionRectInPicture(b.rect, picture).contains(pos) &&
            (!best || b.rect.width() * b.rect.height() < best->rect.width() * best->rect.height()))
            best = &b;
    return best;
}

QString detectionLabel(const DetectionBox &b)
{
    const QString s = QStringLiteral("class %1 %2%").arg(b.classId).arg(detectionConfidencePercent(b));
    return b.trackId ? QStringLiteral("#%1 ").arg(b.trackId) + s : s;
}

long detectionConfidencePercent(const DetectionBox &b) { return std::lround(b.confidence * 100.0 / 255); }

QString detectionClassGuess(int classId)
{
    // COCO_LABELS in the SDK's examples/payload_get_bounding_box_detection.cpp
    static const char *const coco[] = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
        "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"};
    if (classId < 0 || classId >= int(std::size(coco))) return QString::number(classId);
    return QStringLiteral("%1 (%2?)").arg(classId).arg(QLatin1String(coco[classId]));
}

double detectionRate(const std::deque<DetectionFrame> &log, qint64 nowMs)
{
    const qint64 windowMs = 2000;
    int n = 0;
    for (auto it = log.rbegin(); it != log.rend() && nowMs - it->monoMs < windowMs; ++it) ++n;
    return n * 1000.0 / windowMs;
}
