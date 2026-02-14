// Vision Profile Tester - Offline vision profile testing tool
// main.cpp

#include <QApplication>
#include "VisionProfileTesterWindow.h"
#include "DarkTheme.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Vision Profile Tester");
    app.setApplicationVersion("1.0.0");
    app.setStyleSheet(DarkTheme::getStyleSheet());

    VisionProfileTesterWindow window;
    window.show();

    return app.exec();
}
