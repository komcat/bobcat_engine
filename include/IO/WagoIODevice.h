// WagoIODevice.h - WAGO 750-series IO device implementation via Modbus TCP
#pragma once

#include "IEziIODevice.h"
#include "EziIO_Types.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <memory>

// Forward declaration for Modbus client
namespace modbus {
  class ModbusTcpClient;
}

/**
 * @brief WAGO 750-series IO device implementation
 *
 * Handles communication with WAGO 750-xxx fieldbus couplers via Modbus TCP.
 * Implements the IEziIODevice interface for compatibility with EziIOManager.
 *
 * Supported devices: WAGO 750-342, 750-352, 750-841, etc.
 * Protocol: Modbus TCP on port 502
 *
 * Modbus Address Mapping for WAGO 750:
 * - Digital Outputs (Coils):     0x0000+ (Function codes 0x01, 0x05, 0x0F)
 * - Digital Inputs (Discrete):   0x0000+ (Function code 0x02)
 * - Analog Outputs (Holding):    0x0000+ (Function codes 0x03, 0x06, 0x10)
 * - Analog Inputs (Input Reg):   0x0000+ (Function code 0x04)
 */
class WagoIODevice : public IEziIODevice {
public:
  WagoIODevice(const EziIODeviceConfig& config);
  virtual ~WagoIODevice();

  // ============================================================================
  // CONNECTION MANAGEMENT
  // ============================================================================
  bool Connect() override;
  bool Disconnect() override;
  bool IsConnected() const override { return m_isConnected; }

  // ============================================================================
  // INPUT OPERATIONS
  // ============================================================================
  EziIOError ReadInputs(uint32_t& inputs, uint32_t& latch) override;
  EziIOError GetLastInputStatus(uint32_t& inputs, uint32_t& latch) override;
  EziIOError ClearLatch(uint32_t latchMask) override;

  // ============================================================================
  // OUTPUT OPERATIONS
  // ============================================================================
  EziIOError ReadOutputs(uint32_t& outputs, uint32_t& status) override;
  EziIOError GetLastOutputStatus(uint32_t& outputs, uint32_t& status) override;
  EziIOError SetOutputs(uint32_t setMask, uint32_t clearMask) override;
  EziIOError SetOutput(int pin, bool state) override;

  // ============================================================================
  // DEVICE INFO
  // ============================================================================
  int GetDeviceId() const override { return m_config.DeviceId; }
  std::string GetName() const override { return m_config.Name; }
  std::string GetIpAddress() const override { return m_config.IpAddress; }
  int GetInputCount() const override { return m_config.InputCount; }
  int GetOutputCount() const override { return m_config.OutputCount; }

  // ============================================================================
  // PIN NAMING
  // ============================================================================
  void MapInputPinName(const std::string& pinName, int pinNumber) override;
  void MapOutputPinName(const std::string& pinName, int pinNumber) override;
  int GetInputPinByName(const std::string& pinName) const override;
  int GetOutputPinByName(const std::string& pinName) const override;

  // Reverse lookup
  std::string GetInputPinName(int pinNumber) const override;
  std::string GetOutputPinName(int pinNumber) const override;

  // Get all configured pin names (for UI dropdowns)
  std::vector<std::string> GetAllInputPinNames() const override;
  std::vector<std::string> GetAllOutputPinNames() const override;

private:
  // Configuration
  EziIODeviceConfig m_config;

  // Connection state
  bool m_isConnected;
  int m_socket;
  static constexpr int MODBUS_PORT = 502;
  static constexpr int MODBUS_TIMEOUT_MS = 3000;

  // WAGO 750-series Process Image addresses
  // Write outputs: 0x0000 (0)
  // Read output status: 0x0200 (512) - Process Image Input area
  static constexpr uint16_t WAGO_OUTPUT_WRITE_ADDR = 0x0000;
  static constexpr uint16_t WAGO_OUTPUT_READ_ADDR = 0x0200;  // 512

  // Modbus protocol
  uint16_t m_transactionId;
  std::mutex m_socketMutex;

  // Status tracking
  std::mutex m_statusMutex;
  IOStatus m_lastStatus;
  uint32_t m_previousInputs;  // For latch detection

  // Pin name mapping
  std::map<std::string, int> m_inputPinNames;
  std::map<std::string, int> m_outputPinNames;
  std::map<int, std::string> m_inputPinNumbers;
  std::map<int, std::string> m_outputPinNumbers;

  // Current output state (cached for read-modify-write operations)
  uint32_t m_currentOutputState;

  // ============================================================================
  // MODBUS TCP PROTOCOL HELPERS
  // ============================================================================

  // Low-level socket operations
  bool CreateSocket();
  void CloseSocket();
  bool SendRequest(const std::vector<uint8_t>& request);
  bool ReceiveResponse(std::vector<uint8_t>& response, size_t expectedLength);

  // Modbus frame building
  std::vector<uint8_t> BuildReadCoilsRequest(uint16_t startAddr, uint16_t count);
  std::vector<uint8_t> BuildReadDiscreteInputsRequest(uint16_t startAddr, uint16_t count);
  std::vector<uint8_t> BuildWriteSingleCoilRequest(uint16_t addr, bool value);
  std::vector<uint8_t> BuildWriteMultipleCoilsRequest(uint16_t startAddr, uint16_t count, const std::vector<bool>& values);
  std::vector<uint8_t> BuildReadHoldingRegistersRequest(uint16_t startAddr, uint16_t count);
  std::vector<uint8_t> BuildReadInputRegistersRequest(uint16_t startAddr, uint16_t count);

  // Modbus response parsing
  bool ParseReadBitsResponse(const std::vector<uint8_t>& response, uint32_t& bits, int count);
  bool ParseReadRegistersResponse(const std::vector<uint8_t>& response, std::vector<uint16_t>& registers);

  // Helper
  uint32_t GetOutputPinMask(int pin) const;

  // ============================================================================
  // PIN/ADDRESS ADAPTER LAYER
  // ============================================================================
  // Converts between WAGO raw bit positions (0-7) and UI-expected format (8-15)
  // The UI expects: 8-pin devices use bits 8-15, 16-pin devices use bits 16-31

  // Get the bit offset for outputs based on device pin count
  int GetOutputBitOffset() const {
    return (m_config.OutputCount > 8) ? 16 : 8;
  }

  // Get the bit offset for inputs based on device pin count
  int GetInputBitOffset() const {
    return 0;  // Inputs use bits 0-15 directly
  }

  // Convert WAGO raw output bits (0-N) to UI format (offset+0 to offset+N)
  uint32_t WagoToUiBits(uint32_t wagoBits) const {
    return wagoBits << GetOutputBitOffset();
  }

  // Convert UI format bits back to WAGO raw bits
  uint32_t UiToWagoBits(uint32_t uiBits) const {
    return uiBits >> GetOutputBitOffset();
  }

  // Convert UI pin number to WAGO coil address
  int UiPinToWagoAddress(int uiPin) const {
    int offset = GetOutputBitOffset();
    return uiPin - offset;  // UI pin 8 -> WAGO address 0
  }

  // Convert WAGO coil address to UI pin number
  int WagoAddressToUiPin(int wagoAddr) const {
    int offset = GetOutputBitOffset();
    return wagoAddr + offset;  // WAGO address 0 -> UI pin 8
  }
};
