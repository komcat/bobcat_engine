// TcpClientManager.h - Manages multiple TCP data clients
#pragma once

#include "TcpDataClient.h"
#include "DataStore.h"
#include <QObject>
#include <memory>
#include <vector>
#include <string>
#include <mutex>

/**
 * @brief Manages multiple TCP data clients
 *
 * - Loads client configurations from JSON
 * - Connects clients to DataStore
 * - Emits signals for real-time updates
 */
class TcpClientManager : public QObject {
  Q_OBJECT

public:
  struct ClientConfig {
    std::string id;
    std::string name;
    std::string host;
    int port;

    // Protocol configuration
    std::string protocolType;  // "single" or "multi"

    // For single-channel mode
    std::string dataStoreChannel;

    // For multi-channel mode
    std::vector<std::string> dataChannels;

    bool autoConnect;
    bool autoReconnect;
    std::string description;

    ClientConfig()
      : port(8888)
      , protocolType("single")
      , autoConnect(false)
      , autoReconnect(false)
    {
    }
  };

  struct ClientStatus {
    std::string id;
    bool connected;
    float latestValue;
    size_t pendingCount;
  };

  explicit TcpClientManager(DataStore* dataStore, QObject* parent = nullptr);
  ~TcpClientManager();

  // Configuration
  bool LoadConfig(const std::string& configPath);
  bool SaveConfig(const std::string& configPath) const;

  // Client operations
  bool AddClient(const ClientConfig& config);
  bool RemoveClient(const std::string& clientId);
  bool ConnectClient(const std::string& clientId);
  void DisconnectClient(const std::string& clientId);
  void ConnectAll();
  void DisconnectAll();
  void ConnectAutoClients();

  // Status queries
  std::vector<std::string> GetClientIds() const;
  bool IsClientConnected(const std::string& clientId) const;
  ClientConfig GetClientConfig(const std::string& clientId) const;
  ClientStatus GetClientStatus(const std::string& clientId) const;
  std::vector<ClientStatus> GetAllClientStatus() const;
  size_t GetClientCount() const;

signals:
  /**
   * @brief Emitted whenever new data arrives from any client
   * @param clientId The client that received the data
   * @param value The received value
   * @param timestamp Milliseconds since epoch
   */
  void dataReceived(const std::string& clientId, float value, qint64 timestamp);

  /**
   * @brief Emitted when client connection status changes
   * @param clientId The client ID
   * @param connected True if connected, false if disconnected
   */
  void connectionChanged(const std::string& clientId, bool connected);

private:
  struct ManagedClient {
    ClientConfig config;
    std::unique_ptr<TcpDataClient> client;

    ManagedClient(const ClientConfig& cfg)
      : config(cfg)
      , client(std::make_unique<TcpDataClient>())
    {
    }
  };

  DataStore* m_dataStore;
  mutable std::recursive_mutex m_clientsMutex;
  std::vector<ManagedClient> m_clients;

  // Single-channel callback
  void OnDataReceived(const std::string& clientId, const std::string& channelId, float value);

  // Multi-channel callback
  void OnMultiChannelDataReceived(const std::string& clientId,
                                   const std::vector<std::string>& channelIds,
                                   size_t channelIndex,
                                   float value,
                                   qint64 timestamp);

  void OnConnectionChanged(const std::string& clientId, bool connected);
  ManagedClient* FindClient(const std::string& clientId);
  const ManagedClient* FindClient(const std::string& clientId) const;
};