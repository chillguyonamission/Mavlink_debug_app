#pragma once
// The KLV stream's widgets: the compact panel under the video, its settings, and the Analyze tab.

#include <QWidget>

class KlvReceiver;
class MetadataModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Latest stream metadata under the video, on the model's refresh and only while visible.
class KlvPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KlvPanel(MetadataModel *model, QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void refresh();

    MetadataModel *m_model;
    QLabel *m_title;
    QList<QPair<int, QLabel *>> m_values;
};

// Address, port, interface and RTP caps. May be moved elsewhere, like VideoPage's RTSP settings.
class KlvSettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KlvSettingsPanel(KlvReceiver *receiver, QWidget *parent = nullptr);

private:
    void apply();

    KlvReceiver *m_receiver;
    QLineEdit *m_address, *m_media, *m_encoding;
    QSpinBox *m_port, *m_clockRate;
    QComboBox *m_iface;
    QLabel *m_message;
};

// Analyze › KLV: every tag with raw value and validity, packet history, PIDs and pads, counters, log and export.
class KlvPage : public QWidget
{
    Q_OBJECT
public:
    KlvPage(KlvReceiver *receiver, MetadataModel *model, QWidget *parent = nullptr);

private:
    void refresh();  // 4 Hz, only while visible
    void exportTo(const QString &filter, const QString &name, const QByteArray &data);

    KlvReceiver *m_receiver;
    MetadataModel *m_model;
    QLabel *m_counters, *m_message;
    QTableWidget *m_tags, *m_packets, *m_pids;
    QPlainTextEdit *m_log;
    QCheckBox *m_hold, *m_errorsOnly;
    QStringList m_shownLog;
};
