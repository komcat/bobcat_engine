// TcpClientWidget.h - Qt UI for TCP client management
#pragma once

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <memory>
#include <vector>

struct ApplicationContext;
class TcpClientManager;

/**
 * @brief Modern Qt widget for TCP client management
 * - Clean flat card design
 * - Real-time status updates
 * - Connect/disconnect controls
 */
class TcpClientWidget : public QWidget {
  Q_OBJECT

public:
  explicit TcpClientWidget(ApplicationContext* context, QWidget* parent = nullptr);
  ~TcpClientWidget();

private slots:
  void onConnectAllClicked();
  void onDisconnectAllClicked();
  void onUpdateStatus();

private:
  void setupUI();
  void createClientCard(const std::string& clientId);
  void updateClientCard(const std::string& clientId);
  QString formatValue(float value) const;

  struct ClientCard {
    std::string clientId;
    QWidget* cardWidget;
    QLabel* nameLabel;
    QLabel* statusLabel;
    QLabel* valueLabel;
    QLabel* channelLabel;
    QPushButton* connectButton;
    QPushButton* disconnectButton;
  };

  ApplicationContext* m_context;
  TcpClientManager* m_tcpManager;

  QPushButton* m_connectAllBtn;
  QPushButton* m_disconnectAllBtn;
  QLabel* m_summaryLabel;
  QVBoxLayout* m_cardsLayout;
  QTimer* m_updateTimer;

  std::vector<ClientCard> m_clientCards;
};