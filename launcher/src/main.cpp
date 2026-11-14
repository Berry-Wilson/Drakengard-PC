#include "MainWindow.h"
#include "Settings.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Drakengard Launcher");

    copyOldSettings();
    MainWindow window;
    window.show();
    return app.exec();
}
