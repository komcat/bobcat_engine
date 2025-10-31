import pyvisa
import socket
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import time
import queue
from datetime import datetime
import argparse
import sys
import base64
from io import BytesIO

# Try to import pystray for system tray functionality
try:
    import pystray
    from pystray import MenuItem as item
    from PIL import Image, ImageDraw
    TRAY_AVAILABLE = True
except ImportError:
    TRAY_AVAILABLE = False
    print("Warning: pystray and/or PIL not available. System tray functionality disabled.")
    print("Install with: pip install pystray pillow")

class CLD101xServerGUI:
    def __init__(self, auto_start=False):
        self.root = tk.Tk()
        self.root.title("CLD101x Server Control Panel")
        self.root.geometry("800x600")
        
        # Server state
        self.server_running = False
        self.server_thread = None
        self.server_socket = None
        self.client_count = 0
        self.total_commands = 0
        self.auto_start = auto_start
        
        # VISA instrument
        self.instr = None
        self.rm = None
        
        # Communication queues
        self.log_queue = queue.Queue()
        self.status_queue = queue.Queue()
        
        # System tray
        self.tray_icon = None
        self.is_minimized_to_tray = False
        
        # Server settings
        self.host = tk.StringVar(value="127.0.0.88")
        self.port = tk.IntVar(value=65432)
        self.visa_resource = tk.StringVar(value="USB0::0x1313::0x804F::M00930341::INSTR")
        
        # Current readings
        self.current_temp = tk.StringVar(value="--")
        self.current_laser = tk.StringVar(value="--")
        self.laser_state = tk.StringVar(value="--")
        self.tec_state = tk.StringVar(value="--")
        
        self.setup_ui()
        self.setup_system_tray()
        self.start_log_updater()
        
        # Configure window close behavior
        self.root.protocol("WM_DELETE_WINDOW", self.on_window_close)
        
        # Auto-start functionality
        if self.auto_start:
            self.root.after(1000, self.auto_start_sequence)
        
    def create_tray_icon(self):
        """Create a simple icon for the system tray"""
        # Create a simple 64x64 icon
        width = 64
        height = 64
        
        # Create image with transparent background
        image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        # Draw a simple server icon
        if self.server_running:
            # Green circle for running server
            draw.ellipse([8, 8, 56, 56], fill=(0, 255, 0, 255), outline=(0, 200, 0, 255), width=2)
            # Add "S" for Server
            draw.text((24, 20), "S", fill=(255, 255, 255, 255))
        else:
            # Red circle for stopped server
            draw.ellipse([8, 8, 56, 56], fill=(255, 0, 0, 255), outline=(200, 0, 0, 255), width=2)
            # Add "S" for Server
            draw.text((24, 20), "S", fill=(255, 255, 255, 255))
            
        return image
    
    def setup_system_tray(self):
        """Setup system tray icon and menu"""
        if not TRAY_AVAILABLE:
            return
            
        try:
            # Create tray menu
            menu = pystray.Menu(
                item('Show Window', self.show_window),
                item('Hide Window', self.hide_window),
                pystray.Menu.SEPARATOR,
                item('Server Status', self.show_server_status),
                item('Start Server', self.start_server_from_tray, enabled=lambda item: not self.server_running),
                item('Stop Server', self.stop_server_from_tray, enabled=lambda item: self.server_running),
                pystray.Menu.SEPARATOR,
                item('Exit', self.quit_application)
            )
            
            # Create tray icon
            icon_image = self.create_tray_icon()
            self.tray_icon = pystray.Icon("CLD101x Server", icon_image, menu=menu)
            
        except Exception as e:
            print(f"Failed to setup system tray: {e}")
            self.tray_icon = None
    
    def start_tray_icon(self):
        """Start the system tray icon in a separate thread"""
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                tray_thread = threading.Thread(target=self.tray_icon.run, daemon=True)
                tray_thread.start()
            except Exception as e:
                print(f"Failed to start tray icon: {e}")
    
    def update_tray_icon(self):
        """Update the tray icon to reflect server status"""
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                new_icon = self.create_tray_icon()
                self.tray_icon.icon = new_icon
                
                # Update tooltip
                status = "Running" if self.server_running else "Stopped"
                self.tray_icon.title = f"CLD101x Server - {status}"
            except Exception as e:
                print(f"Failed to update tray icon: {e}")
    
    def show_window(self, icon=None, item=None):
        """Show the main window"""
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()
        self.is_minimized_to_tray = False
    
    def hide_window(self, icon=None, item=None):
        """Hide the main window to system tray"""
        if TRAY_AVAILABLE and self.tray_icon:
            self.root.withdraw()
            self.is_minimized_to_tray = True
            
            # Show notification on first minimize
            if not hasattr(self, '_first_minimize_done'):
                self._first_minimize_done = True
                self.show_tray_notification("CLD101x Server minimized to system tray")
        else:
            messagebox.showwarning("System Tray Not Available", 
                                 "System tray functionality is not available.\n"
                                 "Install pystray and pillow: pip install pystray pillow")
    
    def show_tray_notification(self, message):
        """Show a system tray notification"""
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                self.tray_icon.notify(message, "CLD101x Server")
            except Exception as e:
                print(f"Failed to show notification: {e}")
    
    def show_server_status(self, icon=None, item=None):
        """Show server status in tray notification"""
        status_msg = f"Server: {'Running' if self.server_running else 'Stopped'}\n"
        status_msg += f"Clients: {self.client_count}\n"
        status_msg += f"Commands: {self.total_commands}"
        
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                self.tray_icon.notify(status_msg, "CLD101x Server Status")
            except Exception as e:
                print(f"Failed to show status notification: {e}")
    
    def start_server_from_tray(self, icon=None, item=None):
        """Start server from tray menu"""
        self.start_server()
        self.show_tray_notification("Server started")
    
    def stop_server_from_tray(self, icon=None, item=None):
        """Stop server from tray menu"""
        self.stop_server()
        self.show_tray_notification("Server stopped")
    
    def quit_application(self, icon=None, item=None):
        """Quit the entire application"""
        self.is_minimized_to_tray = False
        self.on_closing()
    
    def on_window_close(self):
        """Handle window close button - minimize to tray instead of closing"""
        if TRAY_AVAILABLE and self.tray_icon:
            self.hide_window()
        else:
            # If tray not available, ask user
            result = messagebox.askyesnocancel(
                "Close Application",
                "System tray is not available.\n\n"
                "Yes: Exit application\n"
                "No: Minimize to taskbar\n"
                "Cancel: Keep window open"
            )
            
            if result is True:  # Yes - exit
                self.on_closing()
            elif result is False:  # No - minimize to taskbar
                self.root.iconify()
            # Cancel - do nothing
    
    def auto_start_sequence(self):
        """Automatically start the server after GUI initialization"""
        self.log_message("Auto-start sequence initiated...")
        
        # First, test VISA connection
        try:
            self.test_visa()
            if self.instr is not None:
                # If VISA connection successful, start server
                self.log_message("VISA connection successful, starting server...")
                self.start_server()
                self.log_message("Server auto-started successfully!")
            else:
                self.log_message("Auto-start failed: Could not establish VISA connection", "ERROR")
        except Exception as e:
            self.log_message(f"Auto-start failed: {e}", "ERROR")
    
    def setup_ui(self):
        # Main notebook for tabs
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill='both', expand=True, padx=5, pady=5)
        
        # Server Control Tab
        server_frame = ttk.Frame(notebook)
        notebook.add(server_frame, text="Server Control")
        self.setup_server_tab(server_frame)
        
        # Instrument Status Tab
        status_frame = ttk.Frame(notebook)
        notebook.add(status_frame, text="Instrument Status")
        self.setup_status_tab(status_frame)
        
        # Logs Tab
        log_frame = ttk.Frame(notebook)
        notebook.add(log_frame, text="Activity Logs")
        self.setup_log_tab(log_frame)
        
    def setup_server_tab(self, parent):
        # System tray info
        if TRAY_AVAILABLE:
            tray_frame = ttk.Frame(parent)
            tray_frame.pack(fill='x', padx=5, pady=5)
            ttk.Label(tray_frame, text="💾 System Tray: Available - Close window to minimize to tray", 
                     foreground="blue", font=('Arial', 9)).pack()
        else:
            tray_frame = ttk.Frame(parent)
            tray_frame.pack(fill='x', padx=5, pady=5)
            ttk.Label(tray_frame, text="❌ System Tray: Not Available (pip install pystray pillow)", 
                     foreground="red", font=('Arial', 9)).pack()
        
        # Auto-start indicator
        if self.auto_start:
            auto_frame = ttk.Frame(parent)
            auto_frame.pack(fill='x', padx=5, pady=5)
            ttk.Label(auto_frame, text="⚡ AUTO-START MODE ENABLED", 
                     foreground="green", font=('Arial', 10, 'bold')).pack()
        
        # Connection Settings
        settings_frame = ttk.LabelFrame(parent, text="Connection Settings")
        settings_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Label(settings_frame, text="Host:").grid(row=0, column=0, sticky='w', padx=5, pady=2)
        ttk.Entry(settings_frame, textvariable=self.host, width=15).grid(row=0, column=1, padx=5, pady=2)
        
        ttk.Label(settings_frame, text="Port:").grid(row=0, column=2, sticky='w', padx=5, pady=2)
        ttk.Entry(settings_frame, textvariable=self.port, width=8).grid(row=0, column=3, padx=5, pady=2)
        
        ttk.Label(settings_frame, text="VISA Resource:").grid(row=1, column=0, sticky='w', padx=5, pady=2)
        ttk.Entry(settings_frame, textvariable=self.visa_resource, width=40).grid(row=1, column=1, columnspan=3, sticky='ew', padx=5, pady=2)
        
        # Server Control
        control_frame = ttk.LabelFrame(parent, text="Server Control")
        control_frame.pack(fill='x', padx=5, pady=5)
        
        self.start_btn = ttk.Button(control_frame, text="Start Server", command=self.start_server)
        self.start_btn.pack(side='left', padx=5, pady=5)
        
        self.stop_btn = ttk.Button(control_frame, text="Stop Server", command=self.stop_server, state='disabled')
        self.stop_btn.pack(side='left', padx=5, pady=5)
        
        ttk.Button(control_frame, text="Test VISA Connection", command=self.test_visa).pack(side='left', padx=5, pady=5)
        
        ttk.Button(control_frame, text="Restart Server", command=self.restart_server).pack(side='left', padx=5, pady=5)
        
        # Tray control buttons
        if TRAY_AVAILABLE:
            ttk.Button(control_frame, text="Hide to Tray", command=self.hide_window).pack(side='right', padx=5, pady=5)
        
        # Server Status
        status_frame = ttk.LabelFrame(parent, text="Server Status")
        status_frame.pack(fill='both', expand=True, padx=5, pady=5)
        
        self.status_label = ttk.Label(status_frame, text="Server: Stopped", font=('Arial', 12, 'bold'))
        self.status_label.pack(pady=5)
        
        self.client_label = ttk.Label(status_frame, text="Active Clients: 0")
        self.client_label.pack(pady=2)
        
        self.command_label = ttk.Label(status_frame, text="Total Commands: 0")
        self.command_label.pack(pady=2)
        
        self.visa_label = ttk.Label(status_frame, text="VISA: Not Connected")
        self.visa_label.pack(pady=2)
        
    def setup_status_tab(self, parent):
        # Current Readings
        readings_frame = ttk.LabelFrame(parent, text="Current Readings")
        readings_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Label(readings_frame, text="Temperature:").grid(row=0, column=0, sticky='w', padx=5, pady=5)
        ttk.Label(readings_frame, textvariable=self.current_temp, font=('Arial', 12, 'bold')).grid(row=0, column=1, sticky='w', padx=5, pady=5)
        ttk.Label(readings_frame, text="°C").grid(row=0, column=2, sticky='w', padx=2, pady=5)
        
        ttk.Label(readings_frame, text="Laser Current:").grid(row=1, column=0, sticky='w', padx=5, pady=5)
        ttk.Label(readings_frame, textvariable=self.current_laser, font=('Arial', 12, 'bold')).grid(row=1, column=1, sticky='w', padx=5, pady=5)
        ttk.Label(readings_frame, text="A").grid(row=1, column=2, sticky='w', padx=2, pady=5)
        
        # States
        states_frame = ttk.LabelFrame(parent, text="Device States")
        states_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Label(states_frame, text="Laser:").grid(row=0, column=0, sticky='w', padx=5, pady=5)
        ttk.Label(states_frame, textvariable=self.laser_state, font=('Arial', 12, 'bold')).grid(row=0, column=1, sticky='w', padx=5, pady=5)
        
        ttk.Label(states_frame, text="TEC:").grid(row=1, column=0, sticky='w', padx=5, pady=5)
        ttk.Label(states_frame, textvariable=self.tec_state, font=('Arial', 12, 'bold')).grid(row=1, column=1, sticky='w', padx=5, pady=5)
        
        # Manual Control
        control_frame = ttk.LabelFrame(parent, text="Manual Control")
        control_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Button(control_frame, text="Read Temperature", command=self.read_temperature).pack(side='left', padx=5, pady=5)
        ttk.Button(control_frame, text="Read Current", command=self.read_current).pack(side='left', padx=5, pady=5)
        ttk.Button(control_frame, text="Update All", command=self.update_readings).pack(side='left', padx=5, pady=5)
        
    def setup_log_tab(self, parent):
        # Log display
        self.log_text = scrolledtext.ScrolledText(parent, height=20, width=80)
        self.log_text.pack(fill='both', expand=True, padx=5, pady=5)
        
        # Log controls
        control_frame = ttk.Frame(parent)
        control_frame.pack(fill='x', padx=5, pady=5)
        
        ttk.Button(control_frame, text="Clear Logs", command=self.clear_logs).pack(side='left', padx=5)
        
        self.auto_scroll = tk.BooleanVar(value=True)
        ttk.Checkbutton(control_frame, text="Auto-scroll", variable=self.auto_scroll).pack(side='left', padx=5)
        
    def restart_server(self):
        """Restart the server"""
        self.log_message("Restarting server...")
        if self.server_running:
            self.stop_server()
            self.root.after(1000, self.start_server)
        else:
            self.start_server()
        
    def log_message(self, message, level="INFO"):
        timestamp = datetime.now().strftime("%H:%M:%S")
        log_entry = f"[{timestamp}] {level}: {message}\n"
        self.log_queue.put(log_entry)
        print(f"[{timestamp}] {level}: {message}")
        
    def start_log_updater(self):
        """Update logs from queue in main thread"""
        try:
            while True:
                log_entry = self.log_queue.get_nowait()
                self.log_text.insert(tk.END, log_entry)
                if self.auto_scroll.get():
                    self.log_text.see(tk.END)
        except queue.Empty:
            pass
        
        self.root.after(100, self.start_log_updater)
        
    def test_visa(self):
        try:
            if self.rm is None:
                self.rm = pyvisa.ResourceManager()
            
            if self.instr is not None:
                self.instr.close()
                
            self.instr = self.rm.open_resource(self.visa_resource.get())
            self.instr.timeout = 1000
            
            idn = self.instr.query("*IDN?")
            self.visa_label.config(text=f"VISA: Connected - {idn.strip()}")
            self.log_message(f"VISA connected: {idn.strip()}")
            
            self.update_readings()
            
        except Exception as e:
            self.visa_label.config(text="VISA: Connection Failed")
            self.log_message(f"VISA connection failed: {e}", "ERROR")
            
    def start_server(self):
        if self.server_running:
            return
            
        try:
            if self.instr is None:
                self.test_visa()
                if self.instr is None:
                    self.log_message("Cannot start server: VISA not connected", "ERROR")
                    return
            
            self.server_thread = threading.Thread(target=self.server_worker, daemon=True)
            self.server_running = True
            self.server_thread.start()
            
            self.start_btn.config(state='disabled')
            self.stop_btn.config(state='normal')
            self.status_label.config(text="Server: Running")
            
            # Update tray icon
            self.update_tray_icon()
            
            self.log_message(f"Server started on {self.host.get()}:{self.port.get()}")
            
        except Exception as e:
            self.log_message(f"Failed to start server: {e}", "ERROR")
            
    def stop_server(self):
        if not self.server_running:
            return
            
        self.server_running = False
        
        if self.server_socket:
            self.server_socket.close()
            
        self.start_btn.config(state='normal')
        self.stop_btn.config(state='disabled')
        self.status_label.config(text="Server: Stopped")
        
        # Update tray icon
        self.update_tray_icon()
        
        self.log_message("Server stopped")
        
    def server_worker(self):
        """Server worker thread"""
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host.get(), self.port.get()))
            self.server_socket.listen()
            
            while self.server_running:
                try:
                    self.server_socket.settimeout(1.0)
                    conn, addr = self.server_socket.accept()
                    
                    client_thread = threading.Thread(target=self.handle_client, args=(conn, addr), daemon=True)
                    client_thread.start()
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    if self.server_running:
                        self.log_message(f"Server error: {e}", "ERROR")
                    break
                    
        except Exception as e:
            self.log_message(f"Server startup error: {e}", "ERROR")
        finally:
            if self.server_socket:
                self.server_socket.close()
                
    def handle_client(self, conn, addr):
        """Handle individual client connection"""
        self.client_count += 1
        self.root.after(0, lambda: self.client_label.config(text=f"Active Clients: {self.client_count}"))
        self.log_message(f"Client connected: {addr}")
        
        try:
            while self.server_running:
                try:
                    conn.settimeout(1.0)
                    data = conn.recv(1024).decode()
                    if not data:
                        break
                        
                    response = self.process_command(data.strip())
                    conn.sendall(response.encode())
                    
                    self.total_commands += 1
                    self.root.after(0, lambda: self.command_label.config(text=f"Total Commands: {self.total_commands}"))
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    self.log_message(f"Client {addr} error: {e}", "ERROR")
                    break
                    
        finally:
            conn.close()
            self.client_count -= 1
            self.root.after(0, lambda: self.client_label.config(text=f"Active Clients: {self.client_count}"))
            self.log_message(f"Client disconnected: {addr}")
    
    def process_command(self, data):
        """Process incoming command and return response"""
        self.log_message(f"Command: {data}")
        
        parts = data.split()
        command = parts[0] if parts else ""
        value = parts[1] if len(parts) > 1 else None
        
        try:
            # EXISTING COMMANDS
            if command == "SET_LASER_CURRENT" and value is not None:
                self.instr.write(f"source1:current:level:amplitude {value}")
                response = f"Set LD current [A]: {self.instr.query('source1:current:level:amplitude?')}"
                
            elif command == "SET_TEC_TEMPERATURE" and value is not None:
                self.instr.write(f"source2:temperature:spoint {value}")
                response = f"Set TEC temperature [C]: {self.instr.query('source2:temperature:spoint?')}"
                
            elif command == "LASER_ON":
                self.instr.write("output1:state on")
                response = f"Laser state: {self.instr.query('output1:state?')}"
                self.root.after(0, lambda: self.laser_state.set(response.split(': ')[1]))
                
            elif command == "LASER_OFF":
                self.instr.write("output1:state off")
                response = f"Laser state: {self.instr.query('output1:state?')}"
                self.root.after(0, lambda: self.laser_state.set(response.split(': ')[1]))
                
            elif command == "TEC_ON":
                self.instr.write("output2:state on")
                response = f"TEC state: {self.instr.query('output2:state?')}"
                self.root.after(0, lambda: self.tec_state.set(response.split(': ')[1]))
                
            elif command == "TEC_OFF":
                self.instr.write("output2:state off")
                response = f"TEC state: {self.instr.query('output2:state?')}"
                self.root.after(0, lambda: self.tec_state.set(response.split(': ')[1]))
                
            elif command == "READ_LASER_CURRENT":
                current = self.instr.query("sense3:current:dc:data?")
                response = f"Current laser current [A]: {current}"
                self.root.after(0, lambda: self.current_laser.set(current.strip()))
                
            elif command == "READ_TEC_TEMPERATURE":
                try:
                    original_timeout = self.instr.timeout
                    self.instr.timeout = 200
                    temp = self.instr.query("SENSe2:temperature:data?")
                    response = f"Current TEC temperature [C]: {temp}"
                    self.root.after(0, lambda: self.current_temp.set(temp.strip()))
                except pyvisa.errors.VisaIOError as e:
                    if "timeout" in str(e).lower():
                        response = "ERROR: Temperature reading timeout"
                    else:
                        response = f"ERROR: VISA error - {str(e)}"
                finally:
                    self.instr.timeout = original_timeout
            
            # STATUS QUERY COMMANDS
            elif command in ["OUTPUT1:STATE?", "OUTPut1:STATe?", "output1:state?"]:
                try:
                    state = self.instr.query("output1:state?")
                    state_clean = state.strip().upper()
                    self.log_message(f"Raw laser state from hardware: '{state}' -> '{state_clean}'")
                    
                    if state_clean in ['1', 'ON']:
                        response = "ON"
                        self.root.after(0, lambda: self.laser_state.set("ON"))
                    else:
                        response = "OFF"
                        self.root.after(0, lambda: self.laser_state.set("OFF"))
                        
                except Exception as e:
                    self.log_message(f"Failed to query laser state: {e}", "ERROR")
                    try:
                        current = float(self.instr.query("sense3:current:dc:data?").strip())
                        if current > 0.001:
                            response = "ON"
                            self.log_message(f"Inferred laser ON from current: {current}A")
                        else:
                            response = "OFF"
                            self.log_message(f"Inferred laser OFF from current: {current}A")
                    except:
                        response = "OFF"
                        
            elif command in ["OUTPUT2:STATE?", "OUTPut2:STATe?", "output2:state?"]:
                try:
                    state = self.instr.query("output2:state?")
                    state_clean = state.strip().upper()
                    self.log_message(f"Raw TEC state from hardware: '{state}' -> '{state_clean}'")
                    
                    if state_clean in ['1', 'ON']:
                        response = "ON"
                        self.root.after(0, lambda: self.tec_state.set("ON"))
                    else:
                        response = "OFF"
                        self.root.after(0, lambda: self.tec_state.set("OFF"))
                        
                except Exception as e:
                    self.log_message(f"Failed to query TEC state: {e}", "ERROR")
                    try:
                        temp = float(self.instr.query("SENSe2:temperature:data?").strip())
                        setpoint = float(self.instr.query("source2:temperature:spoint?").strip())
                        temp_diff = abs(temp - setpoint)
                        
                        if temp_diff < 2.0:
                            response = "ON"
                            self.log_message(f"Inferred TEC ON - Temp: {temp}°C, Setpoint: {setpoint}°C")
                        else:
                            response = "OFF"
                            self.log_message(f"Inferred TEC OFF - Temp: {temp}°C, Setpoint: {setpoint}°C")
                    except:
                        response = "OFF"
            
            # INSTRUMENT IDENTIFICATION
            elif command in ["*IDN?", "IDN?"]:
                try:
                    response = self.instr.query("*IDN?")
                except Exception as e:
                    response = f"ERROR: Failed to query instrument ID - {str(e)}"
            
            # ERROR QUEUE QUERY
            elif command in ["SYST:ERR?", "SYSTEM:ERROR?"]:
                try:
                    response = self.instr.query("system:error?")
                except Exception as e:
                    response = f"ERROR: Failed to query error queue - {str(e)}"
            
            # GENERIC SCPI QUERY PASSTHROUGH
            elif command.endswith('?'):
                try:
                    response = self.instr.query(command)
                    self.log_message(f"Generic SCPI query: {command} -> {response}")
                except Exception as e:
                    response = f"ERROR: Query failed - {str(e)}"
                    
            else:
                response = "Unknown command or missing value"
                
        except pyvisa.errors.VisaIOError as e:
            response = f"ERROR: VISA communication error - {str(e)}"
            self.log_message(f"VISA error: {e}", "ERROR")
            
        except Exception as e:
            response = f"ERROR: {str(e)}"
            self.log_message(f"Command error: {e}", "ERROR")
            
        self.log_message(f"Response: {response}")
        return response
        
    def read_temperature(self):
        """Manual temperature reading"""
        if self.instr is None:
            self.log_message("VISA not connected", "ERROR")
            return
            
        try:
            original_timeout = self.instr.timeout
            self.instr.timeout = 200
            temp = self.instr.query("SENSe2:temperature:data?")
            self.current_temp.set(temp.strip())
            self.log_message(f"Temperature: {temp.strip()} °C")
        except Exception as e:
            self.log_message(f"Failed to read temperature: {e}", "ERROR")
        finally:
            self.instr.timeout = original_timeout
            
    def read_current(self):
        """Manual current reading"""
        if self.instr is None:
            self.log_message("VISA not connected", "ERROR")
            return
            
        try:
            current = self.instr.query("sense3:current:dc:data?")
            self.current_laser.set(current.strip())
            self.log_message(f"Current: {current.strip()} A")
        except Exception as e:
            self.log_message(f"Failed to read current: {e}", "ERROR")
            
    def update_readings(self):
        """Update all readings"""
        self.read_temperature()
        self.read_current()
        
        try:
            laser_state = self.instr.query("output1:state?")
            self.laser_state.set(laser_state.strip())
            
            tec_state = self.instr.query("output2:state?")
            self.tec_state.set(tec_state.strip())
        except Exception as e:
            self.log_message(f"Failed to read states: {e}", "ERROR")
            
    def clear_logs(self):
        """Clear log display"""
        self.log_text.delete(1.0, tk.END)
        
    def run(self):
        """Start the GUI application"""
        # Start tray icon
        self.start_tray_icon()
        
        # Start GUI
        self.root.protocol("WM_DELETE_WINDOW", self.on_window_close)
        self.root.mainloop()
        
    def on_closing(self):
        """Clean shutdown"""
        self.stop_server()
        
        # Stop tray icon
        if self.tray_icon and TRAY_AVAILABLE:
            try:
                self.tray_icon.stop()
            except:
                pass
        
        if self.instr:
            try:
                self.instr.close()
            except:
                pass
                
        if self.rm:
            try:
                self.rm.close()
            except:
                pass
                
        self.root.destroy()

def main():
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='CLD101x Server Control Panel')
    parser.add_argument('--auto-start', '-a', action='store_true', 
                       help='Automatically start the server when the application launches')
    parser.add_argument('--headless', action='store_true',
                       help='Run without GUI (headless mode)')
    parser.add_argument('--host', default='127.0.0.11',
                       help='Server host address (default: 127.0.0.11)')
    parser.add_argument('--port', type=int, default=65432,
                       help='Server port (default: 65432)')
    parser.add_argument('--visa-resource', default='USB0::0x1313::0x804F::M00930341::INSTR',
                       help='VISA resource string')
    
    args = parser.parse_args()
    
    if args.headless:
        # Headless mode - run server without GUI
        print("Starting CLD101x server in headless mode...")
        run_headless_server(args.host, args.port, args.visa_resource)
    else:
        # GUI mode
        app = CLD101xServerGUI(auto_start=args.auto_start)
        
        # Override default settings if provided via command line
        if args.host != '127.0.0.11':
            app.host.set(args.host)
        if args.port != 65432:
            app.port.set(args.port)
        if args.visa_resource != 'USB0::0x1313::0x804F::M00930341::INSTR':
            app.visa_resource.set(args.visa_resource)
            
        app.run()

def run_headless_server(host, port, visa_resource):
    """Run server without GUI (headless mode)"""
    import signal
    
    print(f"Initializing VISA connection to {visa_resource}...")
    try:
        rm = pyvisa.ResourceManager()
        instr = rm.open_resource(visa_resource)
        instr.timeout = 1000
        
        idn = instr.query("*IDN?")
        print(f"Connected to: {idn.strip()}")
        
    except Exception as e:
        print(f"VISA connection failed: {e}")
        return
    
    print(f"Starting server on {host}:{port}...")
    
    # Server implementation for headless mode
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    def signal_handler(sig, frame):
        print("\nShutting down server...")
        server_socket.close()
        instr.close()
        rm.close()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    
    try:
        server_socket.bind((host, port))
        server_socket.listen()
        print(f"Server listening on {host}:{port}")
        print("Press Ctrl+C to stop the server")
        
        while True:
            conn, addr = server_socket.accept()
            print(f"Client connected: {addr}")
            
            # Handle client in separate thread
            client_thread = threading.Thread(
                target=lambda: handle_headless_client(conn, addr, instr), 
                daemon=True
            )
            client_thread.start()
            
    except Exception as e:
        print(f"Server error: {e}")
    finally:
        server_socket.close()
        instr.close()
        rm.close()

def handle_headless_client(conn, addr, instr):
    """Handle client in headless mode"""
    try:
        while True:
            data = conn.recv(1024).decode()
            if not data:
                break
                
            print(f"Command from {addr}: {data.strip()}")
            
            # Process command (same logic as GUI version)
            parts = data.split()
            command = parts[0] if parts else ""
            value = parts[1] if len(parts) > 1 else None
            
            try:
                # EXISTING COMMANDS
                if command == "SET_LASER_CURRENT" and value is not None:
                    instr.write(f"source1:current:level:amplitude {value}")
                    response = f"Set LD current [A]: {instr.query('source1:current:level:amplitude?')}"
                elif command == "SET_TEC_TEMPERATURE" and value is not None:
                    instr.write(f"source2:temperature:spoint {value}")
                    response = f"Set TEC temperature [C]: {instr.query('source2:temperature:spoint?')}"
                elif command == "LASER_ON":
                    instr.write("output1:state on")
                    response = f"Laser state: {instr.query('output1:state?')}"
                elif command == "LASER_OFF":
                    instr.write("output1:state off")
                    response = f"Laser state: {instr.query('output1:state?')}"
                elif command == "TEC_ON":
                    instr.write("output2:state on")
                    response = f"TEC state: {instr.query('output2:state?')}"
                elif command == "TEC_OFF":
                    instr.write("output2:state off")
                    response = f"TEC state: {instr.query('output2:state?')}"
                elif command == "READ_LASER_CURRENT":
                    current = instr.query("sense3:current:dc:data?")
                    response = f"Current laser current [A]: {current}"
                elif command == "READ_TEC_TEMPERATURE":
                    try:
                        original_timeout = instr.timeout
                        instr.timeout = 200
                        temp = instr.query("SENSe2:temperature:data?")
                        response = f"Current TEC temperature [C]: {temp}"
                    except pyvisa.errors.VisaIOError as e:
                        if "timeout" in str(e).lower():
                            response = "ERROR: Temperature reading timeout"
                        else:
                            response = f"ERROR: VISA error - {str(e)}"
                    finally:
                        instr.timeout = original_timeout
                
                # STATUS QUERY COMMANDS FOR HEADLESS MODE
                elif command in ["OUTPUT1:STATE?", "OUTPut1:STATe?", "output1:state?"]:
                    try:
                        state = instr.query("output1:state?")
                        state_clean = state.strip().upper()
                        if state_clean in ['1', 'ON']:
                            response = "ON"
                        else:
                            response = "OFF"
                    except Exception as e:
                        print(f"Failed to query laser state: {e}")
                        # Fallback inference
                        try:
                            current = float(instr.query("sense3:current:dc:data?").strip())
                            response = "ON" if current > 0.001 else "OFF"
                        except:
                            response = "OFF"
                            
                elif command in ["OUTPUT2:STATE?", "OUTPut2:STATe?", "output2:state?"]:
                    try:
                        state = instr.query("output2:state?")
                        state_clean = state.strip().upper()
                        if state_clean in ['1', 'ON']:
                            response = "ON"
                        else:
                            response = "OFF"
                    except Exception as e:
                        print(f"Failed to query TEC state: {e}")
                        # Fallback inference
                        try:
                            temp = float(instr.query("SENSe2:temperature:data?").strip())
                            setpoint = float(instr.query("source2:temperature:spoint?").strip())
                            response = "ON" if abs(temp - setpoint) < 2.0 else "OFF"
                        except:
                            response = "OFF"
                
                elif command in ["*IDN?", "IDN?"]:
                    response = instr.query("*IDN?")
                elif command in ["SYST:ERR?", "SYSTEM:ERROR?"]:
                    response = instr.query("system:error?")
                elif command.endswith('?'):
                    response = instr.query(command)
                else:
                    response = "Unknown command or missing value"
                    
            except pyvisa.errors.VisaIOError as e:
                response = f"ERROR: VISA communication error - {str(e)}"
            except Exception as e:
                response = f"ERROR: {str(e)}"
                
            print(f"Response to {addr}: {response}")
            conn.sendall(response.encode())
            
    except Exception as e:
        print(f"Client {addr} error: {e}")
    finally:
        conn.close()
        print(f"Client {addr} disconnected")

if __name__ == "__main__":
    main()