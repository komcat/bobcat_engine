"""Main application window with alignment optimization support."""

import threading
import time
from PyQt5.QtWidgets import (QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                           QPushButton, QLabel, QStatusBar, QMessageBox,
                           QSystemTrayIcon, QApplication, QGroupBox, 
                           QGridLayout, QComboBox, QProgressBar)
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtGui import QFont

# Import the optimized classes
from core.alignment_collector import AlignmentDataCollector
from core.server import DataServer
from hardware.smart_multimeter import SmartKeithleyMultimeter
from ui.alignment_plotter import AlignmentPlotter  # New specialized plotter
from ui.system_tray import SystemTrayIcon
import config


class MainWindow(QMainWindow):
    """Main window for alignment system with dual channel support."""
    
    def __init__(self):
        """Initialize the main window."""
        super().__init__()
        
        # Initialize components
        self.data_collector = None
        self.ch1_server = None
        self.ch2_server = None
        self.multimeter = None
        self.is_closing = False
        
        # Performance tracking
        self._last_ch1_count = 0
        self._last_ch2_count = 0
        
        # Setup UI
        self._init_ui()
        self._setup_system_tray()
        self._setup_servers()
        self._setup_multimeter()
    
    def _init_ui(self):
        """Initialize the user interface with alignment features."""
        self.setWindowTitle(config.APP_NAME)
        self.setGeometry(100, 100, 1400, 900)
        
        # Central widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Main layout
        main_layout = QVBoxLayout(central_widget)
        
        # === Control Panel ===
        control_group = QGroupBox("Control Panel")
        control_layout = QHBoxLayout()
        
        # Mode selection
        mode_label = QLabel("Mode:")
        control_layout.addWidget(mode_label)
        
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["SEARCH", "COARSE", "FINE", "MONITOR"])
        self.mode_combo.setCurrentText("COARSE")
        self.mode_combo.currentTextChanged.connect(self._on_mode_changed)
        control_layout.addWidget(self.mode_combo)
        
        control_layout.addSpacing(20)
        
        # Start/Stop buttons
        self.start_button = QPushButton('Start Collection')
        self.start_button.clicked.connect(self.start_collection)
        control_layout.addWidget(self.start_button)
        
        self.stop_button = QPushButton('Stop Collection')
        self.stop_button.clicked.connect(self.stop_collection)
        self.stop_button.setEnabled(False)
        control_layout.addWidget(self.stop_button)
        
        # Fast mode button
        self.fast_mode_button = QPushButton('Switch to Fast Mode')
        self.fast_mode_button.clicked.connect(self._switch_to_fast_mode)
        self.fast_mode_button.setEnabled(False)
        control_layout.addWidget(self.fast_mode_button)
        
        control_layout.addSpacing(20)
        
        # Hide button
        self.hide_button = QPushButton('Hide to Tray')
        self.hide_button.clicked.connect(self.hide_to_tray)
        control_layout.addWidget(self.hide_button)
        
        control_layout.addStretch()
        control_group.setLayout(control_layout)
        main_layout.addWidget(control_group)

        self.voltage_1v_button = QPushButton('Set 1V')
        self.voltage_1v_button.clicked.connect(lambda: self.set_bias_voltage(1.0))
        control_layout.addWidget(self.voltage_1v_button)

        self.voltage_2v_button = QPushButton('Set 2V')
        self.voltage_2v_button.clicked.connect(lambda: self.set_bias_voltage(2.0))
        control_layout.addWidget(self.voltage_2v_button)

        self.voltage_off_button = QPushButton('Voltage OFF')
        self.voltage_off_button.clicked.connect(self.disable_voltage)
        control_layout.addWidget(self.voltage_off_button)
        
        # === Status Panel ===
        status_group = QGroupBox("System Status")
        status_layout = QGridLayout()
        
        # Headers
        status_layout.addWidget(QLabel("<b>Channel</b>"), 0, 0)
        status_layout.addWidget(QLabel("<b>Server</b>"), 0, 1)
        status_layout.addWidget(QLabel("<b>Clients</b>"), 0, 2)
        status_layout.addWidget(QLabel("<b>Data Points</b>"), 0, 3)
        status_layout.addWidget(QLabel("<b>Current</b>"), 0, 4)
        status_layout.addWidget(QLabel("<b>Range</b>"), 0, 5)
        status_layout.addWidget(QLabel("<b>Peak</b>"), 0, 6)
        status_layout.addWidget(QLabel("<b>Quality</b>"), 0, 7)
        
        # Channel 1 status
        status_layout.addWidget(QLabel("Channel 1:"), 1, 0)
        self.ch1_server_label = QLabel(f"{config.SERVER_CH1_HOST}:{config.SERVER_CH1_PORT}")
        status_layout.addWidget(self.ch1_server_label, 1, 1)
        self.ch1_client_label = QLabel('0')
        status_layout.addWidget(self.ch1_client_label, 1, 2)
        self.ch1_data_label = QLabel('0')
        status_layout.addWidget(self.ch1_data_label, 1, 3)
        self.ch1_current_label = QLabel('---')
        self.ch1_current_label.setFont(QFont("Courier", 10))
        status_layout.addWidget(self.ch1_current_label, 1, 4)
        self.ch1_range_label = QLabel('---')
        status_layout.addWidget(self.ch1_range_label, 1, 5)
        self.ch1_peak_label = QLabel('---')
        status_layout.addWidget(self.ch1_peak_label, 1, 6)
        
        self.ch1_quality_bar = QProgressBar()
        self.ch1_quality_bar.setRange(0, 100)
        self.ch1_quality_bar.setTextVisible(True)
        status_layout.addWidget(self.ch1_quality_bar, 1, 7)
        
        # Channel 2 status
        status_layout.addWidget(QLabel("Channel 2:"), 2, 0)
        self.ch2_server_label = QLabel(f"{config.SERVER_CH2_HOST}:{config.SERVER_CH2_PORT}")
        status_layout.addWidget(self.ch2_server_label, 2, 1)
        self.ch2_client_label = QLabel('0')
        status_layout.addWidget(self.ch2_client_label, 2, 2)
        self.ch2_data_label = QLabel('0')
        status_layout.addWidget(self.ch2_data_label, 2, 3)
        self.ch2_current_label = QLabel('---')
        self.ch2_current_label.setFont(QFont("Courier", 10))
        status_layout.addWidget(self.ch2_current_label, 2, 4)
        self.ch2_range_label = QLabel('---')
        status_layout.addWidget(self.ch2_range_label, 2, 5)
        self.ch2_peak_label = QLabel('---')
        status_layout.addWidget(self.ch2_peak_label, 2, 6)
        
        self.ch2_quality_bar = QProgressBar()
        self.ch2_quality_bar.setRange(0, 100)
        self.ch2_quality_bar.setTextVisible(True)
        status_layout.addWidget(self.ch2_quality_bar, 2, 7)
        
        status_group.setLayout(status_layout)
        main_layout.addWidget(status_group)
        
        # === Performance Metrics ===
        perf_group = QGroupBox("Performance Metrics")
        perf_layout = QHBoxLayout()
        
        self.ch1_rate_label = QLabel("Ch1 Rate: --- Hz")
        perf_layout.addWidget(self.ch1_rate_label)
        
        self.ch2_rate_label = QLabel("Ch2 Rate: --- Hz")
        perf_layout.addWidget(self.ch2_rate_label)
        
        self.ch1_time_label = QLabel("Ch1 Time: --- ms")
        perf_layout.addWidget(self.ch1_time_label)
        
        self.ch2_time_label = QLabel("Ch2 Time: --- ms")
        perf_layout.addWidget(self.ch2_time_label)
        
        perf_group.setLayout(perf_layout)
        main_layout.addWidget(perf_group)
        
        # === Data Plotter ===
        # Use the alignment-optimized plotter
        self.data_plotter = AlignmentPlotter(config.MAX_DATA_POINTS)
        main_layout.addWidget(self.data_plotter)
        
        # Status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage('Ready - Select mode and click Start Collection')
    
    def _setup_system_tray(self):
        """Setup system tray functionality."""
        if not QSystemTrayIcon.isSystemTrayAvailable():
            QMessageBox.critical(self, "System Tray",
                               "System tray is not available on this system.")
            return
        
        self.tray_icon = SystemTrayIcon(self)
        self.tray_icon.show()
        
        self.tray_icon.showMessage(
            'Alignment System',
            'Application started - Ready for alignment',
            QSystemTrayIcon.Information,
            2000
        )
    
    def _setup_multimeter(self):
        """Setup optimized multimeter connection."""
        self.multimeter = SmartKeithleyMultimeter(config.MULTIMETER_ADDRESS)
        success, message = self.multimeter.connect()
        
        if success:
            # Configure based on initial mode
            initial_mode = self.mode_combo.currentText()
            self.multimeter.configure_for_alignment(
                config.MODE_SETTINGS[initial_mode]["initial_range"]
            )
            self.status_bar.showMessage(f"{message} - Mode: {initial_mode}")
            print(message)
        else:
            self.status_bar.showMessage(message)
            print(message)
            QMessageBox.warning(self, "Multimeter Connection", message)
    
    def _setup_servers(self):
        """Setup the data servers for both channels."""
        # Channel 1 server
        self.ch1_server = DataServer(config.SERVER_CH1_HOST, config.SERVER_CH1_PORT)
        success1, message1 = self.ch1_server.setup()
        
        if success1:
            self.ch1_server.start(lambda sock, addr: self._handle_new_client(1, sock, addr))
            print(f"Channel 1: {message1}")
        else:
            print(f"Channel 1 server failed: {message1}")
            QMessageBox.warning(self, "Channel 1 Server", message1)
        
        # Channel 2 server
        self.ch2_server = DataServer(config.SERVER_CH2_HOST, config.SERVER_CH2_PORT)
        success2, message2 = self.ch2_server.setup()
        
        if success2:
            self.ch2_server.start(lambda sock, addr: self._handle_new_client(2, sock, addr))
            print(f"Channel 2: {message2}")
        else:
            print(f"Channel 2 server failed: {message2}")
            QMessageBox.warning(self, "Channel 2 Server", message2)
        
        if success1 and success2:
            self.status_bar.showMessage("Both channel servers ready - Select mode to begin")
        elif success1:
            self.status_bar.showMessage("Only Channel 1 server ready")
        elif success2:
            self.status_bar.showMessage("Only Channel 2 server ready")
        else:
            self.status_bar.showMessage("Server setup failed")
    
    def _handle_new_client(self, channel: int, client_socket, address):
        """Handle a new client connection."""
        print(f"Channel {channel}: New client from {address}")
        
        if self.data_collector:
            self.data_collector.add_client(channel, client_socket)
            
            client_thread = threading.Thread(
                target=self._client_handler,
                args=(channel, client_socket),
                daemon=True
            )
            client_thread.start()
        else:
            client_socket.close()
    
    def _client_handler(self, channel: int, client_socket):
        """Handle individual client connection."""
        try:
            while not self.is_closing and self.data_collector and self.data_collector.running:
                time.sleep(1)
        except (ConnectionResetError, ConnectionAbortedError):
            print(f"Channel {channel} client disconnected")
        finally:
            if self.data_collector:
                self.data_collector.remove_client(channel, client_socket)
            try:
                client_socket.close()
            except:
                pass
    
    def start_collection(self):
        """Start data collection with selected mode."""
        if not self.multimeter:
            QMessageBox.warning(self, "Error", "Multimeter not connected!")
            return
        
        # Create alignment data collector
        self.data_collector = AlignmentDataCollector(self.multimeter)
        
        # Set initial mode
        current_mode = self.mode_combo.currentText()
        self.data_collector.set_mode(current_mode)
        
        # Connect channel 1 signals
        self.data_collector.ch1_data_updated.connect(
            lambda val, rng: self._update_channel_display(1, val, rng)
        )
        self.data_collector.ch1_histogram_updated.connect(self.data_plotter.update_ch1_histogram)
        self.data_collector.ch1_client_count_changed.connect(
            lambda count: self.ch1_client_label.setText(str(count))
        )
        
        # Connect channel 2 signals
        self.data_collector.ch2_data_updated.connect(
            lambda val, rng: self._update_channel_display(2, val, rng)
        )
        self.data_collector.ch2_histogram_updated.connect(self.data_plotter.update_ch2_histogram)
        self.data_collector.ch2_client_count_changed.connect(
            lambda count: self.ch2_client_label.setText(str(count))
        )
        
        # Connect alignment quality signals
        self.data_collector.alignment_quality.connect(self._update_alignment_quality)
        self.data_collector.signal_found.connect(self._on_signal_found)
        self.data_collector.signal_lost.connect(self._on_signal_lost)
        
        # Start collection
        self.data_collector.start()
        
        # Start update timers
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._update_statistics)
        self.update_timer.start(1000)  # Update every second
        
        # Update UI state
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        self.fast_mode_button.setEnabled(True)
        self.mode_combo.setEnabled(True)  # Can change mode during operation
        
        self.status_bar.showMessage(f'Data collection started - Mode: {current_mode}')
        
        # Update tray icon
        if hasattr(self, 'tray_icon'):
            self.tray_icon.update_status(True)
    
    def stop_collection(self):
        """Stop data collection."""
        if hasattr(self, 'update_timer'):
            self.update_timer.stop()
            
        if self.data_collector:
            self.data_collector.stop()
            self.data_collector = None
        
        # Update UI
        self.start_button.setEnabled(True)
        self.stop_button.setEnabled(False)
        self.fast_mode_button.setEnabled(False)
        self.status_bar.showMessage('Data collection stopped')
        
        # Update tray icon
        if hasattr(self, 'tray_icon'):
            self.tray_icon.update_status(False)
    
    def _on_mode_changed(self, mode: str):
        """Handle mode change."""
        if self.data_collector:
            self.data_collector.set_mode(mode)
            self.status_bar.showMessage(f"Switched to {mode} mode")
            
            # Update button text based on mode
            if mode == "MONITOR":
                self.fast_mode_button.setText("In Fast Mode")
                self.fast_mode_button.setEnabled(False)
            else:
                self.fast_mode_button.setText("Switch to Fast Mode")
                self.fast_mode_button.setEnabled(True)
    
    # Add methods:
    def set_bias_voltage(self, voltage):
        """Set bias voltage on both channels."""
        if self.multimeter:
            success, msg = self.multimeter.set_both_channels_voltage(voltage, voltage)
            self.status_bar.showMessage(msg)
            
    def disable_voltage(self):
        """Disable all voltage outputs."""
        if self.multimeter:
            success, msg = self.multimeter.disable_all_voltage_outputs()
            self.status_bar.showMessage(msg)

    def _switch_to_fast_mode(self):
        """Quick switch to fast monitoring mode."""
        self.mode_combo.setCurrentText("MONITOR")
        if self.multimeter:
            self.multimeter.configure_fast_mode()
        if self.data_collector:
            self.data_collector.set_mode("MONITOR")
        self.status_bar.showMessage("Switched to fast monitoring mode (100+ Hz)")
    
    def _update_channel_display(self, channel: int, value: float, range_info: str):
        """Update channel display with value and range."""
        if channel == 1:
            self.ch1_current_label.setText(f"{value:.3e}")
            self.ch1_range_label.setText(range_info)
            self.data_plotter.update_ch1_plot(value)
        else:
            self.ch2_current_label.setText(f"{value:.3e}")
            self.ch2_range_label.setText(range_info)
            self.data_plotter.update_ch2_plot(value)
    
    def _update_alignment_quality(self, channel: int, quality: float):
        """Update alignment quality indicator."""
        if channel == 1:
            self.ch1_quality_bar.setValue(int(quality))
        else:
            self.ch2_quality_bar.setValue(int(quality))
    
    def _on_signal_found(self, channel: int):
        """Handle signal found event."""
        self.status_bar.showMessage(f"Signal detected on channel {channel}!", 5000)
        if hasattr(self, 'tray_icon'):
            self.tray_icon.showMessage(
                'Signal Found',
                f'Signal detected on channel {channel}',
                QSystemTrayIcon.Information,
                2000
            )
    
    def _on_signal_lost(self, channel: int):
        """Handle signal lost event."""
        self.status_bar.showMessage(f"Signal lost on channel {channel}!", 5000)
    
    def _update_statistics(self):
        """Update statistics and performance metrics."""
        if self.data_collector:
            # Update data counts
            ch1_count = self.data_collector.ch1_data_count
            ch2_count = self.data_collector.ch2_data_count
            
            self.ch1_data_label.setText(str(ch1_count))
            self.ch2_data_label.setText(str(ch2_count))
            
            # Calculate rates
            if hasattr(self, '_last_ch1_count'):
                ch1_rate = ch1_count - self._last_ch1_count
                ch2_rate = ch2_count - self._last_ch2_count
                
                self.ch1_rate_label.setText(f"Ch1 Rate: {ch1_rate} Hz")
                self.ch2_rate_label.setText(f"Ch2 Rate: {ch2_rate} Hz")
                
                # Update timing if available
                if ch1_rate > 0:
                    self.ch1_time_label.setText(f"Ch1 Time: {1000/ch1_rate:.1f} ms")
                if ch2_rate > 0:
                    self.ch2_time_label.setText(f"Ch2 Time: {1000/ch2_rate:.1f} ms")
            
            self._last_ch1_count = ch1_count
            self._last_ch2_count = ch2_count
            
            # Update peak values
            stats1 = self.data_collector.get_statistics(1)
            stats2 = self.data_collector.get_statistics(2)
            
            if stats1:
                self.ch1_peak_label.setText(f"{stats1.get('peak', 0):.3e}")
            if stats2:
                self.ch2_peak_label.setText(f"{stats2.get('peak', 0):.3e}")
    
    def hide_to_tray(self):
        """Hide window to system tray."""
        if hasattr(self, 'tray_icon'):
            self.hide()
            self.tray_icon.showMessage(
                'Alignment System',
                'Application minimized to system tray',
                QSystemTrayIcon.Information,
                2000
            )
        else:
            QMessageBox.information(self, "System Tray",
                                  "System tray not available. Window will be minimized.")
            self.showMinimized()
    
    def get_status(self) -> str:
        """Get current application status."""
        if self.data_collector:
            stats1 = self.data_collector.get_statistics(1)
            stats2 = self.data_collector.get_statistics(2)
            
            status = f"Mode: {self.mode_combo.currentText()}\n"
            if stats1:
                status += f"Ch1: {stats1.get('current', 0):.3e} A (Peak: {stats1.get('peak', 0):.3e})\n"
            if stats2:
                status += f"Ch2: {stats2.get('current', 0):.3e} A (Peak: {stats2.get('peak', 0):.3e})"
            
            return status
        else:
            return "Data collection not active"
    
    def closeEvent(self, event):
        """Handle window close event."""
        if hasattr(self, 'tray_icon') and self.tray_icon.isVisible():
            event.ignore()
            self.hide_to_tray()
        else:
            reply = QMessageBox.question(self, 'Exit Application',
                                       'Are you sure you want to exit?',
                                       QMessageBox.Yes | QMessageBox.No,
                                       QMessageBox.No)
            
            if reply == QMessageBox.Yes:
                self.close_application()
                event.accept()
            else:
                event.ignore()
    
    def close_application(self):
        """Completely close the application."""
        self.is_closing = True
        
        # Stop data collection
        if self.data_collector:
            self.data_collector.stop()
        
        # Stop servers
        if self.ch1_server:
            self.ch1_server.stop()
        if self.ch2_server:
            self.ch2_server.stop()
        
        # Close multimeter
        if self.multimeter:
            self.multimeter.close()
        
        # Hide tray icon
        if hasattr(self, 'tray_icon'):
            self.tray_icon.hide()
        
        QApplication.quit()