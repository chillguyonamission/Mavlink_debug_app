#include "main_window.h"
#include "theme.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);  // Qt WebEngine (Web settings tab) needs it before the app exists
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Gremsy"));
    QApplication::setApplicationName(QStringLiteral("MavlinkDebugApp"));
    // Portable Windows folder (scripts/package-windows.ps1): use the bundled GStreamer plugins, not an installed GStreamer
    const QString gstDir = QApplication::applicationDirPath() + QStringLiteral("/gstreamer-1.0");
    if (QDir(gstDir).exists()) {
        qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", QDir::toNativeSeparators(gstDir).toLocal8Bit());
        qunsetenv("GST_PLUGIN_PATH_1_0");  // an installed GStreamer's extra plugin folders
        qunsetenv("GST_PLUGIN_PATH");
        qputenv("GST_PLUGIN_SCANNER_1_0", QDir::toNativeSeparators(QApplication::applicationDirPath() + QStringLiteral("/gst-plugin-scanner.exe")).toLocal8Bit());
        // Own registry: one shared with another GStreamer version would list plugins this folder does not have
        const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(cache);
        qputenv("GST_REGISTRY_1_0", QDir::toNativeSeparators(cache + QStringLiteral("/gst-registry.bin")).toLocal8Bit());
    }
    applyTheme(app);
    MainWindow w;  // built first, so the splash reports what actually loaded
    BootSplash splash(w.bootChecks());
    QObject::connect(&splash, &BootSplash::finished, &w, &QWidget::show);
    splash.start();
    return app.exec();
}
