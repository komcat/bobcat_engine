// TcpClientFormWidget.cpp
#include "TcpClientFormWidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>

TcpClientFormWidget::TcpClientFormWidget(QWidget* parent)
  : QWidget(parent)
  , m_scrollArea(nullptr)
  , m_formContent(nullptr)
{
  setupUI();
}

void TcpClientFormWidget::setupUI()
{
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Title
  QLabel* title = new QLabel("TCP Clients Configuration");
  title->setStyleSheet("QLabel { color: white; font-size: 20px; font-weight: bold; padding: 10px; }");
  mainLayout->addWidget(title);

  // Scroll area
  m_scrollArea = new QScrollArea();
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #2b2b2b; }");

  m_formContent = new QWidget();
  m_scrollArea->setWidget(m_formContent);

  mainLayout->addWidget(m_scrollArea);
}

void TcpClientFormWidget::clear()
{
  m_fields.clear();

  if (m_formContent->layout()) {
    QLayoutItem* item;
    while ((item = m_formContent->layout()->takeAt(0)) != nullptr) {
      delete item->widget();
      delete item;
    }
    delete m_formContent->layout();
  }
}

void TcpClientFormWidget::addClientGroup(const QJsonObject& client)
{
  QVBoxLayout* formLayout = qobject_cast<QVBoxLayout*>(m_formContent->layout());
  if (!formLayout) return;

  int index = static_cast<int>(m_fields.size());

  QString formStyle =
    "QLineEdit, QSpinBox { background-color: #3a3a3a; color: white; border: 1px solid #555555; "
    "padding: 5px; font-size: 14px; } "
    "QCheckBox { color: white; font-size: 14px; }";

  QGroupBox* groupBox = new QGroupBox(QString("TCP Client #%1").arg(index + 1));
  groupBox->setStyleSheet(
    "QGroupBox { color: white; font-size: 16px; font-weight: bold; "
    "border: 2px solid #555555; border-radius: 5px; margin-top: 10px; padding-top: 10px; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");

  QVBoxLayout* groupLayout = new QVBoxLayout();

  QFormLayout* clientForm = new QFormLayout();
  clientForm->setSpacing(10);
  clientForm->setLabelAlignment(Qt::AlignRight);

  TcpClientFields fields;

  // ID
  fields.id = new QLineEdit(client["id"].toString());
  fields.id->setStyleSheet(formStyle);
  clientForm->addRow(new QLabel("<span style='color: white;'>ID:</span>"), fields.id);

  // Name
  fields.name = new QLineEdit(client["name"].toString());
  fields.name->setStyleSheet(formStyle);
  clientForm->addRow(new QLabel("<span style='color: white;'>Name:</span>"), fields.name);

  // Host
  fields.host = new QLineEdit(client.isEmpty() ? "127.0.0.1" : client["host"].toString());
  fields.host->setStyleSheet(formStyle);
  clientForm->addRow(new QLabel("<span style='color: white;'>Host:</span>"), fields.host);

  // Port
  fields.port = new QSpinBox();
  fields.port->setRange(0, 65535);
  fields.port->setValue(client.isEmpty() ? 8888 : client["port"].toInt());
  fields.port->setStyleSheet(formStyle);
  clientForm->addRow(new QLabel("<span style='color: white;'>Port:</span>"), fields.port);

  // Data Channel
  fields.dataChannel = new QLineEdit(client["data_channel"].toString());
  fields.dataChannel->setStyleSheet(formStyle);
  clientForm->addRow(new QLabel("<span style='color: white;'>Data Channel:</span>"), fields.dataChannel);

  // Auto Connect
  fields.autoConnect = new QCheckBox("Auto Connect");
  fields.autoConnect->setChecked(client.isEmpty() ? true : client["auto_connect"].toBool());
  fields.autoConnect->setStyleSheet("QCheckBox { color: white; }");
  clientForm->addRow("", fields.autoConnect);

  // Auto Reconnect
  fields.autoReconnect = new QCheckBox("Auto Reconnect");
  fields.autoReconnect->setChecked(client.isEmpty() ? true : client["auto_reconnect"].toBool());
  fields.autoReconnect->setStyleSheet("QCheckBox { color: white; }");
  clientForm->addRow("", fields.autoReconnect);

  // Description
  fields.description = new QLineEdit(client["description"].toString());
  fields.description->setStyleSheet(formStyle);
  clientForm->addRow(new QLabel("<span style='color: white;'>Description:</span>"), fields.description);

  groupLayout->addLayout(clientForm);

  // Remove button
  QPushButton* removeBtn = new QPushButton("🗑 Remove Client");
  removeBtn->setStyleSheet(
    "QPushButton { background-color: #cc0000; color: white; border: none; "
    "padding: 8px; font-size: 12px; } "
    "QPushButton:hover { background-color: #ee0000; }");
  connect(removeBtn, &QPushButton::clicked, this, [this, index]() {
    onRemoveClientClicked(index);
  });
  groupLayout->addWidget(removeBtn);

  groupBox->setLayout(groupLayout);

  m_fields.push_back(fields);

  // Insert before the stretch and add button
  int insertPos = formLayout->count() - 2;  // Before stretch and add button
  formLayout->insertWidget(insertPos, groupBox);
}

void TcpClientFormWidget::rebuildForm()
{
  // Collect current data
  QJsonDocument currentData = collectToJson();

  // Reload form
  loadFromJson(currentData);
}

void TcpClientFormWidget::loadFromJson(const QJsonDocument& config)
{
  clear();

  QVBoxLayout* formLayout = new QVBoxLayout(m_formContent);
  formLayout->setSpacing(20);

  QJsonArray clients = config.object()["tcp_clients"].toArray();

  // Add each client
  for (int i = 0; i < clients.size(); ++i) {
    addClientGroup(clients[i].toObject());
  }

  // Add Client button
  QPushButton* addBtn = new QPushButton("➕ Add TCP Client");
  addBtn->setStyleSheet(
    "QPushButton { background-color: #0066cc; color: white; border: none; "
    "padding: 10px; font-size: 14px; font-weight: bold; } "
    "QPushButton:hover { background-color: #0077ee; }");
  connect(addBtn, &QPushButton::clicked, this, &TcpClientFormWidget::onAddClientClicked);
  formLayout->addWidget(addBtn);

  formLayout->addStretch();
}

void TcpClientFormWidget::onAddClientClicked()
{
  QJsonObject newClient;
  newClient["id"] = QString("client_%1").arg(m_fields.size() + 1);
  newClient["name"] = QString("Client %1").arg(m_fields.size() + 1);
  newClient["host"] = "127.0.0.1";
  newClient["port"] = 8888;
  newClient["data_channel"] = "data";
  newClient["auto_connect"] = true;
  newClient["auto_reconnect"] = true;
  newClient["description"] = "";

  addClientGroup(newClient);
}

void TcpClientFormWidget::onRemoveClientClicked(int index)
{
  if (index < 0 || index >= static_cast<int>(m_fields.size())) return;

  // Must keep at least one client
  if (m_fields.size() <= 1) {
    return;
  }

  // Remove from fields
  m_fields.erase(m_fields.begin() + index);

  // Rebuild the form to update indices
  rebuildForm();
}

QJsonDocument TcpClientFormWidget::collectToJson() const
{
  QJsonArray clientsArray;

  for (const auto& fields : m_fields) {
    QJsonObject client;
    client["id"] = fields.id->text();
    client["name"] = fields.name->text();
    client["host"] = fields.host->text();
    client["port"] = fields.port->value();
    client["data_channel"] = fields.dataChannel->text();
    client["auto_connect"] = fields.autoConnect->isChecked();
    client["auto_reconnect"] = fields.autoReconnect->isChecked();
    client["description"] = fields.description->text();
    clientsArray.append(client);
  }

  QJsonObject root;
  root["tcp_clients"] = clientsArray;
  return QJsonDocument(root);
}