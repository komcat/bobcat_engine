// WagoIODevice.cpp - WAGO 750-series IO device implementation via Modbus TCP
#include "WagoIODevice.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #define SOCKET_ERROR -1
#endif

// ============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ============================================================================

WagoIODevice::WagoIODevice(const EziIODeviceConfig& config)
  : m_config(config),
    m_isConnected(false),
    m_transactionId(0),
    m_previousInputs(0),
    m_currentOutputState(0)
{
  std::cout << "[WagoIODevice] Created device: " << config.Name
            << " (IP: " << config.IpAddress
            << ", Inputs: " << config.InputCount
            << ", Outputs: " << config.OutputCount << ")" << std::endl;
}

WagoIODevice::~WagoIODevice() {
  Disconnect();
}

// ============================================================================
// CONNECTION MANAGEMENT
// ============================================================================

bool WagoIODevice::Connect() {
  if (m_isConnected) {
    std::cout << "[WagoIODevice] " << m_config.Name << " already connected" << std::endl;
    return true;
  }

  std::cout << "[WagoIODevice] Connecting to " << m_config.Name
            << " at " << m_config.IpAddress << ":" << MODBUS_PORT << std::endl;

  if (!CreateSocket()) {
    std::cerr << "[WagoIODevice] Failed to connect to " << m_config.Name << std::endl;
    return false;
  }

  m_isConnected = true;

  // Read initial output state (ReadOutputs returns UI-formatted bits,
  // but also updates m_currentOutputState with raw WAGO bits internally)
  uint64_t outputs = 0, status = 0;
  ReadOutputs(outputs, status);
  // Note: m_currentOutputState is already set correctly inside ReadOutputs

  std::cout << "[WagoIODevice] Successfully connected to " << m_config.Name << std::endl;
  return true;
}

bool WagoIODevice::Disconnect() {
  if (!m_isConnected) {
    return true;
  }

  std::cout << "[WagoIODevice] Disconnecting from " << m_config.Name << std::endl;

  CloseSocket();
  m_isConnected = false;

  std::cout << "[WagoIODevice] Disconnected from " << m_config.Name << std::endl;
  return true;
}

bool WagoIODevice::CreateSocket() {
  std::lock_guard<std::mutex> lock(m_socketMutex);

#ifdef _WIN32
  // Initialize Winsock
  static bool wsaInitialized = false;
  if (!wsaInitialized) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
      std::cerr << "[WagoIODevice] WSAStartup failed" << std::endl;
      return false;
    }
    wsaInitialized = true;
  }
#endif

  // Create socket
  m_socket.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  if (!m_socket.valid()) {
    std::cerr << "[WagoIODevice] Failed to create socket" << std::endl;
    return false;
  }

  // Set socket timeout
#ifdef _WIN32
  DWORD timeout = MODBUS_TIMEOUT_MS;
  setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
  setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
  struct timeval tv;
  tv.tv_sec = MODBUS_TIMEOUT_MS / 1000;
  tv.tv_usec = (MODBUS_TIMEOUT_MS % 1000) * 1000;
  setsockopt(m_socket.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(m_socket.get(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

  // Connect to WAGO device
  struct sockaddr_in serverAddr;
  memset(&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(MODBUS_PORT);

  if (inet_pton(AF_INET, m_config.IpAddress.c_str(), &serverAddr.sin_addr) <= 0) {
    std::cerr << "[WagoIODevice] Invalid IP address: " << m_config.IpAddress << std::endl;
    m_socket.reset();
    return false;
  }

  if (connect(m_socket.get(), (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
    std::cerr << "[WagoIODevice] Failed to connect to " << m_config.IpAddress << std::endl;
    m_socket.reset();
    return false;
  }

  return true;
}

void WagoIODevice::CloseSocket() {
  std::lock_guard<std::mutex> lock(m_socketMutex);

  if (m_socket.valid()) {
    m_socket.reset();
  }
}

// ============================================================================
// MODBUS TCP COMMUNICATION
// ============================================================================

bool WagoIODevice::SendRequest(const std::vector<uint8_t>& request) {
  if (!m_socket.valid()) {
    return false;
  }

  int sent = send(m_socket.get(), reinterpret_cast<const char*>(request.data()),
                  static_cast<int>(request.size()), 0);
  return sent == static_cast<int>(request.size());
}

bool WagoIODevice::ReceiveResponse(std::vector<uint8_t>& response, size_t expectedLength) {
  if (!m_socket.valid()) {
    return false;
  }

  response.resize(expectedLength);
  int received = recv(m_socket.get(), reinterpret_cast<char*>(response.data()),
                      static_cast<int>(expectedLength), 0);

  if (received <= 0) {
    response.clear();
    return false;
  }

  response.resize(received);
  return true;
}

// ============================================================================
// MODBUS FRAME BUILDING
// ============================================================================

std::vector<uint8_t> WagoIODevice::BuildReadCoilsRequest(uint16_t startAddr, uint16_t count) {
  // Modbus TCP Frame: Transaction ID (2) + Protocol ID (2) + Length (2) + Unit ID (1) + Function (1) + Data
  std::vector<uint8_t> request;
  uint16_t transId = ++m_transactionId;

  // MBAP Header
  request.push_back(transId >> 8);       // Transaction ID high
  request.push_back(transId & 0xFF);     // Transaction ID low
  request.push_back(0x00);               // Protocol ID high
  request.push_back(0x00);               // Protocol ID low
  request.push_back(0x00);               // Length high
  request.push_back(0x06);               // Length low (6 bytes follow)
  request.push_back(0x01);               // Unit ID

  // PDU
  request.push_back(0x01);               // Function code: Read Coils
  request.push_back(startAddr >> 8);     // Start address high
  request.push_back(startAddr & 0xFF);   // Start address low
  request.push_back(count >> 8);         // Quantity high
  request.push_back(count & 0xFF);       // Quantity low

  return request;
}

std::vector<uint8_t> WagoIODevice::BuildReadDiscreteInputsRequest(uint16_t startAddr, uint16_t count) {
  std::vector<uint8_t> request;
  uint16_t transId = ++m_transactionId;

  // MBAP Header
  request.push_back(transId >> 8);
  request.push_back(transId & 0xFF);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x06);
  request.push_back(0x01);               // Unit ID

  // PDU
  request.push_back(0x02);               // Function code: Read Discrete Inputs
  request.push_back(startAddr >> 8);
  request.push_back(startAddr & 0xFF);
  request.push_back(count >> 8);
  request.push_back(count & 0xFF);

  return request;
}

std::vector<uint8_t> WagoIODevice::BuildWriteSingleCoilRequest(uint16_t addr, bool value) {
  std::vector<uint8_t> request;
  uint16_t transId = ++m_transactionId;

  // MBAP Header
  request.push_back(transId >> 8);
  request.push_back(transId & 0xFF);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x06);
  request.push_back(0x01);               // Unit ID

  // PDU
  request.push_back(0x05);               // Function code: Write Single Coil
  request.push_back(addr >> 8);
  request.push_back(addr & 0xFF);
  request.push_back(value ? 0xFF : 0x00); // Value high (0xFF00 = ON, 0x0000 = OFF)
  request.push_back(0x00);                // Value low

  return request;
}

std::vector<uint8_t> WagoIODevice::BuildWriteMultipleCoilsRequest(uint16_t startAddr, uint16_t count,
                                                                   const std::vector<bool>& values) {
  std::vector<uint8_t> request;
  uint16_t transId = ++m_transactionId;

  // Calculate byte count for coil data
  uint8_t byteCount = static_cast<uint8_t>((count + 7) / 8);

  // MBAP Header
  request.push_back(transId >> 8);
  request.push_back(transId & 0xFF);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(7 + byteCount);      // Length
  request.push_back(0x01);               // Unit ID

  // PDU
  request.push_back(0x0F);               // Function code: Write Multiple Coils
  request.push_back(startAddr >> 8);
  request.push_back(startAddr & 0xFF);
  request.push_back(count >> 8);
  request.push_back(count & 0xFF);
  request.push_back(byteCount);

  // Pack coil values into bytes
  for (int i = 0; i < byteCount; i++) {
    uint8_t byte = 0;
    for (int bit = 0; bit < 8 && (i * 8 + bit) < count; bit++) {
      if (values[i * 8 + bit]) {
        byte |= (1 << bit);
      }
    }
    request.push_back(byte);
  }

  return request;
}

std::vector<uint8_t> WagoIODevice::BuildReadHoldingRegistersRequest(uint16_t startAddr, uint16_t count) {
  std::vector<uint8_t> request;
  uint16_t transId = ++m_transactionId;

  request.push_back(transId >> 8);
  request.push_back(transId & 0xFF);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x06);
  request.push_back(0x01);

  request.push_back(0x03);               // Function code: Read Holding Registers
  request.push_back(startAddr >> 8);
  request.push_back(startAddr & 0xFF);
  request.push_back(count >> 8);
  request.push_back(count & 0xFF);

  return request;
}

std::vector<uint8_t> WagoIODevice::BuildReadInputRegistersRequest(uint16_t startAddr, uint16_t count) {
  std::vector<uint8_t> request;
  uint16_t transId = ++m_transactionId;

  request.push_back(transId >> 8);
  request.push_back(transId & 0xFF);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x00);
  request.push_back(0x06);
  request.push_back(0x01);

  request.push_back(0x04);               // Function code: Read Input Registers
  request.push_back(startAddr >> 8);
  request.push_back(startAddr & 0xFF);
  request.push_back(count >> 8);
  request.push_back(count & 0xFF);

  return request;
}

// ============================================================================
// MODBUS RESPONSE PARSING
// ============================================================================

bool WagoIODevice::ParseReadBitsResponse(const std::vector<uint8_t>& response, uint64_t& bits, int count) {
  // Minimum response: MBAP header (7) + function code (1) + byte count (1) + data (1+)
  if (response.size() < 10) {
    return false;
  }

  // Check for error response (function code with high bit set)
  if (response[7] & 0x80) {
    std::cerr << "[WagoIODevice] Modbus error response: 0x" << std::hex
              << (int)response[8] << std::dec << std::endl;
    return false;
  }

  uint8_t byteCount = response[8];
  bits = 0;

  // Unpack bits from response bytes (supports up to 8 bytes = 64 bits for 48+ I/O)
  for (int i = 0; i < byteCount && i < 8; i++) {
    bits |= (static_cast<uint64_t>(response[9 + i]) << (i * 8));
  }

  // Mask to actual count
  if (count < 64) {
    bits &= ((1ULL << count) - 1);
  }

  return true;
}

bool WagoIODevice::ParseReadRegistersResponse(const std::vector<uint8_t>& response,
                                               std::vector<uint16_t>& registers) {
  if (response.size() < 10) {
    return false;
  }

  if (response[7] & 0x80) {
    return false;
  }

  uint8_t byteCount = response[8];
  int regCount = byteCount / 2;

  registers.clear();
  for (int i = 0; i < regCount; i++) {
    uint16_t reg = (response[9 + i * 2] << 8) | response[10 + i * 2];
    registers.push_back(reg);
  }

  return true;
}

// ============================================================================
// INPUT OPERATIONS
// ============================================================================

EziIOError WagoIODevice::ReadInputs(uint64_t& inputs, uint64_t& latch) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  std::lock_guard<std::mutex> lock(m_socketMutex);

  // Build and send Read Discrete Inputs request
  auto request = BuildReadDiscreteInputsRequest(0, m_config.InputCount);
  if (!SendRequest(request)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Expected response size: MBAP (7) + FC (1) + byte count (1) + data bytes
  size_t dataBytes = static_cast<size_t>((m_config.InputCount + 7) / 8);
  size_t expectedLen = 9 + dataBytes;

  std::vector<uint8_t> response;
  if (!ReceiveResponse(response, expectedLen)) {
    return EziIOError::OPERATION_FAILED;
  }

  uint64_t newInputs = 0;
  if (!ParseReadBitsResponse(response, newInputs, m_config.InputCount)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Calculate latch (rising edge detection)
  latch = (newInputs ^ m_previousInputs) & newInputs;

  // Update status
  {
    std::lock_guard<std::mutex> statusLock(m_statusMutex);
    m_lastStatus.Inputs = newInputs;
    m_lastStatus.Latch |= latch;  // Accumulate latch bits
    m_lastStatus.Valid = true;
  }

  m_previousInputs = newInputs;
  inputs = newInputs;

  return EziIOError::SUCCESS;
}

EziIOError WagoIODevice::GetLastInputStatus(uint64_t& inputs, uint64_t& latch) {
  std::lock_guard<std::mutex> lock(m_statusMutex);

  if (!m_lastStatus.Valid) {
    return EziIOError::NOT_INITIALIZED;
  }

  inputs = m_lastStatus.Inputs;
  latch = m_lastStatus.Latch;
  return EziIOError::SUCCESS;
}

EziIOError WagoIODevice::ClearLatch(uint64_t latchMask) {
  std::lock_guard<std::mutex> lock(m_statusMutex);
  m_lastStatus.Latch &= ~latchMask;
  return EziIOError::SUCCESS;
}

// ============================================================================
// OUTPUT OPERATIONS
// ============================================================================

EziIOError WagoIODevice::ReadOutputs(uint64_t& outputs, uint64_t& status) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  std::lock_guard<std::mutex> lock(m_socketMutex);

  // Build and send Read Coils request
  // WAGO 750: Read output status from address 0x0200 (512), not 0x0000
  auto request = BuildReadCoilsRequest(WAGO_OUTPUT_READ_ADDR, m_config.OutputCount);
  if (!SendRequest(request)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Expected response size
  size_t dataBytes = static_cast<size_t>((m_config.OutputCount + 7) / 8);
  size_t expectedLen = 9 + dataBytes;

  std::vector<uint8_t> response;
  if (!ReceiveResponse(response, expectedLen)) {
    return EziIOError::OPERATION_FAILED;
  }

  uint64_t wagoRawBits = 0;
  if (!ParseReadBitsResponse(response, wagoRawBits, m_config.OutputCount)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Update cached state (keep raw WAGO bits for internal use)
  m_currentOutputState = wagoRawBits;

  // Convert WAGO raw bits (0-7) to UI format (8-15 for 8-pin devices)
  uint64_t uiFormattedBits = WagoToUiBits(wagoRawBits);

  // Update status with UI-formatted bits
  {
    std::lock_guard<std::mutex> statusLock(m_statusMutex);
    m_lastStatus.Outputs = uiFormattedBits;
    m_lastStatus.OutputStatus = uiFormattedBits;
    m_lastStatus.Valid = true;
  }

  outputs = uiFormattedBits;
  status = uiFormattedBits;

  return EziIOError::SUCCESS;
}

EziIOError WagoIODevice::GetLastOutputStatus(uint64_t& outputs, uint64_t& status) {
  std::lock_guard<std::mutex> lock(m_statusMutex);

  if (!m_lastStatus.Valid) {
    return EziIOError::NOT_INITIALIZED;
  }

  outputs = m_lastStatus.Outputs;
  status = m_lastStatus.OutputStatus;
  return EziIOError::SUCCESS;
}

EziIOError WagoIODevice::SetOutputs(uint64_t setMask, uint64_t clearMask) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  // Convert UI format masks to WAGO raw format
  uint64_t wagoSetMask = UiToWagoBits(setMask);
  uint64_t wagoClearMask = UiToWagoBits(clearMask);

  // Calculate new output state (using raw WAGO bits)
  uint64_t newState = (m_currentOutputState | wagoSetMask) & ~wagoClearMask;

  // Build coil values array
  std::vector<bool> values;
  for (int i = 0; i < m_config.OutputCount; i++) {
    values.push_back((newState & (1ULL << i)) != 0);
  }

  std::lock_guard<std::mutex> lock(m_socketMutex);

  // Write all coils to address 0x0000 (WAGO Process Image Output area)
  auto request = BuildWriteMultipleCoilsRequest(WAGO_OUTPUT_WRITE_ADDR, static_cast<uint16_t>(m_config.OutputCount), values);
  if (!SendRequest(request)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Expected response: MBAP (7) + FC (1) + Start Addr (2) + Quantity (2) = 12 bytes
  std::vector<uint8_t> response;
  if (!ReceiveResponse(response, 12)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Check for error
  if (response.size() >= 8 && (response[7] & 0x80)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Update cached state (raw WAGO bits)
  m_currentOutputState = newState;

  // Update status with UI-formatted bits
  uint64_t uiFormattedBits = WagoToUiBits(newState);
  {
    std::lock_guard<std::mutex> statusLock(m_statusMutex);
    m_lastStatus.Outputs = uiFormattedBits;
    m_lastStatus.OutputStatus = uiFormattedBits;
  }

  return EziIOError::SUCCESS;
}

EziIOError WagoIODevice::SetOutput(int pin, bool state) {
  if (!m_isConnected) {
    return EziIOError::DEVICE_DISCONNECTED;
  }

  // Pin is 0-based (0-47 for 48-pin WAGO)
  if (pin < 0 || pin >= m_config.OutputCount) {
    return EziIOError::INVALID_PIN;
  }

  std::lock_guard<std::mutex> lock(m_socketMutex);

  // Write single coil to address 0x0000 + pin (WAGO Process Image Output area)
  auto request = BuildWriteSingleCoilRequest(static_cast<uint16_t>(WAGO_OUTPUT_WRITE_ADDR + pin), state);
  if (!SendRequest(request)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Expected response: echo of request (12 bytes)
  std::vector<uint8_t> response;
  if (!ReceiveResponse(response, 12)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Check for error
  if (response.size() >= 8 && (response[7] & 0x80)) {
    return EziIOError::OPERATION_FAILED;
  }

  // Update cached state (raw WAGO bits)
  if (state) {
    m_currentOutputState |= (1ULL << pin);
  } else {
    m_currentOutputState &= ~(1ULL << pin);
  }

  // Update status with UI-formatted bits
  uint64_t uiFormattedBits = WagoToUiBits(m_currentOutputState);
  {
    std::lock_guard<std::mutex> statusLock(m_statusMutex);
    m_lastStatus.Outputs = uiFormattedBits;
    m_lastStatus.OutputStatus = uiFormattedBits;
  }

  return EziIOError::SUCCESS;
}

uint64_t WagoIODevice::GetOutputPinMask(int pin) const {
  if (pin < 0 || pin >= m_config.OutputCount) {
    return 0;
  }
  // Return UI-formatted mask (shifted by bit offset)
  int offset = (m_config.OutputCount > 8) ? 16 : 8;
  return (1ULL << (offset + pin));
}

// ============================================================================
// PIN NAMING
// ============================================================================

void WagoIODevice::MapInputPinName(const std::string& pinName, int pinNumber) {
  m_inputPinNames[pinName] = pinNumber;
  m_inputPinNumbers[pinNumber] = pinName;
}

void WagoIODevice::MapOutputPinName(const std::string& pinName, int pinNumber) {
  m_outputPinNames[pinName] = pinNumber;
  m_outputPinNumbers[pinNumber] = pinName;
}

int WagoIODevice::GetInputPinByName(const std::string& pinName) const {
  auto it = m_inputPinNames.find(pinName);
  return (it != m_inputPinNames.end()) ? it->second : -1;
}

int WagoIODevice::GetOutputPinByName(const std::string& pinName) const {
  auto it = m_outputPinNames.find(pinName);
  return (it != m_outputPinNames.end()) ? it->second : -1;
}

std::string WagoIODevice::GetInputPinName(int pinNumber) const {
  auto it = m_inputPinNumbers.find(pinNumber);
  return (it != m_inputPinNumbers.end()) ? it->second : "";
}

std::string WagoIODevice::GetOutputPinName(int pinNumber) const {
  auto it = m_outputPinNumbers.find(pinNumber);
  return (it != m_outputPinNumbers.end()) ? it->second : "";
}

std::vector<std::string> WagoIODevice::GetAllInputPinNames() const {
  std::vector<std::string> names;
  for (const auto& [name, pin] : m_inputPinNames) {
    names.push_back(name);
  }
  return names;
}

std::vector<std::string> WagoIODevice::GetAllOutputPinNames() const {
  std::vector<std::string> names;
  for (const auto& [name, pin] : m_outputPinNames) {
    names.push_back(name);
  }
  return names;
}
