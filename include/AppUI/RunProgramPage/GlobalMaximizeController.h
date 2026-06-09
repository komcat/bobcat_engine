// GlobalMaximizeController.h
// Owns a GlobalMaximizeExecutor and wires it to the GlobalMaximizeWidget on
// the Run Program page's "Scanning" tab. Mirrors ScanController: the page keeps
// the widget (a tab body); this controller coordinates
// "user clicks Start -> executor runs the dual-hex routine -> finishes -> result".
#pragma once

#include <QObject>
#include <QString>
#include <functional>

struct ApplicationContext;
class GlobalMaximizeExecutor;
class GlobalMaximizeWidget;
class QWidget;

class GlobalMaximizeController : public QObject {
  Q_OBJECT

public:
  struct Deps {
    QWidget*              dialogParent = nullptr;
    ApplicationContext*   context      = nullptr;
    GlobalMaximizeWidget* widget       = nullptr;
    std::function<void(const QString&)> appendLog;
  };

  explicit GlobalMaximizeController(Deps deps, QObject* parent = nullptr);
  ~GlobalMaximizeController() override;

  GlobalMaximizeExecutor* executor() const { return m_executor; }

private slots:
  void onStartRequested();
  void onStopRequested();
  void onLogMessage(QString message);
  void onCompleted(double peakValue, bool success);
  void onError(QString errorMessage);

private:
  Deps m_deps;
  GlobalMaximizeExecutor* m_executor = nullptr;
};
