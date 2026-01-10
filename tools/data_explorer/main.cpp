// main.cpp - SIPHOG Data Explorer
// Standalone utility to browse and filter serial number result data

#include <QApplication>
#include "DataExplorerWindow.h"
#include <iostream>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("SIPHOG Data Explorer");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Bobcat Engine");

    std::cout << "======================================" << std::endl;
    std::cout << "  SIPHOG Data Explorer v1.0.0" << std::endl;
    std::cout << "  Browse serial number result data" << std::endl;
    std::cout << "======================================" << std::endl;

    DataExplorerWindow window;
    window.show();

    return app.exec();
}
