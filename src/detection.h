#pragma once
// Onboard object detection metadata: the SDK's det_packet_t, validated and mapped onto the video picture.
// Separate from object tracking: this is only what the payload's detector reports. Pure, so it is unit tested.

#include "detection_packet.h"

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>

#include <deque>
#include <vector>

struct DetectionBox {
    QRect rect;           // top-left corner and size in DET_REF_WIDTH x DET_REF_HEIGHT pixels, clipped to that frame
    quint16 trackId = 0;  // 0 = no track assigned yet
    quint8 classId = 0;   // meaning depends on the model the payload runs; the packet has no label
    quint8 confidence = 0;  // 0-255 = 0-100%
};

struct DetectionFrame {
    quint32 frameId = 0;
    std::vector<DetectionBox> boxes;
    int rejected = 0;  // boxes the packet claimed that have no usable geometry
    qint64 monoMs = 0, wallMs = 0;  // when it was received: the packet itself has no timestamp
};

// MAVLink 2 trims trailing zero bytes and the decoder zero-fills them again, so a short frame is normal and the wire
// length cannot tell a truncated packet from a trimmed one. A box the wire did not carry reads as w = h = 0, and is
// rejected here with any other box that has no area or starts outside the reference frame.
DetectionFrame parseDetectionPacket(const det_packet_t &pkt);

enum class DetectionState {
    Off,         // not enabled from here and no packets: support unknown
    Waiting,     // enabled, no packet yet
    NoResponse,  // enabled, still no packet after kDetectionNoResponseMs
    Empty,       // packets arriving, nothing detected
    Boxes,       // packets arriving, with boxes
    Stale,       // packets stopped while enabled
    Rejected     // the payload refused the mode or the stream request
};

constexpr qint64 kDetectionStaleMs = 1500;  // the SDK example's DET_TIMEOUT_MS: the payload just stops when detection is off
constexpr qint64 kDetectionNoResponseMs = 5000;

// enabledMs and lastPacketMs are monotonic, -1 = never.
DetectionState detectionState(qint64 enabledMs, qint64 lastPacketMs, int boxes, bool rejected, qint64 nowMs);
inline bool detectionLive(DetectionState s) { return s == DetectionState::Empty || s == DetectionState::Boxes; }
QString detectionStateText(DetectionState s);

// The box in the widget, given the rectangle the video is painted in.
QRectF detectionRectInPicture(const QRect &box, const QRectF &picture);
// The smallest box under pos that has a track ID, so it can be followed across packets. nullptr = none.
const DetectionBox *detectionAt(const std::vector<DetectionBox> &boxes, QPointF pos, const QRectF &picture);
QString detectionLabel(const DetectionBox &b);  // "#7 class 3 87%": class numbers only, names depend on the model
long detectionConfidencePercent(const DetectionBox &b);
// "3 (motorcycle?)": a guess from the COCO table in the SDK's example for its EO model, for the data window. Other models
// number classes differently, so an ID outside the table is only a number.
QString detectionClassGuess(int classId);
double detectionRate(const std::deque<DetectionFrame> &log, qint64 nowMs);  // packets per second, over the last 2 s
