#include <QApplication>
#include <QGuiApplication>

#include "AppConfig.h"
#include "IconFactory.h"
#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    // 高 DPI：不做取整，按物理像素精确缩放
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("EdenLauncher"));
    QApplication::setApplicationName(QStringLiteral("EdenLauncher"));
    QApplication::setApplicationDisplayName(QStringLiteral("Eden Launcher"));
    QApplication::setApplicationVersion(QStringLiteral("2.0.0"));
    app.setWindowIcon(IconFactory::icon(QStringLiteral("chip"), Theme::accent(), 64));

    AppConfig::instance().load();
    Theme::apply(app);

    MainWindow window;
    Theme::applyDarkTitleBar(&window);
    window.show();

    return app.exec();
}
