// TcpClientManager.cpp
#include "TcpClientManager.h"
#include <nlohmann/json.hpp>
#include <QDateTime>
#include <fstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

TcpClientManager::TcpClientManager(DataStore* dataStore, QObject* parent)
  : QObject(parent)
  , m_dataStore(dataStore)
{
  if (!m_dataStore) {
    std::cerr << "[TcpClientManager] Error: DataStore cannot be null" << std::endl;
    m_valid = false;
    return;
  }

  std::cout << "[TcpClientManager] Created" << std::endl;
}

TcpClientManager::~TcpClientManager()
{
  DisconnectAll();
  std::cout << "[TcpClientManager] Destroyed" << std::endl;
}

bool TcpClientManager::LoadConfig(const std::string& configPath)
{
  try {
    std::ifstream file(configPath);
    if (!file.is_open()) {
      std::cerr << "[TcpClientManager] Config file not found: " << configPath << std::endl;
      return false;
    }

    json config;
    file >> config;

    std::lock_guard lock(m_clientsMutex);
    m_clients.clear();

    if (!config.contains("tcp_clients") || !config["tcp_clients"].is_array()) {
      std::cerr << "[TcpClientManager] Invalid config format" << std::endl;
      return false;
    }

    for (const auto& clientJson : config["tcp_clients"]) {
      ClientConfig clientConfig;
      clientConfig.id = clientJson.value("id", "");
      clientConfig.name = clientJson.value("name", "TCP Client");
      clientConfig.host = clientJson.value("host", "127.0.0.1");
      clientConfig.port = clientJson.value("port", 8888);
      clientConfig.protocolType = clientJson.value("protocol_type", "single");
      clientConfig.autoConnect = clientJson.value("auto_connect", false);
      clientConfig.autoReconnect = clientJson.value("auto_reconnect", false);
      clientConfig.description = clientJson.value("description", "");

      // Load channel configuration based on protocol type
      if (clientConfig.protocolType == "multi" || clientConfig.protocolType == "raw_csv") {
        if (clientJson.contains("data_channels") && clientJson["data_channels"].is_array()) {
          for (const auto& channel : clientJson["data_channels"]) {
            clientConfig.dataChannels.push_back(channel.get<std::string>());
          }
        }
        else {
          std::cerr << "[TcpClientManager] Multi-channel client missing 'data_channels' array: "
                    << clientConfig.id << std::endl;
        }
      }
      else {
        // Single-channel mode
        clientConfig.dataStoreChannel = clientJson.value("data_channel", clientConfig.id);
      }

      if (clientConfig.id.empty()) {
        std::cerr << "[TcpClientManager] Skipping client with empty ID" << std::endl;
        continue;
      }

      m_clients.emplace_back(clientConfig);

      std::cout << "[TcpClientManager] Added client: " << clientConfig.id
        << " (" << clientConfig.host << ":" << clientConfig.port << ", "
        << clientConfig.protocolType << " mode)" << std::endl;
    }

    std::cout << "[TcpClientManager] Loaded " << m_clients.size() << " clients from config" << std::endl;
    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "[TcpClientManager] Error loading config: " << e.what() << std::endl;
    return false;
  }
}

bool TcpClientManager::SaveConfig(const std::string& configPath) const
{
  try {
    json config;
    json clientsArray = json::array();

    std::lock_guard lock(m_clientsMutex);

    for (const auto& managed : m_clients) {
      json clientJson;
      clientJson["id"] = managed.config.id;
      clientJson["name"] = managed.config.name;
      clientJson["host"] = managed.config.host;
      clientJson["port"] = managed.config.port;
      clientJson["protocol_type"] = managed.config.protocolType;

      // Save channel configuration based on protocol type
      if (managed.config.protocolType == "multi" || managed.config.protocolType == "raw_csv") {
        clientJson["data_channels"] = managed.config.dataChannels;
      }
      else {
        clientJson["data_channel"] = managed.config.dataStoreChannel;
      }

      clientJson["auto_connect"] = managed.config.autoConnect;
      clientJson["auto_reconnect"] = managed.config.autoReconnect;
      clientJson["description"] = managed.config.description;

      clientsArray.push_back(clientJson);
    }

    config["tcp_clients"] = clientsArray;

    std::ofstream file(configPath);
    if (!file.is_open()) {
      return false;
    }

    file << config.dump(2);
    std::cout << "[TcpClientManager] Saved config to: " << configPath << std::endl;
    return true;
  }
  catch (const std::exception& e) {
    std::cerr << "[TcpClientManager] Error saving config: " << e.what() << std::endl;
    return false;
  }
}

bool TcpClientManager::AddClient(const ClientConfig& config)
{
  std::lock_guard lock(m_clientsMutex);

  // Check for duplicate ID
  if (FindClient(config.id)) {
    std::cerr << "[TcpClientManager] Client already exists: " << config.id << std::endl;
    return false;
  }

  m_clients.emplace_back(config);
  std::cout << "[TcpClientManager] Added client: " << config.id << std::endl;
  return true;
}

bool TcpClientManager::RemoveClient(const std::string& clientId)
{
  std::lock_guard lock(m_clientsMutex);

  auto it = std::find_if(m_clients.begin(), m_clients.end(),
    [&clientId](const ManagedClient& mc) { return mc.config.id == clientId; });

  if (it != m_clients.end()) {
    it->client->Disconnect();
    m_clients.erase(it);
    std::cout << "[TcpClientManager] Removed client: " << clientId << std::endl;
    return true;
  }

  return false;
}

bool TcpClientManager::ConnectClient(const std::string& clientId)
{
  std::lock_guard lock(m_clientsMutex);

  auto* managed = FindClient(clientId);
  if (!managed) {
    std::cerr << "[TcpClientManager] Client not found: " << clientId << std::endl;
    return false;
  }

  const auto& config = managed->config;
  auto& client = managed->client;

  // Configure protocol type
  if (config.protocolType == "multi") {
    client->SetProtocolType(TcpProtocolType::MultiChannel);
    client->SetChannelCount(config.dataChannels.size());

    // Set up multi-channel callback with timestamp
    client->SetMultiChannelDataCallback(
      [this, id = clientId, channels = config.dataChannels](size_t channelIndex, float value, qint64 timestamp) {
        OnMultiChannelDataReceived(id, channels, channelIndex, value, timestamp);
      });
  }
  else if (config.protocolType == "raw_csv") {
    client->SetProtocolType(TcpProtocolType::RawCsv);
    client->SetChannelCount(config.dataChannels.size());

    // Set up multi-channel callback with timestamp (same as multi, but different protocol)
    client->SetMultiChannelDataCallback(
      [this, id = clientId, channels = config.dataChannels](size_t channelIndex, float value, qint64 timestamp) {
        OnMultiChannelDataReceived(id, channels, channelIndex, value, timestamp);
      });
  }
  else {
    // Single-channel mode
    client->SetProtocolType(TcpProtocolType::Single);
    client->SetChannelCount(1);

    // Set up single-channel callback
    client->SetDataCallback([this, id = clientId, channelId = config.dataStoreChannel](float value) {
      OnDataReceived(id, channelId, value);
    });
  }

  // Set up connection callback
  client->SetConnectionCallback([this, id = clientId](bool connected) {
    OnConnectionChanged(id, connected);
  });

  // Connect
  bool connected = client->Connect(config.host, config.port);

  if (connected) {
    std::cout << "[TcpClientManager] Connected client: " << clientId
              << " (" << config.protocolType << " mode)" << std::endl;

    // Enable auto-reconnect AFTER successful connection
    if (config.autoReconnect) {
      client->EnableAutoReconnect(true, 5000);
    }

    return true;
  }
  else {
    std::cerr << "[TcpClientManager] Failed to connect client: " << clientId << std::endl;

    // Enable auto-reconnect even on failure (it will retry)
    if (config.autoReconnect) {
      client->EnableAutoReconnect(true, 5000);
    }

    return false;
  }
}

void TcpClientManager::DisconnectClient(const std::string& clientId)
{
  std::lock_guard lock(m_clientsMutex);

  auto* managed = FindClient(clientId);
  if (managed) {
    managed->client->Disconnect();
    std::cout << "[TcpClientManager] Disconnected client: " << clientId << std::endl;
  }
}

void TcpClientManager::ConnectAll()
{
  std::lock_guard lock(m_clientsMutex);

  for (auto& managed : m_clients) {
    if (!managed.client->IsConnected()) {
      ConnectClient(managed.config.id);
    }
  }
}

void TcpClientManager::DisconnectAll()
{
  std::lock_guard lock(m_clientsMutex);

  for (auto& managed : m_clients) {
    managed.client->Disconnect();
  }

  std::cout << "[TcpClientManager] Disconnected all clients" << std::endl;
}

void TcpClientManager::ConnectAutoClients()
{
  std::lock_guard lock(m_clientsMutex);

  for (auto& managed : m_clients) {
    if (managed.config.autoConnect && !managed.client->IsConnected()) {
      ConnectClient(managed.config.id);
    }
  }
}

std::vector<std::string> TcpClientManager::GetClientIds() const
{
  std::lock_guard lock(m_clientsMutex);

  std::vector<std::string> ids;
  ids.reserve(m_clients.size());

  for (const auto& managed : m_clients) {
    ids.push_back(managed.config.id);
  }

  return ids;
}

bool TcpClientManager::IsClientConnected(const std::string& clientId) const
{
  std::lock_guard lock(m_clientsMutex);

  const auto* managed = FindClient(clientId);
  return managed && managed->client->IsConnected();
}

TcpClientManager::ClientConfig TcpClientManager::GetClientConfig(const std::string& clientId) const
{
  std::lock_guard lock(m_clientsMutex);

  const auto* managed = FindClient(clientId);
  if (managed) {
    return managed->config;
  }

  return ClientConfig();
}

TcpClientManager::ClientStatus TcpClientManager::GetClientStatus(const std::string& clientId) const
{
  std::lock_guard lock(m_clientsMutex);

  ClientStatus status;
  status.id = clientId;
  status.connected = false;
  status.latestValue = 0.0f;
  status.pendingCount = 0;

  const auto* managed = FindClient(clientId);
  if (managed) {
    status.connected = managed->client->IsConnected();
    status.latestValue = managed->client->GetLatestValue();
    status.pendingCount = managed->client->GetPendingCount();
  }

  return status;
}

std::vector<TcpClientManager::ClientStatus> TcpClientManager::GetAllClientStatus() const
{
  std::lock_guard lock(m_clientsMutex);

  std::vector<ClientStatus> statuses;
  statuses.reserve(m_clients.size());

  for (const auto& managed : m_clients) {
    ClientStatus status;
    status.id = managed.config.id;
    status.connected = managed.client->IsConnected();
    status.latestValue = managed.client->GetLatestValue();
    status.pendingCount = managed.client->GetPendingCount();
    statuses.push_back(status);
  }

  return statuses;
}

size_t TcpClientManager::GetClientCount() const
{
  std::lock_guard lock(m_clientsMutex);
  return m_clients.size();
}

void TcpClientManager::OnDataReceived(const std::string& clientId, const std::string& channelId, float value)
{
  // Store to DataStore
  if (m_dataStore) {
    m_dataStore->SetValue(channelId, value);
  }

  // Emit signal with timestamp for real-time plotting
  qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
  emit dataReceived(clientId, value, timestamp);
}

void TcpClientManager::OnMultiChannelDataReceived(const std::string& /*clientId*/,
                                                    const std::vector<std::string>& channelIds,
                                                    size_t channelIndex,
                                                    float value,
                                                    qint64 timestamp)
{
  // Validate channel index
  if (channelIndex >= channelIds.size()) {
    std::cerr << "[TcpClientManager] Channel index out of range: " << channelIndex
              << " (max: " << channelIds.size() << ")" << std::endl;
    return;
  }

  const std::string& channelId = channelIds[channelIndex];

  // Store to DataStore
  if (m_dataStore) {
    m_dataStore->SetValue(channelId, value);
  }

  // Emit signal with original timestamp from TcpDataClient
  // IMPORTANT: Emit channelId (not clientId) so LiveChartWidget can filter by channel name
  // All channels from one message share the same timestamp for proper time-based plotting
  emit dataReceived(channelId, value, timestamp);
}

void TcpClientManager::OnConnectionChanged(const std::string& clientId, bool connected)
{
  std::cout << "[TcpClientManager] Client " << clientId
    << (connected ? " connected" : " disconnected") << std::endl;

  // Emit signal for UI updates
  emit connectionChanged(clientId, connected);
}

TcpClientManager::ManagedClient* TcpClientManager::FindClient(const std::string& clientId)
{
  auto it = std::find_if(m_clients.begin(), m_clients.end(),
    [&clientId](const ManagedClient& mc) { return mc.config.id == clientId; });

  return (it != m_clients.end()) ? &(*it) : nullptr;
}

const TcpClientManager::ManagedClient* TcpClientManager::FindClient(const std::string& clientId) const
{
  auto it = std::find_if(m_clients.begin(), m_clients.end(),
    [&clientId](const ManagedClient& mc) { return mc.config.id == clientId; });

  return (it != m_clients.end()) ? &(*it) : nullptr;
}