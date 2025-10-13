// TcpClientManager.h - Manages multiple TCP data clients
#pragma once

#include "TcpDataClient.h"
#include "DataStore.h"
#include <memory>
#include <vector>
#include <string>
#include <mutex>

/**
 * @brief Manages multiple TCP data clients
 *
 * - Loads client configurations from JSON
 * - Connects clients to DataStore
 * - No UI dependencies
 */
class TcpClientManager {
public:
  struct ClientConfig {
    std::string id;
    std::string name;
    std::string host;
    int port;
    std::string dataStoreChannel;
    bool autoConnect;
    bool autoReconnect;
    std::string description;

    ClientConfig()
      : port(8888)
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

  explicit TcpClientManager(DataStore* dataStore);
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

  void OnDataReceived(const std::string& channelId, float value);
  void OnConnectionChanged(const std::string& clientId, bool connected);
  ManagedClient* FindClient(const std::string& clientId);
  const ManagedClient* FindClient(const std::string& clientId) const;
};