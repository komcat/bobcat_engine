// ConnectionSplashScreen.h
#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QTimer>

/**
 * @brief Simple splash screen showing hardware connection progress
 *
 * Displays after main UI is shown, updates as hardware connects,
 * auto-closes when all connections complete.
 */
class ConnectionSplashScreen : public QWidget
{
  Q_OBJECT

public:
  explicit ConnectionSplashScreen(QWidget* parent = nullptr);
  ~ConnectionSplashScreen() = default;

public slots:
  void updateStatus(QString component, QString status);
  void setProgress(int percent);
  void onConnectionsComplete();

private:
  void setupUI();

  QLabel* m_titleLabel;
  QLabel* m_statusLabel;
  QProgressBar* m_progressBar;
  QLabel* m_componentListLabel;

  QString m_componentStatuses;
  int m_totalComponents;
  int m_connectedComponents;
};
