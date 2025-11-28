"""
CCS PD3-10024-8-EI(A) LED Controller Test Script
Tests communication with CCS LED controllers at specified IP addresses.

Protocol: TCP/IP
Default Port: 40001
Default IP: 192.168.0.2

Command format: @[Channel][Instruction][Data][Checksum]<CR><LF>
Response format: @[Channel][Response][Checksum]<CR><LF>
"""

import socket
import time
from typing import Optional


class CCSPD3Controller:
    """Class to communicate with CCS PD3-10024-8-EI(A) LED Controller."""

    DEFAULT_PORT = 40001
    TIMEOUT = 5.0

    def __init__(self, ip_address: str, port: int = DEFAULT_PORT):
        self.ip_address = ip_address
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.connected = False

    def connect(self) -> bool:
        """Establish connection to the controller."""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(self.TIMEOUT)
            self.socket.connect((self.ip_address, self.port))
            self.connected = True
            print(f"[OK] Connected to {self.ip_address}:{self.port}")
            return True
        except socket.timeout:
            print(f"[ERROR] Connection timeout to {self.ip_address}:{self.port}")
            return False
        except socket.error as e:
            print(f"[ERROR] Connection failed to {self.ip_address}:{self.port}: {e}")
            return False

    def disconnect(self):
        """Close the connection."""
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
            self.connected = False
            print(f"[OK] Disconnected from {self.ip_address}")

    @staticmethod
    def calculate_checksum(data: str) -> str:
        """
        Calculate checksum for CCS PD3 command.
        Sum all ASCII values, take lowest byte, convert to 2-char hex.
        """
        total = sum(ord(c) for c in data)
        checksum = total & 0xFF  # Take lowest byte
        return f"{checksum:02X}"

    def build_command(self, channel: str, instruction: str, data: str = "") -> str:
        """
        Build a complete command with checksum.
        Format: @[Channel][Instruction][Data][Checksum]
        """
        # Build command without checksum
        cmd_body = f"@{channel}{instruction}{data}"
        # Calculate and append checksum
        checksum = self.calculate_checksum(cmd_body)
        return cmd_body + checksum

    def send_command(self, command: str) -> Optional[str]:
        """Send a command and receive response."""
        if not self.connected or not self.socket:
            print("[ERROR] Not connected")
            return None

        try:
            # Send command with CRLF termination
            cmd_bytes = (command + '\r\n').encode('ascii')
            self.socket.sendall(cmd_bytes)
            print(f"[TX] {command}")

            # Receive response
            response = b''
            self.socket.settimeout(2.0)
            try:
                while True:
                    chunk = self.socket.recv(1024)
                    if not chunk:
                        break
                    response += chunk
                    if b'\r\n' in response or b'\r' in response or b'\n' in response:
                        break
            except socket.timeout:
                pass

            response_str = response.decode('ascii', errors='ignore').strip()
            print(f"[RX] {response_str}")
            return response_str

        except socket.timeout:
            print("[ERROR] Response timeout")
            return None
        except socket.error as e:
            print(f"[ERROR] Communication error: {e}")
            return None

    # ==================== Status Commands ====================

    def check_error_status(self) -> Optional[str]:
        """
        Check error status (C command).
        Response: @00O00<checksum> (normal) or @00O11<checksum> (error)
        """
        cmd = self.build_command("00", "C", "")
        return self.send_command(cmd)

    def get_setting_status(self, channel: int) -> Optional[str]:
        """
        Get setting status for a channel (M command).
        Response: @[ch]OF[intensity].S[mode].L[onoff]<checksum>
        Example: @00OF075.S04.L0 means intensity=75, strobe mode=200us, light=OFF
        """
        ch = f"{channel:02d}"
        cmd = self.build_command(ch, "M", "")
        return self.send_command(cmd)

    def reset_all_channels(self) -> Optional[str]:
        """
        Reset all channels to default (R command).
        """
        cmd = self.build_command("00", "R", "")
        return self.send_command(cmd)

    # ==================== Channel Control Commands ====================

    def set_channel_intensity(self, channel: int, intensity: int) -> Optional[str]:
        """
        Set intensity for a specific channel (F command).

        Args:
            channel: Channel number (0-7 for L1-L8, or 0xFF for all)
            intensity: Intensity value (0-255)
        """
        if not 0 <= channel <= 7:
            print(f"[ERROR] Invalid channel {channel}, must be 0-7 (L1-L8)")
            return None
        if not 0 <= intensity <= 255:
            print(f"[ERROR] Invalid intensity {intensity}, must be 0-255")
            return None

        ch = f"{channel:02d}"
        data = f"{intensity:03d}"
        cmd = self.build_command(ch, "F", data)
        return self.send_command(cmd)

    def set_all_channels_intensity(self, intensity: int) -> Optional[str]:
        """Set all channels to the same intensity."""
        if not 0 <= intensity <= 255:
            print(f"[ERROR] Invalid intensity {intensity}, must be 0-255")
            return None

        data = f"{intensity:03d}"
        cmd = self.build_command("FF", "F", data)
        return self.send_command(cmd)

    def set_lighting_mode(self, channel: int, mode: int) -> Optional[str]:
        """
        Set lighting mode for a channel (S command).

        Args:
            channel: Channel number (0-7)
            mode: 0=continuous/ON-OFF, 1-10=strobe (40us to 40ms)
        """
        if not 0 <= channel <= 7:
            print(f"[ERROR] Invalid channel {channel}, must be 0-7")
            return None
        if not 0 <= mode <= 10:
            print(f"[ERROR] Invalid mode {mode}, must be 0-10")
            return None

        ch = f"{channel:02d}"
        data = f"{mode:02d}"
        cmd = self.build_command(ch, "S", data)
        return self.send_command(cmd)

    def set_onoff(self, channel: int, on: bool) -> Optional[str]:
        """
        Set ON/OFF state for a channel (L command).

        Args:
            channel: Channel number (0-7, or use 0xFF for all)
            on: True for ON, False for OFF
        """
        if not 0 <= channel <= 7 and channel != 0xFF:
            print(f"[ERROR] Invalid channel {channel}")
            return None

        ch = f"{channel:02d}" if channel <= 7 else "FF"
        data = "1" if on else "0"
        cmd = self.build_command(ch, "L", data)
        return self.send_command(cmd)

    def all_channels_off(self) -> Optional[str]:
        """Turn off all channels."""
        cmd = self.build_command("FF", "L", "0")
        return self.send_command(cmd)

    def all_channels_on(self) -> Optional[str]:
        """Turn on all channels."""
        cmd = self.build_command("FF", "L", "1")
        return self.send_command(cmd)

    # ==================== Network Settings ====================

    def set_ip_address(self, ip: str) -> Optional[str]:
        """Set IP address (E01 command). Requires power cycle to take effect."""
        # Format: 192.168.000.002 (must be padded)
        parts = ip.split('.')
        formatted_ip = '.'.join(f"{int(p):03d}" for p in parts)
        cmd = self.build_command("00", "E01", formatted_ip)
        return self.send_command(cmd)

    def set_subnet_mask(self, mask: str) -> Optional[str]:
        """Set subnet mask (E02 command). Requires power cycle to take effect."""
        parts = mask.split('.')
        formatted_mask = '.'.join(f"{int(p):03d}" for p in parts)
        cmd = self.build_command("00", "E02", formatted_mask)
        return self.send_command(cmd)

    def set_gateway(self, gateway: str) -> Optional[str]:
        """Set default gateway (E03 command). Requires power cycle to take effect."""
        parts = gateway.split('.')
        formatted_gw = '.'.join(f"{int(p):03d}" for p in parts)
        cmd = self.build_command("00", "E03", formatted_gw)
        return self.send_command(cmd)


def test_connection(ip_address: str, port: int = 40001) -> bool:
    """Test basic TCP connection to the controller."""
    print(f"\n{'='*60}")
    print(f"Testing connection to {ip_address}:{port}")
    print('='*60)

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        result = sock.connect_ex((ip_address, port))
        sock.close()

        if result == 0:
            print(f"[OK] Port {port} is OPEN")
            return True
        else:
            print(f"[FAIL] Port {port} is CLOSED (error code: {result})")
            return False
    except Exception as e:
        print(f"[FAIL] Connection test failed: {e}")
        return False


def test_controller(ip_address: str, port: int = 40001):
    """Run full test sequence on a controller."""
    print(f"\n{'='*60}")
    print(f"Full Test: CCS PD3 Controller at {ip_address}:{port}")
    print('='*60)

    controller = CCSPD3Controller(ip_address, port)

    if not controller.connect():
        print("[FAIL] Could not connect to controller")
        return False

    try:
        # Test 1: Check error status
        print("\n--- Test 1: Check Error Status (C command) ---")
        status = controller.check_error_status()

        # Test 2: Get setting status for channel 0 (L1)
        print("\n--- Test 2: Get Setting Status for L1 (M command) ---")
        setting = controller.get_setting_status(0)

        # Test 3: Get setting status for all channels
        print("\n--- Test 3: Get Setting Status for All Channels ---")
        for ch in range(8):
            print(f"\n  Channel L{ch+1}:")
            controller.get_setting_status(ch)
            time.sleep(0.1)

        print("\n[OK] Test sequence completed")
        return True

    except Exception as e:
        print(f"[ERROR] Test failed: {e}")
        return False

    finally:
        controller.disconnect()


def test_light_control(ip_address: str, port: int = 40001, channel: int = 0, intensity: int = 50):
    """Test light control on a specific channel."""
    print(f"\n{'='*60}")
    print(f"Light Control Test: {ip_address} Channel L{channel+1}")
    print('='*60)

    controller = CCSPD3Controller(ip_address, port)

    if not controller.connect():
        return False

    try:
        # Set continuous mode
        print(f"\n--- Setting Channel L{channel+1} to Continuous Mode ---")
        controller.set_lighting_mode(channel, 0)
        time.sleep(0.2)

        # Turn on
        print(f"\n--- Turning ON Channel L{channel+1} ---")
        controller.set_onoff(channel, True)
        time.sleep(0.2)

        # Set intensity
        print(f"\n--- Setting Channel L{channel+1} Intensity to {intensity} ---")
        controller.set_channel_intensity(channel, intensity)
        time.sleep(1.0)

        # Read back status
        print(f"\n--- Reading Channel L{channel+1} Status ---")
        controller.get_setting_status(channel)

        # Turn off
        print(f"\n--- Turning OFF Channel L{channel+1} ---")
        controller.set_onoff(channel, False)

        print("\n[OK] Light control test completed")
        return True

    finally:
        controller.disconnect()


def scan_ports(ip_address: str, ports: list = None):
    """Scan common ports on the controller."""
    if ports is None:
        ports = [23, 80, 502, 5000, 8080, 40001, 40002, 50000, 50001]

    print(f"\n{'='*60}")
    print(f"Port Scan: {ip_address}")
    print('='*60)

    open_ports = []
    for port in ports:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            result = sock.connect_ex((ip_address, port))
            sock.close()

            if result == 0:
                print(f"  Port {port}: OPEN")
                open_ports.append(port)
            else:
                print(f"  Port {port}: closed")
        except:
            print(f"  Port {port}: error")

    return open_ports


def ping_test(ip_address: str) -> bool:
    """Test if IP is reachable via ICMP ping."""
    import subprocess
    import platform

    print(f"\n--- Ping Test: {ip_address} ---")

    # Windows uses -n, Linux/Mac uses -c
    param = '-n' if platform.system().lower() == 'windows' else '-c'

    try:
        result = subprocess.run(
            ['ping', param, '1', '-w', '1000', ip_address],
            capture_output=True,
            text=True,
            timeout=5
        )

        if result.returncode == 0:
            print(f"[OK] {ip_address} is reachable")
            return True
        else:
            print(f"[FAIL] {ip_address} is not reachable")
            return False
    except Exception as e:
        print(f"[ERROR] Ping failed: {e}")
        return False


def print_command_reference():
    """Print command reference from the PDF manual."""
    print("""
CCS PD3-10024-8-EI(A) Command Reference
=======================================

Command Format: @[Channel][Instruction][Data][Checksum]<CR><LF>
Response Format: @[Channel][OK/NG][Data][Checksum]<CR><LF>

Channel Specification:
  00-07 = L1-L8
  FF = All channels (for F and L commands)

Instructions:
  F[intensity]  - Set light intensity (000-255)
  S[mode]       - Set lighting mode (00=continuous, 01-10=strobe 40us-40ms)
  L[state]      - Set ON/OFF (0=OFF, 1=ON)
  M             - Get setting status (returns F[int].S[mode].L[state])
  C             - Check error status (returns 00=OK, 11=Error)
  R             - Reset all channels to default

Network Settings (require power cycle):
  E01[ip]       - Set IP address (format: 192.168.000.002)
  E02[mask]     - Set subnet mask (format: 255.255.255.000)
  E03[gateway]  - Set default gateway
  E04[port]     - Set reception port (00000-65535)
  E05[ip]       - Set reply IP address
  E06[port]     - Set reply port

Factory Defaults:
  IP Address: 192.168.0.2
  Subnet Mask: 255.255.255.0
  Gateway: 192.168.0.1
  Port: 40001

Checksum Calculation:
  Sum ASCII values of all characters from @ to data
  Take lowest byte (& 0xFF)
  Convert to 2-character uppercase hex
""")


def parse_status_response(response: str) -> dict:
    """
    Parse the status response from M command.
    Example: @00OF000.S00.L151 -> {channel: 0, intensity: 0, mode: 0, on: True}
    """
    result = {
        'raw': response,
        'valid': False,
        'channel': None,
        'intensity': None,
        'mode': None,
        'mode_name': None,
        'on': None,
    }

    try:
        if not response.startswith('@') or 'O' not in response:
            return result

        # Extract channel
        result['channel'] = int(response[1:3])

        # Find the data part after 'O'
        data_start = response.index('O') + 1
        data = response[data_start:]

        # Parse F[intensity].S[mode].L[state]
        # Example: F000.S00.L151
        parts = data.split('.')
        if len(parts) >= 3:
            # Intensity (F000-F255)
            if parts[0].startswith('F'):
                result['intensity'] = int(parts[0][1:4])

            # Mode (S00-S10)
            if parts[1].startswith('S'):
                mode = int(parts[1][1:3])
                result['mode'] = mode
                mode_names = {
                    0: "Continuous/ON-OFF",
                    1: "Strobe 40μs",
                    2: "Strobe 80μs",
                    3: "Strobe 120μs",
                    4: "Strobe 200μs",
                    5: "Strobe 600μs",
                    6: "Strobe 1ms",
                    7: "Strobe 4ms",
                    8: "Strobe 10ms",
                    9: "Strobe 20ms",
                    10: "Strobe 40ms",
                }
                result['mode_name'] = mode_names.get(mode, f"Unknown ({mode})")

            # ON/OFF state (L0 or L1)
            if parts[2].startswith('L'):
                result['on'] = parts[2][1] == '1'

            result['valid'] = True
    except (ValueError, IndexError) as e:
        print(f"[WARN] Failed to parse response: {e}")

    return result


def parse_error_response(response: str) -> dict:
    """
    Parse error status response from C command.
    Example: @00O004F -> {normal: True, error_code: 0}
    """
    result = {
        'raw': response,
        'valid': False,
        'normal': None,
        'error_code': None,
    }

    try:
        if not response.startswith('@') or 'O' not in response:
            return result

        data_start = response.index('O') + 1
        error_code = response[data_start:data_start+2]
        result['error_code'] = int(error_code)
        result['normal'] = (error_code == '00')
        result['valid'] = True
    except (ValueError, IndexError):
        pass

    return result


def print_status_summary(controller: CCSPD3Controller):
    """Print a formatted summary of all channel statuses."""
    print("\n" + "="*60)
    print("Channel Status Summary")
    print("="*60)
    print(f"{'Channel':<10} {'Intensity':<12} {'Mode':<20} {'State':<8}")
    print("-"*60)

    for ch in range(8):
        response = controller.get_setting_status(ch)
        if response:
            status = parse_status_response(response)
            if status['valid']:
                state = "ON" if status['on'] else "OFF"
                intensity_pct = (status['intensity'] / 255) * 100
                print(f"L{ch+1:<9} {status['intensity']:>3} ({intensity_pct:>5.1f}%)   {status['mode_name']:<20} {state:<8}")
            else:
                print(f"L{ch+1:<9} {'Parse error':<50}")
        time.sleep(0.05)

    print("="*60)


def cycle_all_channels(controller: CCSPD3Controller, intensity: int = 128, on_time: float = 1.0, off_time: float = 1.0):
    """
    Cycle through all 8 channels, turning each on then off.

    Args:
        controller: Connected CCSPD3Controller instance
        intensity: Brightness level 0-255 (128 = 50%)
        on_time: Time to keep channel ON in seconds
        off_time: Time to keep channel OFF in seconds
    """
    print("\n" + "="*60)
    print(f"Cycling All Channels - Intensity: {intensity} ({intensity/255*100:.1f}%)")
    print(f"ON time: {on_time}s, OFF time: {off_time}s")
    print("="*60)

    for ch in range(8):
        channel_name = f"L{ch+1}"

        # Turn ON with specified intensity
        print(f"\n[{channel_name}] Turning ON at {intensity/255*100:.1f}% brightness...")
        controller.set_channel_intensity(ch, intensity)
        time.sleep(on_time)

        # Turn OFF
        print(f"[{channel_name}] Turning OFF...")
        controller.set_channel_intensity(ch, 0)
        time.sleep(off_time)

    print("\n" + "="*60)
    print("Channel cycle complete!")
    print("="*60)


# ==================== Main Test Script ====================

if __name__ == "__main__":
    # Controller configuration
    CONTROLLER_IP = "192.168.0.2"
    CONTROLLER_PORT = 40001

    # Test parameters
    BRIGHTNESS = 128  # 50% brightness (128/255)
    ON_TIME = 1.0     # 1 second ON
    OFF_TIME = 1.0    # 1 second OFF

    print("\n" + "="*60)
    print("CCS PD3-10024-8-EI(A) LED Controller Test")
    print("="*60)
    print(f"Controller: {CONTROLLER_IP}:{CONTROLLER_PORT}")

    # Create controller instance
    controller = CCSPD3Controller(CONTROLLER_IP, CONTROLLER_PORT)

    if not controller.connect():
        print("[FAIL] Could not connect to controller")
        exit(1)

    try:
        # Check error status
        print("\n--- Error Status Check ---")
        error_response = controller.check_error_status()
        if error_response:
            error_status = parse_error_response(error_response)
            if error_status['valid']:
                if error_status['normal']:
                    print("[OK] Controller status: NORMAL (no errors)")
                else:
                    print(f"[WARN] Controller status: ERROR (code: {error_status['error_code']})")

        # Print initial status summary
        print("\n--- Initial Channel Status ---")
        print_status_summary(controller)

        # Cycle through all channels
        print("\n>>> Starting channel cycle test...")
        print(">>> Press Ctrl+C to stop\n")

        cycle_all_channels(
            controller,
            intensity=BRIGHTNESS,
            on_time=ON_TIME,
            off_time=OFF_TIME
        )

        # Ensure all channels are off at the end
        print("\n--- Turning off all channels ---")
        for ch in range(8):
            controller.set_channel_intensity(ch, 0)
            time.sleep(0.05)

        print("\n[OK] Test completed successfully")

    except KeyboardInterrupt:
        print("\n\n[INTERRUPTED] Stopping test...")
        # Turn off all channels on interrupt
        print("Turning off all channels...")
        for ch in range(8):
            controller.set_channel_intensity(ch, 0)
            time.sleep(0.05)
        print("[OK] All channels turned off")

    finally:
        controller.disconnect()

    print("\n" + "="*60)
    print("Test Complete")
    print("="*60)
