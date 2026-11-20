#include "gamepad_page.h"

#include "theme.h"
#include "ui_util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QSlider>

#include <cmath>
#include <functional>

static bool active(double v, int sign)  // sign 0: either direction
{
    return sign > 0 ? v > GamepadMap::kActive : sign < 0 ? v < -GamepadMap::kActive : std::abs(v) > GamepadMap::kActive;
}

// Both sticks (dot = position with the deadzone applied, not inverted) and the LT / RT bars
class GamepadAnalogView : public QWidget
{
public:
    GamepadAnalogView(const GamepadConfig &config, const GamepadState &state) : m_config(config), m_state(state)
    {
        setMinimumSize(260, 200);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double barsH = 44, r = std::min((width() - 30) / 4.0, (height() - barsH - 30) / 2.0);
        const auto shown = [&](PadChannel c) {
            const double v = m_config.axis(c, m_state[c]);
            const int i = int(c) - int(PadChannel::LX);
            return m_config.invert[i] ? -v : v;
        };
        const struct { PadChannel x, y; const char *label; } sticks[] = {{PadChannel::LX, PadChannel::LY, "LEFT (LX / LY)"},
                                                                         {PadChannel::RX, PadChannel::RY, "RIGHT (RX / RY)"}};
        for (int k = 0; k < 2; ++k) {
            const QPointF c(width() * (k ? 0.75 : 0.25), 10 + r);
            p.setPen(QPen(QColor(Theme::Border), 1));
            p.setBrush(QColor(Theme::Raised));
            p.drawEllipse(c, r, r);
            p.drawLine(c - QPointF(r, 0), c + QPointF(r, 0));
            p.drawLine(c - QPointF(0, r), c + QPointF(0, r));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(m_state.connected ? Theme::Accent : Theme::Dim));
            p.drawEllipse(c + QPointF(shown(sticks[k].x) * r, -shown(sticks[k].y) * r), 6, 6);
            p.setPen(QColor(Theme::Dim));
            p.drawText(QRectF(c.x() - r, c.y() + r + 2, 2 * r, 16), Qt::AlignCenter, QLatin1String(sticks[k].label));
        }
        const PadChannel triggers[] = {PadChannel::LT, PadChannel::RT};
        for (int k = 0; k < 2; ++k) {
            const QRectF bar(30, height() - barsH + k * 22, width() - 40, 8);
            p.setPen(QColor(Theme::Dim));
            p.drawText(QRectF(0, bar.y() - 4, 26, 16), Qt::AlignLeft | Qt::AlignVCenter, QLatin1String(padChannelName(triggers[k])));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(Theme::Raised));
            p.drawRoundedRect(bar, 3, 3);
            p.setBrush(QColor(Theme::Accent));
            p.drawRoundedRect(QRectF(bar.topLeft(), QSizeF(bar.width() * m_state[triggers[k]], bar.height())), 3, 3);
        }
    }

private:
    const GamepadConfig &m_config;
    const GamepadState &m_state;
};

// The pad drawn as clickable controls, each labelled with its function. Pressed: blue. Selected: white border.
class GamepadPicture : public QWidget
{
public:
    enum Side { NoLabel, Left, Right, Above, Below };
    struct Item {
        PadChannel channel;
        QRectF rect;  // in a 600 x 340 design space
        bool round;
        Side side;
        int sign;            // which direction lights it: 0 either, +1 / -1 for one D-pad arrow
        const char *text;    // nullptr: the channel name
    };

    GamepadPicture(const GamepadConfig &config, const GamepadState &state, std::function<void(PadChannel)> clicked)
        : m_config(config), m_state(state), m_clicked(std::move(clicked))
    {
        setMinimumSize(420, 240);
        setCursor(Qt::PointingHandCursor);
    }
    void setSelected(PadChannel c)
    {
        m_selected = c;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setTransform(designTransform());
        p.setPen(QPen(QColor(Theme::Border), 2));
        p.setBrush(QColor(Theme::Panel));
        p.drawRoundedRect(QRectF(40, 80, 520, 190), 95, 95);
        QFont f = font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        for (const Item &it : kItems) {
            const bool lit = m_state.connected && active(m_state[it.channel], it.sign);
            p.setPen(QPen(QColor(it.channel == m_selected ? "#ffffff" : Theme::Border), it.channel == m_selected ? 2 : 1));
            p.setBrush(QColor(lit ? Theme::Accent : Theme::Raised));
            if (it.round) p.drawEllipse(it.rect);
            else p.drawRoundedRect(it.rect, 5, 5);
            p.setPen(QColor(lit ? Theme::Bg : Theme::Text));
            p.drawText(it.rect, Qt::AlignCenter, QLatin1String(it.text ? it.text : padChannelName(it.channel)));
            if (it.side == NoLabel) continue;
            const PadFunction fn = m_config[it.channel];
            p.setPen(QColor(fn == PadFunction::None ? Theme::Dim : Theme::Accent));
            const QRectF r = it.rect;
            QRectF box;
            int align = Qt::AlignVCenter;
            switch (it.side) {
            case Left: box = QRectF(r.left() - 74, r.top(), 70, r.height()), align |= Qt::AlignRight; break;
            case Right: box = QRectF(r.right() + 4, r.top(), 70, r.height()), align |= Qt::AlignLeft; break;
            case Above: box = QRectF(r.center().x() - 40, r.top() - 16, 80, 14), align = Qt::AlignCenter; break;
            default: box = QRectF(r.center().x() - 40, r.bottom() + 2, 80, 14), align = Qt::AlignCenter; break;
            }
            p.drawText(box, align, QLatin1String(padFunctionShort(fn)));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const QPointF pos = designTransform().inverted().map(e->position());
        for (const Item &it : kItems)
            if (it.rect.adjusted(-3, -3, 3, 3).contains(pos)) return m_clicked(it.channel);
    }

private:
    QTransform designTransform() const
    {
        const double s = std::min(width() / 600.0, height() / 340.0);
        return QTransform().translate((width() - 600 * s) / 2, (height() - 340 * s) / 2).scale(s, s);
    }

    using C = PadChannel;
    static constexpr Item kItems[] = {
        {C::LT, {70, 10, 100, 24}, false, Left, 0, nullptr},     {C::LB, {70, 44, 100, 24}, false, Left, 0, nullptr},
        {C::RT, {430, 10, 100, 24}, false, Right, 0, nullptr},   {C::RB, {430, 44, 100, 24}, false, Right, 0, nullptr},
        {C::DpadY, {110, 112, 24, 24}, false, NoLabel, 1, "^"},  {C::DpadY, {110, 172, 24, 24}, false, NoLabel, -1, "v"},
        {C::DpadX, {80, 142, 24, 24}, false, NoLabel, -1, "<"},  {C::DpadX, {140, 142, 24, 24}, false, NoLabel, 1, ">"},
        {C::Back, {215, 112, 54, 24}, false, Below, 0, nullptr}, {C::Guide, {280, 104, 40, 40}, true, Below, 0, nullptr},
        {C::Start, {331, 112, 54, 24}, false, Below, 0, nullptr},
        {C::L3, {190, 190, 50, 50}, true, Left, 0, nullptr},     {C::R3, {360, 190, 50, 50}, true, Right, 0, nullptr},
        {C::Y, {466, 96, 30, 30}, true, Above, 0, nullptr},      {C::X, {436, 128, 30, 30}, true, Left, 0, nullptr},
        {C::B, {496, 128, 30, 30}, true, Right, 0, nullptr},     {C::A, {466, 160, 30, 30}, true, Below, 0, nullptr},
        {C::LX, {70, 290, 50, 22}, false, Below, 0, nullptr},    {C::LY, {126, 290, 50, 22}, false, Below, 0, nullptr},
        {C::DpadX, {214, 290, 72, 22}, false, Below, 0, nullptr}, {C::DpadY, {292, 290, 72, 22}, false, Below, 0, nullptr},
        {C::RX, {420, 290, 50, 22}, false, Below, 0, nullptr},   {C::RY, {476, 290, 50, 22}, false, Below, 0, nullptr},
    };

    const GamepadConfig &m_config;
    const GamepadState &m_state;
    std::function<void(PadChannel)> m_clicked;
    PadChannel m_selected = PadChannel::A;
};

static QGroupBox *groupBox(const QString &title, QLayout *layout)
{
    auto *g = new QGroupBox(title);
    g->setLayout(layout);
    return g;
}

GamepadPage::GamepadPage(QWidget *parent) : QWidget(parent)
{
    {
        QSettings s;
        m_config = GamepadConfig::load(s);
    }
    m_status = new QLabel;
    m_name = new QLabel;
    auto *statusRow = new QHBoxLayout;
    statusRow->addWidget(m_status);
    statusRow->addStretch(1);
    statusRow->addWidget(m_name);

    // SET CHANNEL: nothing changes until SET
    m_channel = new QComboBox;
    for (int c = 0; c < kPadChannels; ++c) m_channel->addItem(QLatin1String(padChannelName(PadChannel(c))));
    m_function = new QComboBox;
    QPushButton *set = button(QStringLiteral("SET"), QStringLiteral("Assign the function to the channel. A function already on "
                                                                     "another channel moves here; that channel becomes None."),
                              "primary");
    QPushButton *reset = button(QStringLiteral("Reset default"), QStringLiteral("Default assignment, every INVERT off, every "
                                                                                 "DEADZONE 8 %. Applies at once."));
    auto *setGrid = new QGridLayout;
    setGrid->addWidget(new QLabel(QStringLiteral("CHANNEL")), 0, 0);
    setGrid->addWidget(new QLabel(QStringLiteral("FUNCTION")), 0, 1);
    setGrid->addWidget(m_channel, 1, 0);
    setGrid->addWidget(m_function, 1, 1);
    setGrid->addWidget(reset, 2, 0);
    setGrid->addWidget(set, 2, 1);
    connect(m_channel, &QComboBox::activated, this, [this](int c) { selectChannel(PadChannel(c)); });
    connect(set, &QPushButton::clicked, this, [this] {
        if (m_config.assign(PadChannel(m_channel->currentIndex()), PadFunction(m_function->currentData().toInt()))) configChanged();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_config = GamepadConfig();
        configChanged();
    });

    // JOYSTICK: applies and saves at once
    auto *joy = new QGridLayout;
    for (int side = 0; side < 2; ++side) {
        const int col = side * 4;
        joy->addWidget(new QLabel(side ? QStringLiteral("RIGHT") : QStringLiteral("LEFT")), 0, col);
        joy->addWidget(new QLabel(QStringLiteral("INVERT")), 0, col + 1);
        joy->addWidget(new QLabel(QStringLiteral("DEADZONE")), 0, col + 2);
    }
    for (int i = 0; i < kPadSticks; ++i) {
        const int row = 1 + i % 2, col = (i / 2) * 4;
        m_invert[i] = new QCheckBox;
        m_invert[i]->setToolTip(QStringLiteral("Reverse what this axis sends. ANALOG INPUT keeps showing the real position."));
        m_deadZone[i] = new QSlider(Qt::Horizontal);
        m_deadZone[i]->setRange(0, GamepadConfig::kDeadZoneMax);
        m_deadZone[i]->setToolTip(QStringLiteral("Movement inside this share of the travel around the centre is ignored"));
        m_deadZoneText[i] = new QLabel;
        m_deadZoneText[i]->setMinimumWidth(32);
        joy->addWidget(new QLabel(QLatin1String(padChannelName(PadChannel(int(PadChannel::LX) + i)))), row, col);
        joy->addWidget(m_invert[i], row, col + 1);
        joy->addWidget(m_deadZone[i], row, col + 2);
        joy->addWidget(m_deadZoneText[i], row, col + 3);
        connect(m_invert[i], &QCheckBox::toggled, this, [this, i](bool on) {
            if (m_config.invert[i] == on) return;
            m_config.invert[i] = on;
            configChanged();
        });
        connect(m_deadZone[i], &QSlider::valueChanged, this, [this, i](int v) {
            if (m_config.deadZonePercent[i] == v) return;
            m_config.deadZonePercent[i] = v;
            configChanged();
        });
    }
    joy->setColumnStretch(2, 1);
    joy->setColumnStretch(6, 1);

    m_analog = new GamepadAnalogView(m_config, m_state);
    m_picture = new GamepadPicture(m_config, m_state, [this](PadChannel c) { selectChannel(c); });

    auto *grid = new QGridLayout(this);
    grid->addLayout(statusRow, 0, 0, 1, 2);
    grid->addWidget(groupBox(QStringLiteral("SET CHANNEL"), setGrid), 1, 0);
    grid->addWidget(groupBox(QStringLiteral("JOYSTICK"), joy), 1, 1);
    auto *analogBox = new QVBoxLayout;
    analogBox->addWidget(m_analog);
    auto *pictureBox = new QVBoxLayout;
    pictureBox->addWidget(m_picture);
    grid->addWidget(groupBox(QStringLiteral("ANALOG INPUT"), analogBox), 2, 0);
    grid->addWidget(groupBox(QStringLiteral("CHANNELS"), pictureBox), 2, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 2);
    grid->setRowStretch(2, 1);

    setState({});
    configChanged();
    selectChannel(PadChannel::A);
}

void GamepadPage::selectChannel(PadChannel c)
{
    m_channel->setCurrentIndex(int(c));
    m_function->clear();
    for (int f = 0; f < kPadFunctions; ++f)
        if (padAllowed(c, PadFunction(f))) m_function->addItem(QLatin1String(padFunctionName(PadFunction(f))), f);
    m_function->setCurrentIndex(m_function->findData(int(m_config[c])));
    m_picture->setSelected(c);
}

void GamepadPage::configChanged()
{
    QSettings s;
    m_config.save(s);
    for (int i = 0; i < kPadSticks; ++i) {
        const QSignalBlocker b1(m_invert[i]), b2(m_deadZone[i]);
        m_invert[i]->setChecked(m_config.invert[i]);
        m_deadZone[i]->setValue(m_config.deadZonePercent[i]);
        m_deadZoneText[i]->setText(QStringLiteral("%1 %").arg(m_config.deadZonePercent[i]));
    }
    m_function->setCurrentIndex(m_function->findData(int(m_config[PadChannel(m_channel->currentIndex())])));
    m_picture->update();
    m_analog->update();
}

void GamepadPage::setState(const GamepadState &s)
{
    if (s.connected != m_state.connected || s.name != m_state.name || m_status->text().isEmpty()) {
        m_status->setText(s.connected ? colored(QStringLiteral("● "), Theme::Ok) + QStringLiteral("Gamepad connected")
                                      : colored(QStringLiteral("● "), Theme::Bad) + QStringLiteral("No gamepad found"));
        m_name->setText(s.name.toHtmlEscaped());
    }
    m_state = s;
    if (!isVisible()) return;
    m_picture->update();
    m_analog->update();
}
