#include "gamepad.h"

#include <QSettings>

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QByteArray>

// xpad reports the X button as BTN_X (= BTN_NORTH) and Y as BTN_Y (= BTN_WEST)
static const struct { int code; PadChannel c; } kKeys[] = {
    {BTN_SOUTH, PadChannel::A},       {BTN_EAST, PadChannel::B},       {BTN_X, PadChannel::X},
    {BTN_Y, PadChannel::Y},           {BTN_TL, PadChannel::LB},        {BTN_TR, PadChannel::RB},
    {BTN_SELECT, PadChannel::Back},   {BTN_START, PadChannel::Start},  {BTN_MODE, PadChannel::Guide},
    {BTN_THUMBL, PadChannel::L3},     {BTN_THUMBR, PadChannel::R3}};
#endif

static const char *const kChannelNames[kPadChannels] = {"A", "B", "X", "Y", "LB", "RB", "BACK", "START", "XBOX", "L3", "R3",
                                                        "LX", "LY", "RX", "RY", "LT", "RT", "DPAD_X", "DPAD_Y"};
static const struct { const char *name, *shortName; } kFunctions[kPadFunctions] = {
    {"None", "-"},           {"Gimbal pitch", "Pitch"},   {"Gimbal yaw", "Yaw"},       {"Gimbal roll", "Roll"},
    {"Zoom", "Zoom"},        {"Gimbal home", "Home"},     {"Gimbal mode", "Mode"},     {"Capture image", "Capture"},
    {"Record video", "Record"}, {"Change view", "View"},  {"Change record", "Rec src"}, {"Tracking", "Track"},
    {"Zoom tele", "Tele"},   {"Zoom wide", "Wide"}};

const char *padChannelName(PadChannel c) { return kChannelNames[int(c)]; }
const char *padFunctionName(PadFunction f) { return kFunctions[int(f)].name; }
const char *padFunctionShort(PadFunction f) { return kFunctions[int(f)].shortName; }
bool padIsButton(PadChannel c) { return c < PadChannel::LX; }
bool padIsContinuous(PadFunction f) { return f >= PadFunction::Pitch && f <= PadFunction::Zoom; }
bool padAllowed(PadChannel c, PadFunction f) { return !(padIsButton(c) && padIsContinuous(f)); }

static int stickIndex(PadChannel c) { return c >= PadChannel::LX && c <= PadChannel::RY ? int(c) - int(PadChannel::LX) : -1; }

GamepadConfig::GamepadConfig()
{
    using C = PadChannel;
    using F = PadFunction;
    const std::pair<C, F> defaults[] = {{C::A, F::Home},       {C::B, F::Capture},  {C::X, F::Record}, {C::Y, F::View},
                                        {C::LB, F::RecordSource}, {C::RB, F::Mode}, {C::Start, F::Tracking},
                                        {C::LX, F::Yaw},       {C::LY, F::Pitch},   {C::RY, F::Zoom}};
    for (const auto &[c, f] : defaults) function[int(c)] = f;
    std::fill(std::begin(deadZonePercent), std::end(deadZonePercent), kDeadZoneDefault);
}

bool GamepadConfig::assign(PadChannel c, PadFunction f)
{
    if (!padAllowed(c, f)) return false;
    if (f != PadFunction::None)
        for (PadFunction &other : function)
            if (other == f) other = PadFunction::None;
    function[int(c)] = f;
    return true;
}

double GamepadConfig::axis(PadChannel c, double raw) const
{
    const int i = stickIndex(c);
    if (i < 0) return raw;
    const double v = invert[i] ? -raw : raw, dz = deadZonePercent[i] / 100.0, a = std::abs(v);
    if (a <= dz) return 0;
    return std::copysign(std::min(1.0, (a - dz) / (1 - dz)), v);
}

void GamepadConfig::save(QSettings &s) const
{
    s.beginGroup(QStringLiteral("gamepad"));
    for (int c = 0; c < kPadChannels; ++c) s.setValue(QLatin1String(kChannelNames[c]), QLatin1String(padFunctionName(function[c])));
    for (int i = 0; i < kPadSticks; ++i) {
        const QLatin1String axis(kChannelNames[int(PadChannel::LX) + i]);
        s.setValue(QStringLiteral("invert/") + axis, invert[i]);
        s.setValue(QStringLiteral("deadzone/") + axis, deadZonePercent[i]);
    }
    s.endGroup();
}

GamepadConfig GamepadConfig::load(QSettings &s)
{
    GamepadConfig g;
    s.beginGroup(QStringLiteral("gamepad"));
    for (int c = 0; c < kPadChannels; ++c) {
        const QString name = s.value(QLatin1String(kChannelNames[c])).toString();
        for (int f = 0; f < kPadFunctions; ++f)
            if (name == QLatin1String(padFunctionName(PadFunction(f)))) g.assign(PadChannel(c), PadFunction(f));
    }
    for (int i = 0; i < kPadSticks; ++i) {
        const QLatin1String axis(kChannelNames[int(PadChannel::LX) + i]);
        g.invert[i] = s.value(QStringLiteral("invert/") + axis, false).toBool();
        g.deadZonePercent[i] = std::clamp(s.value(QStringLiteral("deadzone/") + axis, kDeadZoneDefault).toInt(), 0, kDeadZoneMax);
    }
    s.endGroup();
    return g;
}

GamepadOutput gamepadOutput(const GamepadConfig &config, const GamepadState &previous, const GamepadState &current)
{
    GamepadOutput o;
    if (!current.connected) return o;
    for (int i = 0; i < kPadChannels; ++i) {
        const auto c = PadChannel(i);
        const PadFunction f = config[c];
        const double v = config.axis(c, current[c]);
        switch (f) {
        case PadFunction::None: break;
        case PadFunction::Pitch: o.pitch = v; break;
        case PadFunction::Yaw: o.yaw = v; break;
        case PadFunction::Roll: o.roll = v; break;
        case PadFunction::Zoom: o.zoom = v > GamepadMap::kActive ? 1 : v < -GamepadMap::kActive ? -1 : 0; break;
        default:
            if (previous.connected && std::abs(v) > GamepadMap::kActive && std::abs(config.axis(c, previous[c])) <= GamepadMap::kActive)
                o.pressed |= 1u << int(f);
        }
    }
    return o;
}

static qint64 monoMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Gamepad::~Gamepad() { close(); }

#ifdef _WIN32
GamepadState Gamepad::poll()
{
    if (!m_state.connected) {
        const qint64 now = monoMs();
        if (now < m_nextScanMs) return m_state;
        m_nextScanMs = now + 1000;
        if (!open()) return m_state;
    }
    XINPUT_STATE s{};
    if (XInputGetState(DWORD(m_slot), &s) != ERROR_SUCCESS) {
        close();
        return m_state;
    }
    const XINPUT_GAMEPAD &g = s.Gamepad;
    GamepadState &m = m_state;
    m[PadChannel::LX] = std::max(-1.0, g.sThumbLX / 32767.0);
    m[PadChannel::LY] = std::max(-1.0, g.sThumbLY / 32767.0);  // XInput: up is positive already
    m[PadChannel::RX] = std::max(-1.0, g.sThumbRX / 32767.0);
    m[PadChannel::RY] = std::max(-1.0, g.sThumbRY / 32767.0);
    m[PadChannel::LT] = g.bLeftTrigger / 255.0;
    m[PadChannel::RT] = g.bRightTrigger / 255.0;
    const auto held = [&](WORD mask) { return (g.wButtons & mask) ? 1.0 : 0.0; };
    m[PadChannel::DpadX] = held(XINPUT_GAMEPAD_DPAD_RIGHT) - held(XINPUT_GAMEPAD_DPAD_LEFT);
    m[PadChannel::DpadY] = held(XINPUT_GAMEPAD_DPAD_UP) - held(XINPUT_GAMEPAD_DPAD_DOWN);
    const struct { WORD mask; PadChannel c; } keys[] = {
        {XINPUT_GAMEPAD_A, PadChannel::A},          {XINPUT_GAMEPAD_B, PadChannel::B},
        {XINPUT_GAMEPAD_X, PadChannel::X},          {XINPUT_GAMEPAD_Y, PadChannel::Y},
        {XINPUT_GAMEPAD_LEFT_SHOULDER, PadChannel::LB}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, PadChannel::RB},
        {XINPUT_GAMEPAD_BACK, PadChannel::Back},    {XINPUT_GAMEPAD_START, PadChannel::Start},
        {XINPUT_GAMEPAD_LEFT_THUMB, PadChannel::L3}, {XINPUT_GAMEPAD_RIGHT_THUMB, PadChannel::R3}};  // no guide button in XInput
    for (const auto &k : keys) m[k.c] = held(k.mask);
    return m_state;
}

bool Gamepad::open()
{
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE s{};
        if (XInputGetState(i, &s) != ERROR_SUCCESS) continue;
        m_slot = int(i);
        m_state = {};
        m_state.connected = true;
        m_state.name = QStringLiteral("Xbox controller (XInput %1)").arg(i + 1);
        return true;
    }
    return false;
}

void Gamepad::close()
{
    m_slot = -1;
    m_state = {};
}
#else
static bool testBit(const unsigned long *bits, int bit)
{
    constexpr int perLong = 8 * sizeof(unsigned long);
    return (bits[bit / perLong] >> (bit % perLong)) & 1UL;
}

// evdev axis to the GamepadState range: triggers 0..1, the rest -1..1 with y flipped so up is positive
static double axisValue(PadChannel c, int value, int min, int max)
{
    if (c == PadChannel::LT || c == PadChannel::RT) return std::clamp(double(value - min) / (max - min), 0.0, 1.0);
    const double half = (max - min) / 2.0;
    const double v = std::clamp((value - (min + half)) / half, -1.0, 1.0);
    return c == PadChannel::LY || c == PadChannel::RY || c == PadChannel::DpadY ? -v : v;
}

GamepadState Gamepad::poll()
{
    if (!m_state.connected) {
        const qint64 now = monoMs();
        if (now < m_nextScanMs) return m_state;
        m_nextScanMs = now + 1000;
        if (!open()) return m_state;
    }
    input_event ev;
    bool resync = false;
    ssize_t n;
    while ((n = read(m_fd, &ev, sizeof ev)) == ssize_t(sizeof ev)) {
        if (ev.type == EV_SYN && ev.code == SYN_DROPPED) resync = true;
        if (ev.type == EV_ABS)
            for (int c = int(PadChannel::LX); c < kPadChannels; ++c)
                if (ev.code == m_code[c]) m_state.value[c] = axisValue(PadChannel(c), ev.value, m_min[c], m_max[c]);
        if (ev.type == EV_KEY)
            for (const auto &k : kKeys)
                if (ev.code == k.code) m_state[k.c] = ev.value ? 1 : 0;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {  // ENODEV: unplugged
        close();
        return m_state;
    }
    if (resync) {  // the kernel dropped events: read the current state instead of trusting deltas
        close();
        open();
    }
    return m_state;
}

bool Gamepad::open()
{
    constexpr int perLong = 8 * sizeof(unsigned long);
    for (int i = 0; i < 64; ++i) {
        const QByteArray path = "/dev/input/event" + QByteArray::number(i);
        const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;  // missing, or no permission (a pad gets the desktop user's ACL from systemd's uaccess rule)
        unsigned long keys[KEY_MAX / perLong + 1] = {}, abs[ABS_MAX / perLong + 1] = {};
        const bool pad = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs), abs) >= 0 &&
                         testBit(keys, BTN_SOUTH) && testBit(abs, ABS_X) && testBit(abs, ABS_Y);
        if (!pad) {
            ::close(fd);
            continue;
        }
        // xpad and xpadneo: right stick RX/RY, triggers Z/RZ. A Bluetooth pad on the generic HID driver: right stick Z/RZ,
        // triggers BRAKE/GAS.
        const bool rxy = testBit(abs, ABS_RX) && testBit(abs, ABS_RY);
        const int codes[] = {ABS_X, ABS_Y, rxy ? ABS_RX : ABS_Z, rxy ? ABS_RY : ABS_RZ, rxy ? ABS_Z : ABS_BRAKE,
                             rxy ? ABS_RZ : ABS_GAS, ABS_HAT0X, ABS_HAT0Y};  // LX .. DpadY
        m_state = {};
        std::fill(std::begin(m_code), std::end(m_code), -1);
        for (int c = int(PadChannel::LX); c < kPadChannels; ++c) {
            const int code = codes[c - int(PadChannel::LX)];
            input_absinfo info{};
            if (!testBit(abs, code) || ioctl(fd, EVIOCGABS(code), &info) < 0 || info.maximum <= info.minimum) continue;
            m_code[c] = code;
            m_min[c] = info.minimum;
            m_max[c] = info.maximum;
            m_state.value[c] = axisValue(PadChannel(c), info.value, info.minimum, info.maximum);
        }
        unsigned long held[KEY_MAX / perLong + 1] = {};
        ioctl(fd, EVIOCGKEY(sizeof held), held);
        for (const auto &k : kKeys) m_state[k.c] = testBit(held, k.code) ? 1 : 0;
        char name[128] = "";
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        m_state.name = QString::fromUtf8(name);
        m_state.connected = true;
        m_fd = fd;
        return true;
    }
    return false;
}

void Gamepad::close()
{
    if (m_fd >= 0) ::close(m_fd);
    m_fd = -1;
    m_state = {};
}
#endif
