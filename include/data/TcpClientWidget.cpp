// TcpClientWidget.cpp
#include "TcpClientWidget.h"
#include "ApplicationContext.h"
#include "TcpClientManager.h"
#include <QScrollArea>
#include <iostream>

TcpClientWidget::TcpClientWidget(ApplicationContext* context, QWidget* parent)
  : QWidget(parent)
  , m_context(context)
  , m_tcpManager(context->tcpClientManager)
{
  setupUI();

  // Auto-refresh timer (every 500ms)
  m_updateTimer = new QTimer(this);
  connect(m_updateTimer, &QTimer::timeout, this, &TcpClientWidget::onUpdateStatus);
  m_updateTimer->start(500);

  std::cout << "[TcpClientWidget] Created" << std::endl;
}

TcpClientWidget::~TcpClientWidget()
{
  if (m_updateTimer) {
    m_updateTimer->stop();
  }
}

void TcpClientWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(15);

  // Header with dark background
  QWidget* headerWidget = new QWidget();
  headerWidget->setStyleSheet("QWidget { background-color: #1e1e1e; border-radius: 8px; }");
  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(20, 15, 20, 15);

  QLabel* titleLabel = new QLabel("TCP Data Clients");
  titleLabel->setStyleSheet("QLabel { font-size: 28px; font-weight: bold; color: #ffffff; background: transparent; }");
  headerLayout->addWidget(titleLabel);

  m_summaryLabel = new QLabel("Clients: 0 | Connected: 0");
  m_summaryLabel->setStyleSheet("QLabel { color: #aaaaaa; font-size: 14px; background: transparent; }");
  headerLayout->addWidget(m_summaryLabel);

  mainLayout->addWidget(headerWidget);

  // Control buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setSpacing(10);

  m_connectAllBtn = new QPushButton("Connect All");
  m_connectAllBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #28a745; "
    "  color: white; "
    "  padding: 12px 24px; "
    "  font-size: 14px; "
    "  font-weight: 600; "
    "  border: none; "
    "  border-radius: 6px; "
    "} "
    "QPushButton:hover { background-color: #2dbd4e; } "
    "QPushButton:pressed { background-color: #228b3c; }");
  connect(m_connectAllBtn, &QPushButton::clicked, this, &TcpClientWidget::onConnectAllClicked);
  buttonLayout->addWidget(m_connectAllBtn);

  m_disconnectAllBtn = new QPushButton("Disconnect All");
  m_disconnectAllBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #dc3545; "
    "  color: white; "
    "  padding: 12px 24px; "
    "  font-size: 14px; "
    "  font-weight: 600; "
    "  border: none; "
    "  border-radius: 6px; "
    "} "
    "QPushButton:hover { background-color: #e54454; } "
    "QPushButton:pressed { background-color: #c82333; }");
  connect(m_disconnectAllBtn, &QPushButton::clicked, this, &TcpClientWidget::onDisconnectAllClicked);
  buttonLayout->addWidget(m_disconnectAllBtn);

  buttonLayout->addStretch();
  mainLayout->addLayout(buttonLayout);

  // Scrollable area for client cards
  QScrollArea* scrollArea = new QScrollArea();
  scrollArea->setWidgetResizable(true);
  scrollArea->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");

  QWidget* scrollContent = new QWidget();
  m_cardsLayout = new QVBoxLayout(scrollContent);
  m_cardsLayout->setSpacing(12);
  m_cardsLayout->setContentsMargins(0, 0, 0, 0);
  m_cardsLayout->addStretch();

  scrollArea->setWidget(scrollContent);
  mainLayout->addWidget(scrollArea);

  // Create cards for existing clients
  if (m_tcpManager) {
    auto clientIds = m_tcpManager->GetClientIds();
    for (const auto& id : clientIds) {
      createClientCard(id);
    }
  }
}

void TcpClientWidget::createClientCard(const std::string& clientId)
{
  auto config = m_tcpManager->GetClientConfig(clientId);

  // Modern flat card
  QWidget* card = new QWidget();
  card->setStyleSheet(
    "QWidget { "
    "  background-color: #2d2d2d; "
    "  border: 1px solid #404040; "
    "  border-radius: 8px; "
    "}");

  QVBoxLayout* cardLayout = new QVBoxLayout(card);
  cardLayout->setContentsMargins(20, 16, 20, 16);
  cardLayout->setSpacing(12);

  // Header row: Name and Status
  QHBoxLayout* headerRow = new QHBoxLayout();

  QLabel* nameLabel = new QLabel(QString::fromStdString(config.name));
  nameLabel->setStyleSheet("QLabel { color: #ffffff; font-size: 20px; font-weight: bold; background: transparent; }");
  headerRow->addWidget(nameLabel);

  headerRow->addStretch();

  QLabel* statusLabel = new QLabel("● Disconnected");
  statusLabel->setStyleSheet("QLabel { color: #ff4444; font-weight: 600; font-size: 14px; background: transparent; }");
  headerRow->addWidget(statusLabel);

  cardLayout->addLayout(headerRow);

  // Info row: Address and Channel
  QHBoxLayout* infoRow = new QHBoxLayout();

  QLabel* addressLabel = new QLabel(QString::fromStdString(config.host + ":" + std::to_string(config.port)));
  addressLabel->setStyleSheet("QLabel { color: #999999; font-size: 13px; background: transparent; }");
  infoRow->addWidget(addressLabel);

  infoRow->addSpacing(20);

  QLabel* channelLabel = new QLabel(QString("Channel: ") + QString::fromStdString(config.dataStoreChannel));
  channelLabel->setStyleSheet("QLabel { color: #999999; font-size: 13px; background: transparent; }");
  infoRow->addWidget(channelLabel);

  infoRow->addStretch();

  cardLayout->addLayout(infoRow);

  // Value display
  QLabel* valueLabel = new QLabel("Value: --");
  valueLabel->setStyleSheet(
    "QLabel { "
    "  color: #00d9ff; "
    "  font-size: 24px; "
    "  font-weight: 600; "
    "  padding: 12px; "
    "  background-color: #1e1e1e; "
    "  border-radius: 6px; "
    "}");
  cardLayout->addWidget(valueLabel);

  // Buttons
  QHBoxLayout* btnLayout = new QHBoxLayout();
  btnLayout->setSpacing(10);

  QPushButton* connectBtn = new QPushButton("Connect");
  connectBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #28a745; "
    "  color: white; "
    "  padding: 10px 20px; "
    "  font-size: 13px; "
    "  font-weight: 600; "
    "  border: none; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { background-color: #2dbd4e; } "
    "QPushButton:disabled { background-color: #3a3a3a; color: #666666; }");
  connect(connectBtn, &QPushButton::clicked, [this, clientId]() {
    m_tcpManager->ConnectClient(clientId);
  });

  QPushButton* disconnectBtn = new QPushButton("Disconnect");
  disconnectBtn->setStyleSheet(
    "QPushButton { "
    "  background-color: #dc3545; "
    "  color: white; "
    "  padding: 10px 20px; "
    "  font-size: 13px; "
    "  font-weight: 600; "
    "  border: none; "
    "  border-radius: 5px; "
    "} "
    "QPushButton:hover { background-color: #e54454; } "
    "QPushButton:disabled { background-color: #3a3a3a; color: #666666; }");
  disconnectBtn->setEnabled(false);
  connect(disconnectBtn, &QPushButton::clicked, [this, clientId]() {
    m_tcpManager->DisconnectClient(clientId);
  });

  btnLayout->addWidget(connectBtn);
  btnLayout->addWidget(disconnectBtn);
  btnLayout->addStretch();
  cardLayout->addLayout(btnLayout);

  // Add card to layout (before the stretch)
  int count = m_cardsLayout->count();
  m_cardsLayout->insertWidget(count - 1, card);

  // Store card reference
  ClientCard clientCard;
  clientCard.clientId = clientId;
  clientCard.cardWidget = card;
  clientCard.nameLabel = nameLabel;
  clientCard.statusLabel = statusLabel;
  clientCard.valueLabel = valueLabel;
  clientCard.channelLabel = channelLabel;
  clientCard.connectButton = connectBtn;
  clientCard.disconnectButton = disconnectBtn;
  m_clientCards.push_back(clientCard);
}

void TcpClientWidget::updateClientCard(const std::string& clientId)
{
  auto it = std::find_if(m_clientCards.begin(), m_clientCards.end(),
    [&clientId](const ClientCard& card) { return card.clientId == clientId; });

  if (it == m_clientCards.end()) return;

  auto status = m_tcpManager->GetClientStatus(clientId);

  // Update status label
  if (status.connected) {
    it->statusLabel->setText("● Connected");
    it->statusLabel->setStyleSheet("QLabel { color: #28a745; font-weight: 600; font-size: 14px; background: transparent; }");
    it->connectButton->setEnabled(false);
    it->disconnectButton->setEnabled(true);
  }
  else {
    it->statusLabel->setText("● Disconnected");
    it->statusLabel->setStyleSheet("QLabel { color: #ff4444; font-weight: 600; font-size: 14px; background: transparent; }");
    it->connectButton->setEnabled(true);
    it->disconnectButton->setEnabled(false);
  }

  // Update value
  if (status.connected) {
    it->valueLabel->setText(QString("Value: ") + formatValue(status.latestValue));
  }
  else {
    it->valueLabel->setText("Value: --");
  }
}

void TcpClientWidget::onConnectAllClicked()
{
  std::cout << "[TcpClientWidget] Connecting all clients..." << std::endl;

  m_connectAllBtn->setEnabled(false);
  m_connectAllBtn->setText("Connecting...");

  std::thread([this]() {
    m_tcpManager->ConnectAll();

    QMetaObject::invokeMethod(this, [this]() {
      m_connectAllBtn->setEnabled(true);
      m_connectAllBtn->setText("Connect All");
      std::cout << "[TcpClientWidget] Connection attempt complete" << std::endl;
    });
  }).detach();
}

void TcpClientWidget::onDisconnectAllClicked()
{
  std::cout << "[TcpClientWidget] Disconnecting all clients..." << std::endl;

  m_disconnectAllBtn->setEnabled(false);
  m_disconnectAllBtn->setText("Disconnecting...");

  std::thread([this]() {
    m_tcpManager->DisconnectAll();

    QMetaObject::invokeMethod(this, [this]() {
      m_disconnectAllBtn->setEnabled(true);
      m_disconnectAllBtn->setText("Disconnect All");
      std::cout << "[TcpClientWidget] All clients disconnected" << std::endl;
    });
  }).detach();
}

void TcpClientWidget::onUpdateStatus()
{
  if (!m_tcpManager) return;

  for (const auto& card : m_clientCards) {
    updateClientCard(card.clientId);
  }

  auto statuses = m_tcpManager->GetAllClientStatus();
  int connectedCount = std::count_if(statuses.begin(), statuses.end(),
    [](const TcpClientManager::ClientStatus& s) { return s.connected; });

  m_summaryLabel->setText(
    QString("Clients: %1 | Connected: %2")
    .arg(statuses.size())
    .arg(connectedCount));
}

QString TcpClientWidget::formatValue(float value) const
{
  return QString::number(value, 'f', 6);
}