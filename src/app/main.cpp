#include <QApplication>
#include "viewer/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("ParametricCAD");
    QApplication::setOrganizationName("ParametricCAD");

    MainWindow mainWindow;
    mainWindow.resize(1280, 800);
    mainWindow.show();

    return application.exec();
}
