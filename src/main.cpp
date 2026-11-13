#include "main_window.h"
#include "theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);  // Qt WebEngine (Web settings tab) needs it before the app exists
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Gremsy"));
    QApplication::setApplicationName(QStringLiteral("MavlinkDebugApp"));
    applyTheme(app);
    MainWindow w;  // built first, so the splash reports what actually loaded
    BootSplash splash(w.bootChecks());
    QObject::connect(&splash, &BootSplash::finished, &w, &QWidget::show);
    splash.start();
    return app.exec();
}
