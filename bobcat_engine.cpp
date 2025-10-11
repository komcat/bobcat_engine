// bobcat_engine.cpp - Multi-camera main application
#include <QApplication>
#include "MultiCameraView.h"

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);

  // Create and show multi-camera view window
  MultiCameraView* multiCameraView = new MultiCameraView();
  multiCameraView->show();

  return app.exec();
}