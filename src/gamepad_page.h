#pragma once
// Analyze › Gamepad: the configuration page of Gremsy's "Configuration Page - Gamepad control" document. Status, SET CHANNEL,
// JOYSTICK (invert and deadzone per stick axis), ANALOG INPUT and the CHANNELS picture. Changes are saved at once.

#include "gamepad.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class GamepadAnalogView;
class GamepadPicture;

class GamepadPage : public QWidget
{
    Q_OBJECT
public:
    explicit GamepadPage(QWidget *parent = nullptr);
    const GamepadConfig &config() const { return m_config; }
    void setState(const GamepadState &s);  // every poll; repaints only while visible

private:
    void selectChannel(PadChannel c);
    void configChanged();  // save, then show the new config everywhere

    GamepadConfig m_config;
    GamepadState m_state;
    QLabel *m_status, *m_name;
    QComboBox *m_channel, *m_function;
    QCheckBox *m_invert[kPadSticks];
    QSlider *m_deadZone[kPadSticks];
    QLabel *m_deadZoneText[kPadSticks];
    GamepadAnalogView *m_analog;
    GamepadPicture *m_picture;
};
