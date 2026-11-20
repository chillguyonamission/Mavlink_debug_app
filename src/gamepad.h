#pragma once
// Gamepad input and its configurable mapping, following Gremsy's "Configuration Page - Gamepad control" document
// (Payload Plus 1.2.3): every button, stick, trigger and D-pad axis is a channel, each channel carries at most one
// function, and each stick axis has its own invert and deadzone. The reader is the app's own (evdev / XInput).

#include <QString>

class QSettings;

enum class PadChannel {
    A, B, X, Y, LB, RB, Back, Start, Guide, L3, R3,  // buttons
    LX, LY, RX, RY,                                  // sticks (the only axes with invert and deadzone)
    LT, RT,                                          // triggers, 0..1
    DpadX, DpadY,                                    // -1, 0, 1
    Count
};
constexpr int kPadChannels = int(PadChannel::Count);
constexpr int kPadSticks = 4;  // LX, LY, RX, RY

enum class PadFunction {
    None, Pitch, Yaw, Roll, Zoom,  // continuous
    Home, Mode, Capture, Record, View, RecordSource, Tracking, ZoomTele, ZoomWide,  // press
    Count
};
constexpr int kPadFunctions = int(PadFunction::Count);

const char *padChannelName(PadChannel c);    // "A", "LX", "DPAD_X"
const char *padFunctionName(PadFunction f);  // "Gimbal pitch"
const char *padFunctionShort(PadFunction f); // "Pitch", the CHANNELS picture's label
bool padIsButton(PadChannel c);
bool padIsContinuous(PadFunction f);
bool padAllowed(PadChannel c, PadFunction f);  // buttons take no continuous function

struct GamepadState {
    bool connected = false;
    QString name;
    double value[kPadChannels] = {};  // buttons 0/1; sticks and D-pad -1..1 (right and up positive); triggers 0..1
    double operator[](PadChannel c) const { return value[int(c)]; }
    double &operator[](PadChannel c) { return value[int(c)]; }
};

struct GamepadConfig {
    PadFunction function[kPadChannels] = {};
    bool invert[kPadSticks] = {};
    int deadZonePercent[kPadSticks] = {};
    static constexpr int kDeadZoneDefault = 8, kDeadZoneMax = 30;

    GamepadConfig();  // the document's default assignment
    PadFunction operator[](PadChannel c) const { return function[int(c)]; }
    // One function, one channel: a function already on another channel moves here. False if not allowed.
    bool assign(PadChannel c, PadFunction f);
    double axis(PadChannel c, double raw) const;  // invert, then deadzone removed and the rest rescaled (sticks only)

    void save(QSettings &s) const;
    static GamepadConfig load(QSettings &s);  // unknown or disallowed entries keep the default
};

struct GamepadOutput {
    double pitch = 0, yaw = 0, roll = 0;  // -1..1 of the gimbal speed slider
    int zoom = 0;                         // MAVLink continuous zoom: +1 tele, -1 wide, 0 stop
    unsigned pressed = 0;                 // bit (1 << PadFunction) for press functions whose channel just went active
    bool was(PadFunction f) const { return pressed & (1u << int(f)); }
};

namespace GamepadMap {
constexpr double kActive = 0.5;  // a stick, trigger or D-pad counts as pressed past half its travel (the SDK sample's zoom rule)
}

// A disconnected pad gives all zeros, which stops motion. A channel already active when the pad connected is not a press.
GamepadOutput gamepadOutput(const GamepadConfig &config, const GamepadState &previous, const GamepadState &current);

class Gamepad
{
public:
    Gamepad() = default;
    ~Gamepad();
    Gamepad(const Gamepad &) = delete;
    Gamepad &operator=(const Gamepad &) = delete;

    GamepadState poll();  // never blocks; looks for a pad at most once a second while none is open

private:
    void close();
    bool open();

    GamepadState m_state;
    qint64 m_nextScanMs = 0;
#ifdef _WIN32
    int m_slot = -1;  // XInput user index
#else
    int m_fd = -1;  // /dev/input/eventN
    int m_code[kPadChannels] = {};  // ABS code per axis channel, -1 if absent
    int m_min[kPadChannels] = {}, m_max[kPadChannels] = {};
#endif
};
