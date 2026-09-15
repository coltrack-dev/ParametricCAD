#include <QApplication>
#include <QByteArray>

#include "viewer/MainWindow.h"

int main(int argc, char* argv[])
{
#if defined(Q_OS_LINUX)
    // OCCT 7.6 Xw_Window requires an X11/XCB native window.
    // Under Wayland QWidget::winId() is not an X11 Window ID.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
    }
#endif

    QApplication application(argc, argv);

    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
