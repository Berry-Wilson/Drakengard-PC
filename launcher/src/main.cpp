#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Drakengard Launcher");

    MainWindow window;
    window.show();
    return app.exec();
}
