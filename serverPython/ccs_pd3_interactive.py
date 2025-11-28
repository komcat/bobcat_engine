"""
CCS PD3-10024-8-EI(A) Interactive LED Controller
Interactive script for controlling CCS LED lights.

Channel Configuration:
  L1 (ch0) = Collimate Light
  L2 (ch1) = Red
  L3 (ch2) = Green
  L4 (ch3) = Blue
  L5-L8    = Available

Usage:
    python ccs_pd3_interactive.py
"""

import socket
import time
import sys
from typing import Optional


class CCSPD3Controller:
    """Class to communicate with CCS PD3-10024-8-EI(A) LED Controller."""

    DEFAULT_PORT = 40001
    TIMEOUT = 5.0

    # Channel names mapping
    CHANNEL_NAMES = {
        0: "Collimate",
        1: "Red",
        2: "Green",
        3: "Blue",
        4: "L5",
        5: "L6",
        6: "L7",
        7: "L8",
    }

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
        """Calculate checksum: sum ASCII values, take lowest byte, convert to 2-char hex."""
        total = sum(ord(c) for c in data)
        checksum = total & 0xFF
        return f"{checksum:02X}"

    def build_command(self, channel: str, instruction: str, data: str = "") -> str:
        """Build command with checksum: @[Channel][Instruction][Data][Checksum]"""
        cmd_body = f"@{channel}{instruction}{data}"
        checksum = self.calculate_checksum(cmd_body)
        return cmd_body + checksum

    def send_command(self, command: str, silent: bool = False) -> Optional[str]:
        """Send a command and receive response."""
        if not self.connected or not self.socket:
            print("[ERROR] Not connected")
            return None

        try:
            cmd_bytes = (command + '\r\n').encode('ascii')
            self.socket.sendall(cmd_bytes)
            if not silent:
                print(f"[TX] {command}")

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
            if not silent:
                print(f"[RX] {response_str}")
            return response_str

        except socket.timeout:
            print("[ERROR] Response timeout")
            return None
        except socket.error as e:
            print(f"[ERROR] Communication error: {e}")
            return None

    # ==================== Light Control Commands ====================

    def set_intensity(self, channel: int, intensity: int, silent: bool = False) -> Optional[str]:
        """Set intensity for a channel (0-7), intensity (0-255)."""
        if not 0 <= channel <= 7:
            print(f"[ERROR] Invalid channel {channel}")
            return None
        if not 0 <= intensity <= 255:
            print(f"[ERROR] Invalid intensity {intensity}, must be 0-255")
            return None

        ch = f"{channel:02d}"
        data = f"{intensity:03d}"
        cmd = self.build_command(ch, "F", data)
        return self.send_command(cmd, silent)

    def set_all_intensity(self, intensity: int) -> Optional[str]:
        """Set all channels to the same intensity."""
        if not 0 <= intensity <= 255:
            print(f"[ERROR] Invalid intensity {intensity}")
            return None
        cmd = self.build_command("FF", "F", f"{intensity:03d}")
        return self.send_command(cmd)

    def get_status(self, channel: int, silent: bool = False) -> Optional[str]:
        """Get status for a channel."""
        ch = f"{channel:02d}"
        cmd = self.build_command(ch, "M", "")
        return self.send_command(cmd, silent)

    def check_error(self) -> Optional[str]:
        """Check error status."""
        cmd = self.build_command("00", "C", "")
        return self.send_command(cmd)

    # ==================== Convenience Methods ====================

    def collimate_on(self, intensity: int = 128):
        """Turn on collimate light (L1/ch0)."""
        print(f"[Collimate] ON at {intensity/255*100:.1f}%")
        return self.set_intensity(0, intensity, silent=True)

    def collimate_off(self):
        """Turn off collimate light."""
        print("[Collimate] OFF")
        return self.set_intensity(0, 0, silent=True)

    def red_on(self, intensity: int = 128):
        """Turn on red light (L2/ch1)."""
        print(f"[Red] ON at {intensity/255*100:.1f}%")
        return self.set_intensity(1, intensity, silent=True)

    def red_off(self):
        """Turn off red light."""
        print("[Red] OFF")
        return self.set_intensity(1, 0, silent=True)

    def green_on(self, intensity: int = 128):
        """Turn on green light (L3/ch2)."""
        print(f"[Green] ON at {intensity/255*100:.1f}%")
        return self.set_intensity(2, intensity, silent=True)

    def green_off(self):
        """Turn off green light."""
        print("[Green] OFF")
        return self.set_intensity(2, 0, silent=True)

    def blue_on(self, intensity: int = 128):
        """Turn on blue light (L4/ch3)."""
        print(f"[Blue] ON at {intensity/255*100:.1f}%")
        return self.set_intensity(3, intensity, silent=True)

    def blue_off(self):
        """Turn off blue light."""
        print("[Blue] OFF")
        return self.set_intensity(3, 0, silent=True)

    def all_off(self):
        """Turn off all channels."""
        print("[ALL] OFF")
        return self.set_all_intensity(0)

    def white_on(self, intensity: int = 128):
        """Turn on RGB (white light)."""
        print(f"[White/RGB] ON at {intensity/255*100:.1f}%")
        self.set_intensity(1, intensity, silent=True)
        self.set_intensity(2, intensity, silent=True)
        self.set_intensity(3, intensity, silent=True)

    def print_status(self):
        """Print status of all channels."""
        print("\n" + "="*50)
        print("Channel Status")
        print("="*50)
        print(f"{'Channel':<12} {'Name':<12} {'Intensity':<15} {'%':<8}")
        print("-"*50)

        for ch in range(8):
            response = self.get_status(ch, silent=True)
            if response and 'OF' in response:
                try:
                    # Parse F[intensity] from response
                    idx = response.index('OF') + 2
                    intensity = int(response[idx:idx+3])
                    pct = intensity / 255 * 100
                    name = self.CHANNEL_NAMES.get(ch, f"L{ch+1}")
                    print(f"L{ch+1:<11} {name:<12} {intensity:<15} {pct:.1f}%")
                except:
                    print(f"L{ch+1:<11} {'Parse error':<30}")
            time.sleep(0.02)

        print("="*50)


def print_help():
    """Print available commands."""
    print("""
============================================================
CCS PD3 LED Controller - Interactive Commands
============================================================

QUICK COMMANDS:
  collimate [intensity]  - Turn on collimate light (L1)
  red [intensity]        - Turn on red light (L2)
  green [intensity]      - Turn on green light (L3)
  blue [intensity]       - Turn on blue light (L4)
  white [intensity]      - Turn on RGB (red+green+blue)
  off                    - Turn off all lights
  status                 - Show all channel status

SET INTENSITY:
  set <channel> <intensity>
    channel: 1-8 or collimate/red/green/blue
    intensity: 0-255 (or 0%-100%)

  Examples:
    set 1 128          - Set L1 to 128 (50%)
    set red 200        - Set red to 200 (78%)
    set blue 50%       - Set blue to 50%
    set collimate 100  - Set collimate to 100

RAW COMMANDS:
  raw <command>        - Send raw command (e.g., raw @00F12884)

OTHER:
  help                 - Show this help
  quit / exit          - Exit program

NOTES:
  - Intensity: 0-255 (0=off, 255=max) or use percentage (e.g., 50%)
  - Default intensity if not specified: 128 (50%)
============================================================
""")


def parse_intensity(value: str) -> int:
    """Parse intensity from string (supports 0-255 or percentage)."""
    value = value.strip()
    if value.endswith('%'):
        pct = float(value[:-1])
        return int(pct / 100 * 255)
    else:
        return int(value)


def interactive_mode(ip: str, port: int):
    """Run interactive mode."""
    ctrl = CCSPD3Controller(ip, port)

    if not ctrl.connect():
        print("Failed to connect. Check IP address and port.")
        return

    print("\nCCS PD3 LED Controller - Interactive Mode")
    print("Type 'help' for commands, 'quit' to exit")
    print("-" * 50)

    # Show initial status
    ctrl.print_status()

    try:
        while True:
            try:
                user_input = input("\n> ").strip()
            except EOFError:
                break

            if not user_input:
                continue

            parts = user_input.lower().split()
            cmd = parts[0]

            # Get optional intensity (default 128 = 50%)
            intensity = 128
            if len(parts) > 1:
                try:
                    intensity = parse_intensity(parts[1])
                except ValueError:
                    pass

            # Process commands
            if cmd in ('quit', 'exit', 'q'):
                ctrl.all_off()
                break

            elif cmd == 'help':
                print_help()

            elif cmd == 'status':
                ctrl.print_status()

            elif cmd == 'off':
                ctrl.all_off()

            elif cmd == 'collimate':
                if len(parts) > 1 and parts[1] == 'off':
                    ctrl.collimate_off()
                else:
                    ctrl.collimate_on(intensity)

            elif cmd == 'red':
                if len(parts) > 1 and parts[1] == 'off':
                    ctrl.red_off()
                else:
                    ctrl.red_on(intensity)

            elif cmd == 'green':
                if len(parts) > 1 and parts[1] == 'off':
                    ctrl.green_off()
                else:
                    ctrl.green_on(intensity)

            elif cmd == 'blue':
                if len(parts) > 1 and parts[1] == 'off':
                    ctrl.blue_off()
                else:
                    ctrl.blue_on(intensity)

            elif cmd == 'white':
                if len(parts) > 1 and parts[1] == 'off':
                    ctrl.red_off()
                    ctrl.green_off()
                    ctrl.blue_off()
                else:
                    ctrl.white_on(intensity)

            elif cmd == 'set':
                if len(parts) < 3:
                    print("Usage: set <channel> <intensity>")
                    print("  channel: 1-8 or collimate/red/green/blue")
                    print("  intensity: 0-255 or 0%-100%")
                    continue

                # Parse channel
                ch_str = parts[1].lower()
                channel_map = {
                    'collimate': 0, 'col': 0, '1': 0,
                    'red': 1, 'r': 1, '2': 1,
                    'green': 2, 'g': 2, '3': 2,
                    'blue': 3, 'b': 3, '4': 3,
                    '5': 4, '6': 5, '7': 6, '8': 7,
                }

                if ch_str in channel_map:
                    channel = channel_map[ch_str]
                else:
                    try:
                        channel = int(ch_str) - 1  # Convert 1-8 to 0-7
                    except ValueError:
                        print(f"Unknown channel: {ch_str}")
                        continue

                # Parse intensity
                try:
                    intensity = parse_intensity(parts[2])
                except ValueError:
                    print(f"Invalid intensity: {parts[2]}")
                    continue

                name = ctrl.CHANNEL_NAMES.get(channel, f"L{channel+1}")
                print(f"[{name}] Setting to {intensity} ({intensity/255*100:.1f}%)")
                ctrl.set_intensity(channel, intensity)

            elif cmd == 'raw':
                if len(parts) > 1:
                    raw_cmd = ' '.join(parts[1:])
                    ctrl.send_command(raw_cmd)
                else:
                    print("Usage: raw <command>")

            elif cmd == 'test':
                # Quick test cycle
                print("Running test cycle...")
                for name, ch in [('Collimate', 0), ('Red', 1), ('Green', 2), ('Blue', 3)]:
                    print(f"  {name} ON")
                    ctrl.set_intensity(ch, 128, silent=True)
                    time.sleep(0.5)
                    ctrl.set_intensity(ch, 0, silent=True)
                    time.sleep(0.3)
                print("Test complete")

            else:
                print(f"Unknown command: {cmd}")
                print("Type 'help' for available commands")

    except KeyboardInterrupt:
        print("\n\nInterrupted - turning off all lights...")
        ctrl.all_off()

    finally:
        ctrl.disconnect()


def main():
    # Default configuration
    ip = "192.168.0.2"
    port = 40001

    # Parse command line arguments
    if len(sys.argv) >= 2:
        ip = sys.argv[1]
    if len(sys.argv) >= 3:
        port = int(sys.argv[2])

    print("="*60)
    print("CCS PD3-10024-8-EI(A) LED Controller")
    print("="*60)
    print(f"Target: {ip}:{port}")
    print()
    print("Channel Configuration:")
    print("  L1 = Collimate Light")
    print("  L2 = Red")
    print("  L3 = Green")
    print("  L4 = Blue")
    print("  L5-L8 = Available")
    print("-"*60)

    interactive_mode(ip, port)


if __name__ == "__main__":
    main()
