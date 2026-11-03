#include "klv_page.h"

#include "klv_receiver.h"
#include "metadata_model.h"
#include "theme.h"
#include "ui_util.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

// --- KlvPanel ---

KlvPanel::KlvPanel(MetadataModel *model, QWidget *parent) : QWidget(parent), m_model(model)
{
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("klvPanelTitle"));
    m_title->setToolTip(QStringLiteral("Latest metadata received in the stream. Tag 2 is the payload's own clock, and freshness is "
                                       "local arrival time. Nothing maps a packet to a frame. This is separate from the MAVLink "
                                       "telemetry drawn on the picture."));

    struct Cell { const char *caption; int tag; };
    // The tags the parser defines. Platform position and target coordinates are not among them.
    const Cell rows[4][4] = {
        {{"Time (UTC)", 2}, {"Mission", 3}, {"Tail", 4}, {"Platform", 10}},
        {{"Heading °", 5}, {"Pitch °", 6}, {"Roll °", 7}, {"Slant m", 21}},
        {{"Sensor lat °", 13}, {"Sensor lon °", 14}, {"Sensor alt m", 15}, {"HFOV / VFOV °", 16}},
        {{"Center lat °", 23}, {"Center lon °", 24}, {"Center elev m", 25}, {nullptr, 17}},
    };
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(0);
    const auto addValue = [&](int tag, int row, int column) {
        auto *value = new QLabel;
        value->setObjectName(QStringLiteral("klv_%1").arg(tag));
        value->setFont(monoFont(9));
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);  // long values must not widen the panel
        grid->addWidget(value, row, column);
        m_values.append({tag, value});
    };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            const Cell &cell = rows[r][c];
            if (!cell.caption) {
                // The fourth row's last cell is VFOV, under HFOV in the row above
                addValue(cell.tag, r, c * 2 + 1);
                continue;
            }
            auto *caption = new QLabel(QString::fromUtf8(cell.caption));
            caption->setObjectName(QStringLiteral("readoutCaption"));
            grid->addWidget(caption, r, c * 2);
            addValue(cell.tag, r, c * 2 + 1);
        }
    for (int c = 1; c < 8; c += 2) grid->setColumnStretch(c, c == 1 ? 3 : 2);  // the first column holds the full timestamp

    auto *l = new QVBoxLayout(this);
    l->setContentsMargins(4, 2, 4, 2);
    l->setSpacing(2);
    l->addWidget(m_title);
    l->addLayout(grid);

    connect(model, &MetadataModel::changed, this, [this] {
        if (isVisible()) refresh();
    });
    refresh();
}

void KlvPanel::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    refresh();
}

void KlvPanel::refresh()
{
    const QString freshness = m_model->freshness();
    const double age = double(m_model->ageMs()) / 1000.0;
    const char *titleColor = freshness == QLatin1String("live") ? Theme::Ok
                           : freshness == QLatin1String("no data") ? Theme::Dim : Theme::Warn;
    QString state = freshness.toUpper();
    if (m_model->ageMs() >= 0 && freshness == QLatin1String("stale")) state += QStringLiteral(" %1 s").arg(age, 0, 'f', 1);
    m_title->setText(QStringLiteral("KLV ") + colored(state, titleColor) + QLatin1Char(' ') +
                     colored(QStringLiteral("latest stream metadata, not synchronised to the picture"), Theme::Dim));

    for (const auto &[tag, label] : m_values) {
        const QVariantMap f = m_model->field(tag);
        const QString status = f.value(QStringLiteral("status")).toString();
        const QString value = f.value(QStringLiteral("value")).toString().toHtmlEscaped();
        if (status == QLatin1String("valid")) label->setText(value);
        else if (status == QLatin1String("stale")) label->setText(colored(QStringLiteral("%1 (stale %2 s)").arg(value).arg(age, 0, 'f', 1), Theme::Dim));
        else if (status == QLatin1String("invalid")) label->setText(colored(QStringLiteral("invalid"), Theme::Bad));
        else label->setText(colored(QStringLiteral("no data"), Theme::Dim));
    }
}

// --- KlvSettingsPanel ---

KlvSettingsPanel::KlvSettingsPanel(KlvReceiver *receiver, QWidget *parent) : QWidget(parent), m_receiver(receiver)
{
    const KlvReceiver::Config c = receiver->config();
    m_address = new QLineEdit(c.address);
    m_address->setObjectName(QStringLiteral("klvAddress"));
    m_address->setToolTip(QStringLiteral("IPv4 address to listen on, multicast group or local address. The default comes from the KLV "
                                         "monitor, as seen on ViO beta firmware: check your payload's configuration."));
    m_port = new QSpinBox;
    m_port->setObjectName(QStringLiteral("klvPort"));
    m_port->setRange(0, 65535);
    m_port->setValue(c.port);
    m_iface = new QComboBox;
    m_iface->setObjectName(QStringLiteral("klvInterface"));
    m_iface->setToolTip(QStringLiteral("Where the multicast join goes. Pick the payload's interface if another one holds the default route."));
    m_iface->addItem(QStringLiteral("Kernel chooses"), QString());
    for (const QString &name : KlvReceiver::interfaces()) m_iface->addItem(name, name);
    m_iface->setCurrentIndex(qMax(0, m_iface->findData(c.iface)));
    m_media = new QLineEdit(c.media);
    m_media->setObjectName(QStringLiteral("klvMedia"));
    m_media->setMaximumWidth(60);
    m_encoding = new QLineEdit(c.encoding);
    m_encoding->setObjectName(QStringLiteral("klvEncoding"));
    m_encoding->setMaximumWidth(60);
    m_clockRate = new QSpinBox;
    m_clockRate->setObjectName(QStringLiteral("klvClockRate"));
    m_clockRate->setRange(0, 1000000);
    m_clockRate->setValue(c.clockRate);
    auto *applyBtn = button(QStringLiteral("Apply"), QStringLiteral("Check and save these settings. Restarts the stream if it is running."));
    applyBtn->setObjectName(QStringLiteral("klvApply"));
    m_message = new QLabel;
    m_message->setObjectName(QStringLiteral("klvSettingsMessage"));
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *bar = new QHBoxLayout(this);
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(new QLabel(QStringLiteral("Address")));
    bar->addWidget(m_address, 1);
    bar->addWidget(new QLabel(QStringLiteral("Port")));
    bar->addWidget(m_port);
    bar->addWidget(m_iface);
    bar->addWidget(new QLabel(QStringLiteral("RTP")));
    bar->addWidget(m_media);
    bar->addWidget(m_encoding);
    bar->addWidget(m_clockRate);
    bar->addWidget(applyBtn);
    bar->addWidget(m_message);

    connect(applyBtn, &QPushButton::clicked, this, &KlvSettingsPanel::apply);
}

void KlvSettingsPanel::apply()
{
    KlvReceiver::Config c;
    c.address = m_address->text().trimmed();
    c.port = m_port->value();
    c.iface = m_iface->currentData().toString();
    c.media = m_media->text().trimmed();
    c.encoding = m_encoding->text().trimmed();
    c.clockRate = m_clockRate->value();
    const QString err = m_receiver->apply(c);
    m_message->setText(err.isEmpty() ? colored(QStringLiteral("Saved"), Theme::Ok) : colored(err.toHtmlEscaped(), Theme::Bad));
}

// --- KlvPage ---

static QTableWidget *makeTable(const char *name, const QStringList &headers)
{
    auto *t = new QTableWidget(0, int(headers.size()));
    t->setObjectName(QLatin1String(name));
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(t->fontMetrics().height() + 4);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

static void setCell(QTableWidget *t, int row, int column, const QString &text, const char *color = nullptr)
{
    QTableWidgetItem *item = t->item(row, column);
    if (!item) {
        item = new QTableWidgetItem;
        t->setItem(row, column, item);
    }
    if (item->text() != text) item->setText(text);  // keeps selection and scrolling steady at 4 Hz
    item->setForeground(QColor(color ? color : Theme::Text));
}

static const char *statusColor(const QString &status)
{
    if (status == QLatin1String("valid")) return Theme::Ok;
    if (status == QLatin1String("stale")) return Theme::Warn;
    if (status == QLatin1String("invalid")) return Theme::Bad;
    return Theme::Dim;
}

KlvPage::KlvPage(KlvReceiver *receiver, MetadataModel *model, QWidget *parent) : QWidget(parent), m_receiver(receiver), m_model(model)
{
    m_counters = new QLabel;
    m_counters->setObjectName(QStringLiteral("klvCounters"));
    m_counters->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_counters->setWordWrap(true);
    m_hold = new QCheckBox(QStringLiteral("Hold packets"));
    m_errorsOnly = new QCheckBox(QStringLiteral("Errors only"));
    auto *json = button(QStringLiteral("Export JSON…"));
    auto *csv = button(QStringLiteral("Export CSV…"));
    m_message = new QLabel;
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_tags = makeTable("klvTags", {QStringLiteral("Tag"), QStringLiteral("Name"), QStringLiteral("Raw"), QStringLiteral("Value"),
                                   QStringLiteral("Unit"), QStringLiteral("Updated"), QStringLiteral("Status"), QStringLiteral("Error")});
    m_packets = makeTable("klvPackets", {QStringLiteral("Time"), QStringLiteral("Bytes"), QStringLiteral("Result"),
                                         QStringLiteral("Errors"), QStringLiteral("Hex")});
    m_pids = makeTable("klvPids", {QStringLiteral("PID"), QStringLiteral("Packets"), QStringLiteral("/s"), QStringLiteral("Pad"), QStringLiteral("Content")});
    int column = 0;
    for (int width : {40, 190, 110, 190, 40, 90, 60}) m_tags->setColumnWidth(column++, width);
    column = 0;
    for (int width : {90, 50, 70, 150}) m_packets->setColumnWidth(column++, width);
    column = 0;
    for (int width : {60, 70, 40, 110}) m_pids->setColumnWidth(column++, width);
    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("klvLog"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(KlvReceiver::kMaxLog);

    auto *top = new QHBoxLayout;
    top->addWidget(m_counters, 1);
    top->addWidget(m_message);
    top->addWidget(m_hold);
    top->addWidget(m_errorsOnly);
    top->addWidget(json);
    top->addWidget(csv);
    auto *right = new QSplitter(Qt::Vertical);
    right->addWidget(m_packets);
    right->addWidget(m_pids);
    right->addWidget(m_log);
    auto *split = new QSplitter;
    split->addWidget(m_tags);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    auto *l = new QVBoxLayout(this);
    l->addLayout(top);
    l->addWidget(split, 1);

    connect(json, &QPushButton::clicked, this, [this] {
        exportTo(QStringLiteral("JSON (*.json)"), QStringLiteral("klv_metadata.json"), m_model->toJson());
    });
    connect(csv, &QPushButton::clicked, this, [this] {
        exportTo(QStringLiteral("CSV (*.csv)"), QStringLiteral("klv_metadata.csv"), m_model->toCsv());
    });
    auto *t = new QTimer(this);
    connect(t, &QTimer::timeout, this, [this] {
        if (isVisible()) refresh();
    });
    t->start(250);
}

void KlvPage::exportTo(const QString &filter, const QString &name, const QByteArray &data)
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export KLV metadata"), QDir::home().filePath(name), filter);
    if (path.isEmpty()) return;
    QSaveFile f(path);
    const bool ok = f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
    m_message->setText(ok ? colored(QStringLiteral("Saved"), Theme::Ok) : colored(f.errorString().toHtmlEscaped(), Theme::Bad));
}

void KlvPage::refresh()
{
    const KlvReceiver::State state = m_receiver->state();
    m_counters->setText(QStringLiteral("<b>%1</b> · %2 · UDP %3 pkt/s, %4 kbit/s · video %5 fps, %6 superseded · KLV %7/s<br>"
                                       "buffers %8 · decoded %9 · malformed %10 · field errors %11 · duplicates %12 · delayed %13 · "
                                       "queue drops %14 · tags dropped %15 · expected tags not seen %16 · metadata %17")
                            .arg(KlvReceiver::stateText(state).toHtmlEscaped(), m_receiver->endpoint().toHtmlEscaped())
                            .arg(m_receiver->udpRate(), 0, 'f', 0).arg(m_receiver->udpKbps(), 0, 'f', 0)
                            .arg(m_receiver->videoFps(), 0, 'f', 0).arg(m_receiver->framesSuperseded())
                            .arg(m_receiver->klvRate(), 0, 'f', 0)
                            .arg(m_model->buffers()).arg(m_model->decoded()).arg(m_model->malformed()).arg(m_model->fieldErrors())
                            .arg(m_model->duplicates()).arg(m_model->delayed()).arg(m_receiver->klvDropped()).arg(m_model->droppedTags())
                            .arg(m_model->missingExpected().size()).arg(m_model->freshness().toHtmlEscaped()));
    QStringList tip;
    if (const QString err = m_receiver->lastError(); !err.isEmpty()) tip << err;
    if (const QString err = m_model->lastError(); !err.isEmpty()) tip << QStringLiteral("Last malformed packet: ") + err;
    tip << QStringLiteral("Expected tags not seen: ") + m_model->missingExpected().join(QStringLiteral(", "));
    m_counters->setToolTip(tip.join(QLatin1Char('\n')));

    m_tags->setRowCount(m_model->rowCount());
    for (int i = 0; i < m_model->rowCount(); ++i) {
        const QVariantMap f = m_model->fieldAt(i);
        const QString status = f.value(QStringLiteral("status")).toString();
        setCell(m_tags, i, 0, f.value(QStringLiteral("tag")).toString());
        setCell(m_tags, i, 1, f.value(QStringLiteral("name")).toString());
        setCell(m_tags, i, 2, f.value(QStringLiteral("raw")).toString());
        // A value that is not current is not presented as one; the raw column still shows what arrived
        setCell(m_tags, i, 3, status == QLatin1String("invalid") ? QStringLiteral("(last valid) ") + f.value(QStringLiteral("value")).toString()
                                                                 : f.value(QStringLiteral("value")).toString(),
                status == QLatin1String("valid") ? nullptr : Theme::Dim);
        setCell(m_tags, i, 4, f.value(QStringLiteral("unit")).toString());
        setCell(m_tags, i, 5, f.value(QStringLiteral("updated")).toString());
        setCell(m_tags, i, 6, status, statusColor(status));
        setCell(m_tags, i, 7, f.value(QStringLiteral("error")).toString());
    }

    if (!m_hold->isChecked()) {
        const QVariantList packets = m_model->recentPackets(m_errorsOnly->isChecked());
        m_packets->setRowCount(int(packets.size()));
        for (int i = 0; i < packets.size(); ++i) {
            const QVariantMap p = packets[i].toMap();
            const bool ok = p.value(QStringLiteral("ok")).toBool();
            const QString note = p.value(QStringLiteral("note")).toString();
            QStringList errors = p.value(QStringLiteral("errors")).toStringList();
            errors += p.value(QStringLiteral("fieldErrors")).toStringList();
            setCell(m_packets, i, 0, p.value(QStringLiteral("time")).toString());
            setCell(m_packets, i, 1, p.value(QStringLiteral("size")).toString());
            setCell(m_packets, i, 2, !ok ? QStringLiteral("malformed") : !note.isEmpty() ? note : QStringLiteral("ok"),
                    !ok ? Theme::Bad : !note.isEmpty() ? Theme::Warn : Theme::Ok);
            setCell(m_packets, i, 3, errors.join(QStringLiteral("; ")));
            setCell(m_packets, i, 4, p.value(QStringLiteral("hex")).toString());
        }
    }

    const std::vector<KlvReceiver::PidRow> pids = m_receiver->pids();
    m_pids->setRowCount(int(pids.size()));
    for (int i = 0; i < int(pids.size()); ++i) {
        const KlvReceiver::PidRow &p = pids[size_t(i)];
        setCell(m_pids, i, 0, QStringLiteral("0x%1").arg(p.pid, 4, 16, QLatin1Char('0')));
        setCell(m_pids, i, 1, QString::number(p.packets));
        setCell(m_pids, i, 2, QString::number(p.rate));
        setCell(m_pids, i, 3, p.pad);
        setCell(m_pids, i, 4, p.label);
    }

    if (const QStringList log = m_receiver->log(); log != m_shownLog) {
        m_shownLog = log;
        m_log->setPlainText(log.join(QLatin1Char('\n')));
    }
}
