#pragma once
// DIAGNOSE > Web settings: the payload's own settings web app (http://<ip>:8000/) inside the app. Built with Qt WebEngine
// when available (HAVE_WEBENGINE); otherwise it offers the system browser. Nothing loads until the tab is first shown.

#include <QUrl>
#include <QWidget>

class QLabel;
class QVBoxLayout;
#ifdef HAVE_WEBENGINE
class QWebEngineView;
#endif

class WebSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit WebSettingsPage(QWidget *parent = nullptr);
    void setPayloadIp(const QString &ip);
    QUrl homeUrl() const;  // invalid when the IP is not usable

protected:
    void showEvent(QShowEvent *e) override;

private:
    void goHome();

    QString m_ip;
    QLabel *m_address;
    QVBoxLayout *m_body;
#ifdef HAVE_WEBENGINE
    QWebEngineView *m_view = nullptr;  // created on first show: Chromium starts only if the tab is used
#endif
};
