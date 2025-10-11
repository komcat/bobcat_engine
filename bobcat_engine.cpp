#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QDateTime>

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);

  // Create main window
  QWidget window;
  window.setWindowTitle("Bobcat Engine - Qt Version");
  window.resize(500, 400);

  // Create widgets
  QPushButton* button = new QPushButton("Execute Command");
  QTextEdit* logDisplay = new QTextEdit();
  logDisplay->setReadOnly(true);
  logDisplay->append("Application started");

  // Create layout
  QVBoxLayout* layout = new QVBoxLayout();
  layout->addWidget(button);
  layout->addWidget(logDisplay);
  window.setLayout(layout);

  // Connect button click to add log entry
  int clickCount = 0;
  QObject::connect(button, &QPushButton::clicked, [&]() {
    clickCount++;
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString logEntry = QString("[%1] Command executed #%2")
      .arg(timestamp)
      .arg(clickCount);
    logDisplay->append(logEntry);
  });

  window.show();
  return app.exec();
}