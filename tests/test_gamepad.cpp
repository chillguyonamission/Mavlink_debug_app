// Gamepad mapping, following Gremsy's gamepad configuration document: defaults, one function per channel, invert,
// deadzone, presses from any channel, and the saved settings. No pad needed.
#include "gamepad.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using C = PadChannel;
using F = PadFunction;

static GamepadState pad()
{
    GamepadState s;
    s.connected = true;
    return s;
}

class TestGamepad : public QObject
{
    Q_OBJECT
private slots:
    void defaults()  // the document's "Default assignment"
    {
        const GamepadConfig g;
        const std::pair<C, F> expected[] = {{C::A, F::Home}, {C::B, F::Capture}, {C::X, F::Record}, {C::Y, F::View},
                                            {C::LB, F::RecordSource}, {C::RB, F::Mode}, {C::Start, F::Tracking},
                                            {C::LX, F::Yaw}, {C::LY, F::Pitch}, {C::RY, F::Zoom}, {C::RX, F::None},
                                            {C::LT, F::None}, {C::Guide, F::None}, {C::DpadY, F::None}};
        for (const auto &[c, f] : expected) QCOMPARE(g[c], f);
        for (int i = 0; i < kPadSticks; ++i) {
            QCOMPARE(g.deadZonePercent[i], 8);
            QVERIFY(!g.invert[i]);
        }
    }
    void oneFunctionOneChannel()
    {
        GamepadConfig g;
        QVERIFY(g.assign(C::B, F::Home));  // the document's example: Home moves from A to B
        QCOMPARE(g[C::B], F::Home);
        QCOMPARE(g[C::A], F::None);
        QVERIFY(g.assign(C::LB, F::ZoomTele));
        QCOMPARE(g[C::LB], F::ZoomTele);
        QVERIFY(!g.assign(C::X, F::Pitch));  // buttons take no continuous function
        QCOMPARE(g[C::LY], F::Pitch);
        QVERIFY(g.assign(C::DpadY, F::Pitch));
        QCOMPARE(g[C::LY], F::None);
        QVERIFY(g.assign(C::A, F::None));
        QCOMPARE(g[C::A], F::None);
    }
    void deadZoneAndInvert()
    {
        GamepadConfig g;
        QCOMPARE(g.axis(C::LY, 0.08), 0.0);
        QVERIFY(g.axis(C::LY, 0.1) > 0 && g.axis(C::LY, 0.1) < 0.05);  // rescaled: no jump at the deadzone edge
        QCOMPARE(g.axis(C::LY, 1.0), 1.0);
        QCOMPARE(g.axis(C::LY, -1.0), -1.0);
        g.deadZonePercent[int(C::LY) - int(C::LX)] = 0;
        QCOMPARE(g.axis(C::LY, 0.05), 0.05);
        g.invert[int(C::LY) - int(C::LX)] = true;  // the document's example: up tilts the gimbal down
        GamepadState up = pad();
        up[C::LY] = 1;
        QCOMPARE(gamepadOutput(g, up, up).pitch, -1.0);
        QCOMPARE(g.axis(C::LT, 0.05), 0.05);  // no deadzone or invert on triggers
    }
    void sticks()
    {
        const GamepadConfig g;
        GamepadState s = pad();
        s[C::LX] = 1;
        s[C::LY] = -1;
        s[C::RY] = 0.6;
        GamepadOutput o = gamepadOutput(g, s, s);
        QCOMPARE(o.yaw, 1.0);
        QCOMPARE(o.pitch, -1.0);
        QCOMPARE(o.roll, 0.0);
        QCOMPARE(o.zoom, 1);
        s[C::RY] = -0.6;
        QCOMPARE(gamepadOutput(g, s, s).zoom, -1);
        s[C::RY] = 0.4;
        QCOMPARE(gamepadOutput(g, s, s).zoom, 0);
    }
    void disconnectedStops()
    {
        const GamepadConfig g;
        GamepadState held = pad();
        held[C::LX] = held[C::LY] = held[C::RY] = 1;
        GamepadState gone = held;  // a stale reading after unplugging must not keep the gimbal moving
        gone.connected = false;
        const GamepadOutput o = gamepadOutput(g, held, gone);
        QCOMPARE(o.yaw, 0.0);
        QCOMPARE(o.pitch, 0.0);
        QCOMPARE(o.zoom, 0);
        QCOMPARE(o.pressed, 0u);
    }
    void presses()
    {
        GamepadConfig g;
        const GamepadState up = pad();
        GamepadState down = up;
        down[C::A] = 1;
        QVERIFY(gamepadOutput(g, up, down).was(F::Home));
        QVERIFY(!gamepadOutput(g, down, down).was(F::Home));  // held: one press only
        QVERIFY(!gamepadOutput(g, down, up).was(F::Home));    // release
        GamepadState before;                                   // not connected yet
        QVERIFY(!gamepadOutput(g, before, down).was(F::Home)); // held while connecting: not a press
        down[C::Start] = 1;
        QVERIFY(gamepadOutput(g, up, down).was(F::Tracking));

        QVERIFY(g.assign(C::RT, F::ZoomTele));  // a trigger past half its travel
        QVERIFY(g.assign(C::DpadX, F::ZoomWide));  // either D-pad direction
        GamepadState t = up;
        t[C::RT] = 0.4;
        QVERIFY(!gamepadOutput(g, up, t).was(F::ZoomTele));
        t[C::RT] = 0.9;
        t[C::DpadX] = -1;
        const GamepadOutput o = gamepadOutput(g, up, t);
        QVERIFY(o.was(F::ZoomTele) && o.was(F::ZoomWide));
        QVERIFY(!o.was(F::Home));
    }
    void savedAndRestored()
    {
        QTemporaryDir dir;
        QSettings s(dir.filePath(QStringLiteral("pad.ini")), QSettings::IniFormat);
        GamepadConfig g;
        g.assign(C::LB, F::ZoomTele);
        g.assign(C::RX, F::Roll);
        g.invert[1] = true;
        g.deadZonePercent[3] = 20;
        g.save(s);
        const GamepadConfig r = GamepadConfig::load(s);
        for (int c = 0; c < kPadChannels; ++c) QCOMPARE(r.function[c], g.function[c]);
        QVERIFY(r.invert[1] && !r.invert[0]);
        QCOMPARE(r.deadZonePercent[3], 20);
        QCOMPARE(r.deadZonePercent[0], 8);

        s.setValue(QStringLiteral("gamepad/B"), QStringLiteral("Gimbal pitch"));  // not allowed on a button: B keeps its value
        s.setValue(QStringLiteral("gamepad/deadzone/LX"), 90);                   // out of range: clamped
        const GamepadConfig bad = GamepadConfig::load(s);
        QCOMPARE(bad[C::B], F::Capture);
        QCOMPARE(bad.deadZonePercent[0], GamepadConfig::kDeadZoneMax);

        QSettings empty(dir.filePath(QStringLiteral("none.ini")), QSettings::IniFormat);
        QCOMPARE(GamepadConfig::load(empty)[C::A], F::Home);  // nothing saved: defaults
    }
};

QTEST_GUILESS_MAIN(TestGamepad)
#include "test_gamepad.moc"
