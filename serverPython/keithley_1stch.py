import socket
import threading
import time
import pyvisa
import pyqtgraph as pg
import numpy as np
import sys
from PyQt5.QtWidgets import (QApplication, QVBoxLayout, QWidget, QMainWindow, 
                            QSystemTrayIcon, QMenu, QAction, QMessageBox, 
                            QHBoxLayout, QPushButton, QLabel, QStatusBar)
from PyQt5.QtCore import QObject, pyqtSignal, QTimer
from PyQt5.QtGui import QIcon, QPixmap, QPainter, QBrush, QPen, QFont, QColor
from PyQt5.QtCore import Qt

class DataCollector(QObject):
    data_updated = pyqtSignal(float)
    histogram_updated = pyqtSignal(list)
    client_count_changed = pyqtSignal(int)

    def __init__(self, multimeter, parent=None):
        super().__init__(parent)
        self.multimeter = multimeter
        self.running = False
        self.clients = []
        self.lock = threading.Lock()
        self.elapsed_times = []
        self.data_count = 0

    def start(self):
        self.running = True
        data_thread = threading.Thread(target=self.collect_data, daemon=True)
        data_thread.start()
        
        histogram_thread = threading.Thread(target=self.calculate_histogram, daemon=True)
        histogram_thread.start()

    def collect_data(self):
        while self.running:
            try:
                start_time = time.time()
                # Get the current measurement from the multimeter - channel 1 only
                response = self.multimeter.query(":READ?")
                data_str = response.strip()
                
                # Just get the first value (channel 1)
                if ',' in data_str:
                    channel1_value = float(data_str.split(',')[0])
                else:
                    channel1_value = float(data_str)

                # Emit data to update client plot
                self.data_updated.emit(channel1_value)
                self.data_count += 1

                # Send only channel 1 data to clients
                with self.lock:
                    disconnected_clients = []
                    for client in self.clients:
                        try:
                            client.sendall(f"{channel1_value}\n".encode('utf-8'))
                        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                            print("Client disconnected unexpectedly, removing from list.")
                            disconnected_clients.append(client)
                    
                    # Remove disconnected clients
                    for client in disconnected_clients:
                        if client in self.clients:
                            self.clients.remove(client)
                    
                    if disconnected_clients:
                        self.client_count_changed.emit(len(self.clients))

                elapsed_time = time.time() - start_time
                self.elapsed_times.append(elapsed_time)

                time.sleep(0.05)  # Adjust the sleep time as needed

            except pyvisa.errors.VisaIOError as e:
                print(f"VISA IO Error: {e}")
            except ValueError as e:
                print(f"Value Error: {e} for response: {response}")
            except Exception as e:
                print(f"Unexpected error: {e}")

    def calculate_histogram(self):
        while self.running:
            time.sleep(10)  # Calculate histogram every 10 seconds
            with self.lock:
                if self.elapsed_times:
                    hist, bin_edges = np.histogram(self.elapsed_times, bins=20)
                    self.histogram_updated.emit([hist.tolist(), bin_edges.tolist()])
                    self.elapsed_times.clear()

    def add_client(self, client_socket):
        with self.lock:
            self.clients.append(client_socket)
            self.client_count_changed.emit(len(self.clients))

    def remove_client(self, client_socket):
        with self.lock:
            if client_socket in self.clients:
                self.clients.remove(client_socket)
                self.client_count_changed.emit(len(self.clients))

    def get_client_count(self):
        with self.lock:
            return len(self.clients)

    def stop(self):
        self.running = False

class DataPlotter(QWidget):
    def __init__(self):
        super().__init__()
        self.initUI()
        self.data_x = []
        self.max_data_points = 500

    def initUI(self):
        self.layout = QVBoxLayout(self)
        self.plotWidget = pg.GraphicsLayoutWidget()
        self.plotItem = self.plotWidget.addPlot(title="Real-time Channel 1 Current Measurement")
        self.curve = self.plotItem.plot(pen={'color': 'yellow', 'width': 3})
        self.histogramItem = self.plotWidget.addPlot(title="Histogram of Elapsed Times")
        self.histogram = pg.BarGraphItem(x=[], height=[], width=0.1, brush='r')
        self.histogramItem.addItem(self.histogram)
        self.layout.addWidget(self.plotWidget)

    def update_plot(self, data):
        self.data_x.append(data)
        self.curve.setData(self.data_x[-self.max_data_points:])

    def update_histogram(self, hist_data):
        hist, bin_edges = hist_data
        self.histogram.setOpts(x=bin_edges[:-1], height=hist, width=(bin_edges[1] - bin_edges[0]))

class SystemTrayIcon(QSystemTrayIcon):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.parent_window = parent
        self.setup_tray_icon()
        self.setup_menu()
        
    def create_icon(self, color='green'):
        """Create a simple circular icon for the system tray"""
        pixmap = QPixmap(64, 64)
        pixmap.fill(Qt.transparent)  # Fill with transparent
        
        painter = QPainter(pixmap)
        painter.setRenderHint(QPainter.Antialiasing)
        
        # Set color based on status
        if color == 'green':
            brush = QBrush(QColor(0, 255, 0))  # Green for active
            pen = QPen(QColor(0, 204, 0), 2)
        else:
            brush = QBrush(QColor(255, 0, 0))  # Red for inactive
            pen = QPen(QColor(204, 0, 0), 2)
        
        painter.setBrush(brush)
        painter.setPen(pen)
        painter.drawEllipse(8, 8, 48, 48)
        
        # Add "D" for Data Collector
        painter.setPen(QPen(QColor(255, 255, 255), 2))
        painter.setFont(QFont('Arial', 20, QFont.Bold))
        painter.drawText(20, 38, 'D')
        
        painter.end()
        return QIcon(pixmap)
    
    def setup_tray_icon(self):
        """Setup the system tray icon"""
        self.setIcon(self.create_icon('green'))
        self.setToolTip('Data Collector - Running')
        
    def setup_menu(self):
        """Setup the context menu for the system tray"""
        menu = QMenu()
        
        # Show/Hide window actions
        show_action = QAction('Show Window', self)
        show_action.triggered.connect(self.show_window)
        menu.addAction(show_action)
        
        hide_action = QAction('Hide to Tray', self)
        hide_action.triggered.connect(self.hide_window)
        menu.addAction(hide_action)
        
        menu.addSeparator()
        
        # Status action
        status_action = QAction('Show Status', self)
        status_action.triggered.connect(self.show_status)
        menu.addAction(status_action)
        
        menu.addSeparator()
        
        # Exit action
        exit_action = QAction('Exit Application', self)
        exit_action.triggered.connect(self.exit_application)
        menu.addAction(exit_action)
        
        self.setContextMenu(menu)
        
        # Connect double-click to show window
        self.activated.connect(self.tray_icon_activated)
    
    def tray_icon_activated(self, reason):
        """Handle tray icon activation (double-click, etc.)"""
        if reason == QSystemTrayIcon.DoubleClick:
            self.show_window()
    
    def show_window(self):
        """Show the main window"""
        if self.parent_window:
            self.parent_window.show()
            self.parent_window.raise_()
            self.parent_window.activateWindow()
    
    def hide_window(self):
        """Hide the main window to system tray"""
        if self.parent_window:
            self.parent_window.hide()
            self.showMessage(
                'Data Collector',
                'Application minimized to system tray',
                QSystemTrayIcon.Information,
                2000
            )
    
    def show_status(self):
        """Show current status in tray notification"""
        if self.parent_window and hasattr(self.parent_window, 'data_collector'):
            client_count = self.parent_window.data_collector.get_client_count()
            data_count = self.parent_window.data_collector.data_count
            
            status_msg = f"Clients Connected: {client_count}\nData Points Collected: {data_count}"
            
            self.showMessage(
                'Data Collector Status',
                status_msg,
                QSystemTrayIcon.Information,
                3000
            )
    
    def exit_application(self):
        """Exit the entire application"""
        if self.parent_window:
            self.parent_window.close_application()

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.data_collector = None
        self.server = None
        self.server_thread = None
        self.multimeter = None
        self.rm = None
        self.is_closing = False
        
        self.initUI()
        self.setup_system_tray()
        self.setup_server()
        self.setup_multimeter()
        
    def initUI(self):
        """Initialize the user interface"""
        self.setWindowTitle('Data Collector - Keithley Multimeter')
        self.setGeometry(100, 100, 1000, 700)
        
        # Central widget
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Main layout
        main_layout = QVBoxLayout(central_widget)
        
        # Control panel
        control_layout = QHBoxLayout()
        
        self.start_button = QPushButton('Start Collection')
        self.start_button.clicked.connect(self.start_collection)
        control_layout.addWidget(self.start_button)
        
        self.stop_button = QPushButton('Stop Collection')
        self.stop_button.clicked.connect(self.stop_collection)
        self.stop_button.setEnabled(False)
        control_layout.addWidget(self.stop_button)
        
        self.hide_button = QPushButton('Hide to Tray')
        self.hide_button.clicked.connect(self.hide_to_tray)
        control_layout.addWidget(self.hide_button)
        
        control_layout.addStretch()
        
        # Status labels
        self.client_label = QLabel('Connected Clients: 0')
        control_layout.addWidget(self.client_label)
        
        main_layout.addLayout(control_layout)
        
        # Data plotter
        self.data_plotter = DataPlotter()
        main_layout.addWidget(self.data_plotter)
        
        # Status bar
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage('Ready - Click Start Collection to begin')
        
    def setup_system_tray(self):
        """Setup system tray functionality"""
        if not QSystemTrayIcon.isSystemTrayAvailable():
            QMessageBox.critical(self, "System Tray", 
                               "System tray is not available on this system.")
            return
        
        self.tray_icon = SystemTrayIcon(self)
        self.tray_icon.show()
        
        # Show tray message on startup
        self.tray_icon.showMessage(
            'Data Collector',
            'Application started and available in system tray',
            QSystemTrayIcon.Information,
            2000
        )
    
    def setup_multimeter(self):
        """Setup connection to Keithley multimeter"""
        try:
            self.rm = pyvisa.ResourceManager()
            self.multimeter = self.rm.open_resource('GPIB0::1::INSTR')
            
            # Configure multimeter to measure current on channel 1 only
            self.multimeter.write(":SENS:FUNC 'CURR'")
            
            self.status_bar.showMessage('Multimeter connected successfully')
            print("Multimeter connected successfully")
            
        except Exception as e:
            error_msg = f"Failed to connect to multimeter: {e}"
            self.status_bar.showMessage(error_msg)
            print(error_msg)
            QMessageBox.warning(self, "Multimeter Connection", error_msg)
    
    def setup_server(self):
        """Setup the data server"""
        self.host = "127.0.0.44"
        self.port = 55555
        
        try:
            self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server.bind((self.host, self.port))
            self.server.listen(5)
            
            print(f"Server listening on {self.host}:{self.port}")
            self.status_bar.showMessage(f'Server ready on {self.host}:{self.port}')
            
            # Start server thread
            self.server_thread = threading.Thread(target=self.accept_clients, daemon=True)
            self.server_thread.start()
            
        except Exception as e:
            error_msg = f"Failed to setup server: {e}"
            print(error_msg)
            QMessageBox.critical(self, "Server Setup", error_msg)
    
    def start_collection(self):
        """Start data collection"""
        if not self.multimeter:
            QMessageBox.warning(self, "Error", "Multimeter not connected!")
            return
        
        # Setup data collector
        self.data_collector = DataCollector(self.multimeter)
        self.data_collector.data_updated.connect(self.data_plotter.update_plot)
        self.data_collector.histogram_updated.connect(self.data_plotter.update_histogram)
        self.data_collector.client_count_changed.connect(self.update_client_count)
        
        self.data_collector.start()
        
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        self.status_bar.showMessage('Data collection started')
        
        # Update tray icon
        if hasattr(self, 'tray_icon'):
            self.tray_icon.setIcon(self.tray_icon.create_icon('green'))
            self.tray_icon.setToolTip('Data Collector - Collecting Data')
    
    def stop_collection(self):
        """Stop data collection"""
        if self.data_collector:
            self.data_collector.stop()
            self.data_collector = None
        
        self.start_button.setEnabled(True)
        self.stop_button.setEnabled(False)
        self.status_bar.showMessage('Data collection stopped')
        
        # Update tray icon
        if hasattr(self, 'tray_icon'):
            self.tray_icon.setIcon(self.tray_icon.create_icon('red'))
            self.tray_icon.setToolTip('Data Collector - Stopped')
    
    def update_client_count(self, count):
        """Update the client count display"""
        self.client_label.setText(f'Connected Clients: {count}')
    
    def accept_clients(self):
        """Accept client connections"""
        while not self.is_closing:
            try:
                self.server.settimeout(1.0)  # Timeout to allow clean shutdown
                client_socket, addr = self.server.accept()
                print(f"Accepted connection from {addr}")
                
                if self.data_collector:
                    self.data_collector.add_client(client_socket)
                    client_handler = threading.Thread(
                        target=self.handle_client, 
                        args=(client_socket,), 
                        daemon=True
                    )
                    client_handler.start()
                else:
                    # If data collector not running, close the connection
                    client_socket.close()
                    
            except socket.timeout:
                continue
            except Exception as e:
                if not self.is_closing:
                    print(f"Server error: {e}")
                break
    
    def handle_client(self, client_socket):
        """Handle individual client connection"""
        try:
            while not self.is_closing and self.data_collector and self.data_collector.running:
                time.sleep(1)  # Keep connection alive
        except (ConnectionResetError, ConnectionAbortedError):
            print("Client disconnected")
        finally:
            if self.data_collector:
                self.data_collector.remove_client(client_socket)
            try:
                client_socket.close()
            except:
                pass
    
    def hide_to_tray(self):
        """Hide window to system tray"""
        if hasattr(self, 'tray_icon'):
            self.hide()
            self.tray_icon.showMessage(
                'Data Collector',
                'Application minimized to system tray',
                QSystemTrayIcon.Information,
                2000
            )
        else:
            QMessageBox.information(self, "System Tray", 
                                  "System tray not available. Window will be minimized.")
            self.showMinimized()
    
    def closeEvent(self, event):
        """Handle window close event"""
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
        """Completely close the application"""
        self.is_closing = True
        
        # Stop data collection
        if self.data_collector:
            self.data_collector.stop()
        
        # Close server
        if self.server:
            try:
                self.server.close()
            except:
                pass
        
        # Close multimeter
        if self.multimeter:
            try:
                self.multimeter.close()
            except:
                pass
        
        if self.rm:
            try:
                self.rm.close()
            except:
                pass
        
        # Hide tray icon
        if hasattr(self, 'tray_icon'):
            self.tray_icon.hide()
        
        QApplication.quit()

def main():
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)  # Keep app running when window is closed
    
    # Check if system tray is available
    if not QSystemTrayIcon.isSystemTrayAvailable():
        QMessageBox.critical(None, "System Tray",
                           "System tray is not available on this system.")
    
    # Create and show main window
    main_window = MainWindow()
    main_window.show()
    
    sys.exit(app.exec_())

if __name__ == "__main__":
    main()