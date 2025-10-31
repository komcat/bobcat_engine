import pyvisa
import sys
import socket
import ipaddress
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
import time
import requests
import urllib3
import json
import telnetlib
import re

# Disable SSL warnings for devices with self-signed certificates
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

def find_gpib_devices():
    """
    Scan for and print all available GPIB devices
    """
    try:
        # Create a resource manager
        rm = pyvisa.ResourceManager()
        
        # Get list of all available resources
        resources = rm.list_resources()
        
        if not resources:
            print("No VISA resources found.")
            return
        
        print("All VISA Resources:")
        print("-" * 40)
        for resource in resources:
            print(f"  {resource}")
        
        # Filter for GPIB devices specifically
        gpib_devices = [res for res in resources if 'GPIB' in res.upper()]
        
        if not gpib_devices:
            print("\nNo GPIB devices found.")
            return
        
        print(f"\nFound {len(gpib_devices)} GPIB device(s):")
        print("-" * 40)
        
        for device in gpib_devices:
            try:
                # Try to connect and get device info
                instrument = rm.open_resource(device)
                instrument.timeout = 2000  # 2 second timeout
                
                try:
                    # Try to get device identification
                    idn = instrument.query('*IDN?').strip()
                    print(f"  {device}: {idn}")
                except:
                    print(f"  {device}: (Device found but no response to *IDN?)")
                
                instrument.close()
                
            except Exception as e:
                print(f"  {device}: (Error connecting - {str(e)})")
    
    except ImportError:
        print("Error: pyvisa library not found.")
        print("Install it with: pip install pyvisa")
        sys.exit(1)
    
    except Exception as e:
        print(f"Error initializing VISA: {str(e)}")
        print("Make sure you have VISA runtime installed (NI-VISA, Keysight IO Libraries, etc.)")
        sys.exit(1)

def scan_gpib_addresses():
    """
    Alternative method: Scan specific GPIB addresses (0-30) on primary GPIB interface
    """
    try:
        rm = pyvisa.ResourceManager()
        print("\nScanning GPIB addresses 0-30 on primary interface:")
        print("-" * 50)
        
        found_devices = []
        
        for addr in range(31):  # GPIB addresses 0-30
            resource_name = f"GPIB0::{addr}::INSTR"
            try:
                instrument = rm.open_resource(resource_name)
                instrument.timeout = 1000  # 1 second timeout for scanning
                
                try:
                    idn = instrument.query('*IDN?').strip()
                    print(f"  Address {addr:2d}: {idn}")
                    found_devices.append((addr, idn))
                except:
                    # Device responds but not to *IDN?
                    print(f"  Address {addr:2d}: Device present (no *IDN? response)")
                    found_devices.append((addr, "Unknown device"))
                
                instrument.close()
                
            except:
                # No device at this address (expected for most addresses)
                pass
        
        if not found_devices:
            print("  No devices found on GPIB addresses 0-30")
        else:
            print(f"\nSummary: Found {len(found_devices)} device(s)")
            
    except Exception as e:
        print(f"Error during address scan: {str(e)}")

def find_lan_devices():
    """
    Scan for instruments connected via LAN (Ethernet)
    """
    print("\nScanning for LAN-connected instruments:")
    print("-" * 50)
    
    try:
        rm = pyvisa.ResourceManager()
        
        # Find TCPIP resources
        resources = rm.list_resources()
        tcpip_devices = [res for res in resources if 'TCPIP' in res.upper()]
        
        if tcpip_devices:
            print(f"Found {len(tcpip_devices)} TCPIP device(s) via VISA:")
            for device in tcpip_devices:
                try:
                    instrument = rm.open_resource(device)
                    instrument.timeout = 2000
                    try:
                        idn = instrument.query('*IDN?').strip()
                        print(f"  {device}: {idn}")
                    except:
                        print(f"  {device}: (No *IDN? response)")
                    instrument.close()
                except Exception as e:
                    print(f"  {device}: (Error: {str(e)})")
        else:
            print("  No TCPIP devices found via VISA resource discovery")
            
    except Exception as e:
        print(f"  Error: {str(e)}")

def check_web_interface(ip, port, timeout=3):
    """
    Check if the device has a web interface and try to identify it
    """
    info = {"ip": ip, "port": port, "type": "Unknown", "details": ""}
    
    # Try HTTP/HTTPS
    for protocol in ['http', 'https']:
        try:
            url = f"{protocol}://{ip}:{port}"
            response = requests.get(url, timeout=timeout, verify=False, allow_redirects=True)
            
            # Check response headers and content
            server = response.headers.get('Server', '')
            content_lower = response.text.lower()
            
            # Check for known instrument manufacturers in the response
            manufacturers = {
                'keysight': 'Keysight',
                'agilent': 'Agilent',
                'rigol': 'Rigol',
                'tektronix': 'Tektronix',
                'tek.com': 'Tektronix',
                'rohde': 'Rohde & Schwarz',
                'schwarz': 'Rohde & Schwarz',
                'siglent': 'Siglent',
                'keithley': 'Keithley',
                'fluke': 'Fluke',
                'lecroy': 'LeCroy',
                'teledyne': 'Teledyne LeCroy',
                'gw instek': 'GW Instek',
                'gwinstek': 'GW Instek',
                'yokogawa': 'Yokogawa',
                'anritsu': 'Anritsu',
                'picoscope': 'PicoScope',
                'stanford research': 'Stanford Research Systems',
                'national instruments': 'National Instruments',
                'ni.com': 'National Instruments'
            }
            
            for keyword, manufacturer in manufacturers.items():
                if keyword in content_lower or keyword in server.lower():
                    info["type"] = "Web Interface"
                    info["details"] = f"{manufacturer} Instrument"
                    
                    # Try to extract model information
                    title_match = re.search(r'<title>(.*?)</title>', response.text, re.IGNORECASE)
                    if title_match:
                        info["details"] += f" - {title_match.group(1)}"
                    
                    return info
            
            # Generic web interface detected
            if response.status_code == 200:
                info["type"] = "Web Interface"
                title_match = re.search(r'<title>(.*?)</title>', response.text, re.IGNORECASE)
                if title_match:
                    info["details"] = title_match.group(1)
                else:
                    info["details"] = f"HTTP {response.status_code}"
                return info
                
        except:
            continue
    
    return None

def try_scpi_over_telnet(ip, port=5024, timeout=2):
    """
    Try to connect via Telnet and send SCPI commands
    """
    try:
        tn = telnetlib.Telnet(ip, port, timeout)
        tn.write(b"*IDN?\n")
        time.sleep(0.5)
        response = tn.read_very_eager().decode('ascii').strip()
        tn.close()
        if response:
            return response
    except:
        pass
    return None

def identify_instrument_advanced(ip, port):
    """
    Try multiple methods to identify an instrument
    """
    rm = pyvisa.ResourceManager()
    identified = False
    identification = None
    
    print(f"\nDetailed scan of {ip}:{port}")
    print("-" * 40)
    
    # 1. Check if it's a web interface
    if port in [80, 8080, 443, 8443]:
        web_info = check_web_interface(ip, port)
        if web_info:
            print(f"  ✓ Web Interface: {web_info['details']}")
            identified = True
            identification = web_info['details']
    
    # 2. Try different SCPI ports if not already checked
    scpi_ports = [5025, 5024, 5030, 9001, 1234, 3490, 5555, 9221]
    if port not in scpi_ports:
        scpi_ports.insert(0, port)
    
    for scpi_port in scpi_ports:
        if identified:
            break
            
        # Try raw socket
        try:
            print(f"  Trying raw socket on port {scpi_port}...", end="")
            resource_str = f"TCPIP::{ip}::{scpi_port}::SOCKET"
            instrument = rm.open_resource(resource_str)
            instrument.timeout = 2000
            instrument.write_termination = '\n'
            instrument.read_termination = '\n'
            idn = instrument.query('*IDN?').strip()
            print(f" ✓")
            print(f"  ✓ Raw Socket (port {scpi_port}): {idn}")
            instrument.close()
            identified = True
            identification = idn
            break
        except:
            print(f" ✗")
    
    # 3. Try VXI-11 (standard port 111)
    if not identified:
        try:
            print(f"  Trying VXI-11...", end="")
            resource_str = f"TCPIP::{ip}::INSTR"
            instrument = rm.open_resource(resource_str)
            instrument.timeout = 2000
            idn = instrument.query('*IDN?').strip()
            print(f" ✓")
            print(f"  ✓ VXI-11: {idn}")
            instrument.close()
            identified = True
            identification = idn
        except:
            print(f" ✗")
    
    # 4. Try HiSLIP
    if not identified:
        try:
            print(f"  Trying HiSLIP...", end="")
            resource_str = f"TCPIP::{ip}::hislip0::INSTR"
            instrument = rm.open_resource(resource_str)
            instrument.timeout = 2000
            idn = instrument.query('*IDN?').strip()
            print(f" ✓")
            print(f"  ✓ HiSLIP: {idn}")
            instrument.close()
            identified = True
            identification = idn
        except:
            print(f" ✗")
    
    # 5. Try Telnet SCPI
    if not identified:
        for telnet_port in [5024, 5030, 23]:
            print(f"  Trying Telnet on port {telnet_port}...", end="")
            response = try_scpi_over_telnet(ip, telnet_port)
            if response:
                print(f" ✓")
                print(f"  ✓ Telnet (port {telnet_port}): {response}")
                identified = True
                identification = response
                break
            else:
                print(f" ✗")
    
    # 6. Try LXI discovery
    if not identified:
        try:
            print(f"  Trying LXI identification...", end="")
            lxi_url = f"http://{ip}:{port}/lxi/identification"
            response = requests.get(lxi_url, timeout=2, verify=False)
            if response.status_code == 200:
                print(f" ✓")
                print(f"  ✓ LXI Device detected")
                # Try to parse XML response
                if 'Manufacturer' in response.text:
                    identification = "LXI Device"
                identified = True
            else:
                print(f" ✗")
        except:
            print(f" ✗")
    
    if not identified:
        print(f"  ✗ Unable to identify instrument")
        print(f"  Suggestion: Device may require:")
        print(f"    - Specific driver or protocol")
        print(f"    - Authentication credentials")
        print(f"    - Manual configuration in VISA")
        print(f"    - Check device's network settings")
    
    return identification

def check_single_ip(ip, port, timeout=1):
    """
    Check if a single IP address has an instrument listening on the specified port
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((str(ip), port))
        sock.close()
        
        if result == 0:
            return ip
        return None
    except:
        return None

def scan_network_for_instruments(subnet="192.168.1.0/24", common_ports=None):
    """
    Scan a network subnet for instruments on common SCPI ports
    """
    if common_ports is None:
        # Extended list of common ports for network instruments
        common_ports = [
            5025,   # SCPI raw socket (most common)
            5030,   # SCPI telnet
            5024,   # Alternative SCPI
            111,    # VXI-11 (portmapper)
            1234,   # Some Rigol instruments
            3490,   # Some Tektronix instruments
            9001,   # Some instruments
            9221,   # Some Rohde & Schwarz instruments
            5555,   # Some Siglent/Rigol instruments
            8080,   # Web interface (many modern instruments)
            80,     # Web interface
            443,    # Secure web interface
            8443,   # Alternative secure web interface
            23,     # Telnet (older instruments)
        ]
    
    print(f"\nScanning subnet {subnet} for instruments...")
    print(f"Checking ports: {', '.join(map(str, common_ports))}")
    print("-" * 50)
    
    found_instruments = []
    
    try:
        # Parse the subnet
        network = ipaddress.ip_network(subnet, strict=False)
        total_hosts = sum(1 for _ in network.hosts())
        
        print(f"Scanning {total_hosts} IP addresses...")
        
        for port in common_ports:
            print(f"\nChecking port {port}...", end="", flush=True)
            
            with ThreadPoolExecutor(max_workers=100) as executor:
                # Submit all IP checks for this port
                futures = {executor.submit(check_single_ip, ip, port, 0.5): ip 
                          for ip in network.hosts()}
                
                # Collect results
                found_count = 0
                for future in as_completed(futures):
                    ip = future.result()
                    if ip:
                        if found_count == 0:
                            print()  # New line after "Checking port..."
                        print(f"  Found device at {ip}:{port}")
                        found_instruments.append((str(ip), port))
                        found_count += 1
                
                if found_count == 0:
                    print(" (none found)")
        
        # Try to identify found instruments
        if found_instruments:
            print(f"\n{'='*50}")
            print(f"Attempting to identify {len(found_instruments)} found device(s):")
            print("="*50)
            
            # Group by IP for more efficient identification
            ip_ports = {}
            for ip, port in found_instruments:
                if ip not in ip_ports:
                    ip_ports[ip] = []
                ip_ports[ip].append(port)
            
            for ip, ports in ip_ports.items():
                print(f"\nDevice at {ip} (responding on ports: {', '.join(map(str, ports))})")
                
                # Try to identify using the most promising port
                if 8080 in ports or 80 in ports or 443 in ports:
                    # Likely has web interface
                    web_port = 8080 if 8080 in ports else (80 if 80 in ports else 443)
                    identify_instrument_advanced(ip, web_port)
                elif 5025 in ports:
                    # Standard SCPI port
                    identify_instrument_advanced(ip, 5025)
                else:
                    # Try the first available port
                    identify_instrument_advanced(ip, ports[0])
        
        else:
            print("\nNo instruments found on the network")
            
    except Exception as e:
        print(f"Error during network scan: {str(e)}")

def detect_local_network():
    """
    Try to detect the local network subnet automatically
    """
    try:
        # Get local IP address
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        
        # Convert to /24 subnet (common for home/lab networks)
        ip_parts = local_ip.split('.')
        subnet = f"{ip_parts[0]}.{ip_parts[1]}.{ip_parts[2]}.0/24"
        
        return subnet, local_ip
    except:
        return "192.168.1.0/24", "Unknown"

def manual_instrument_connect(ip):
    """
    Manually try to connect to a specific instrument with detailed diagnostics
    """
    print(f"\nManual connection attempt to {ip}")
    print("="*50)
    
    identify_instrument_advanced(ip, 8080)

if __name__ == "__main__":
    print("GPIB and LAN Instrument Scanner (Enhanced)")
    print("=" * 50)
    
    # Check for required packages
    try:
        import requests
    except ImportError:
        print("Warning: 'requests' library not found. Web interface detection will be limited.")
        print("Install it with: pip install requests")
    
    # Method 1: Use VISA resource discovery for GPIB
    find_gpib_devices()
    
    # Method 2: Scan GPIB addresses directly
    #scan_gpib_addresses()
    
    # Method 3: Find LAN devices via VISA
    find_lan_devices()
    
    # Ask user if they want to do network scanning
    print("\n" + "=" * 50)
    response = input("\nDo you want to scan the local network for instruments? (y/n): ").strip().lower()
    
    if response == 'y':
        # Detect local network
        subnet, local_ip = detect_local_network()
        print(f"\nLocal IP: {local_ip}")
        print(f"Detected subnet: {subnet}")
        
        # Ask if user wants to use a different subnet
        custom = input(f"Press Enter to scan {subnet} or enter a different subnet (e.g., 192.168.0.0/24): ").strip()
        if custom:
            subnet = custom
        
        # Method 4: Network scan
        scan_network_for_instruments(subnet)
    
    # Manual connection option for problematic devices
    print("\n" + "=" * 50)
    response = input("\nDo you have a specific instrument IP to diagnose? (y/n): ").strip().lower()
    
    if response == 'y':
        ip = input("Enter the IP address (e.g., 192.168.1.121): ").strip()
        if ip:
            manual_instrument_connect(ip)
    
    print("\nScan complete.")