#include <QApplication>
#include "CameraLiveView.h"

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);

  // Create and show camera live view window
  CameraLiveView* liveView = new CameraLiveView();
  liveView->show();

  return app.exec();
}