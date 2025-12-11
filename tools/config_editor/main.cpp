// Config Editor - Standalone JSON Configuration File Editor
// main.cpp

#include <QApplication>
#include "ConfigEditorWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Config Editor");
    app.setApplicationVersion("1.0.0");

    ConfigEditorWindow window;
    window.show();

    return app.exec();
}
