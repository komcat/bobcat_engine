from PyQt5 import QtCore, QtWidgets, QtGui
import pyqtgraph as pg
import numpy as np
import socket
from siphog_unit import SiphogUnit
from constants_siphog_gui import *  # string/number constants mostly for gui function
from pyqtgraph import GraphicsLayoutWidget, PlotItem, TextItem

data_key = ["SLED_Current (mA)", "Photo Current (uA)", "SLED_Temp (C)", "Target SAG_PWR (V)", "SAG_PWR (V)", "TEC_Current (mA)"]
host = "127.0.0.1"
port = 65432

def process_out_msg(out_msg):
    values_str = out_msg.strip()[1:-1]  # Remove leading '$' and trailing ',' and split by ','
    values_list = values_str.split(',')
    values = [float(value.strip()) for value in values_list]  # Convert each value to float
    return values

class SiPhOGThread(QtCore.QThread):
    data_updated = QtCore.pyqtSignal(list)  # Emitting list of floats
    status_updated = QtCore.pyqtSignal(str)  # Emitting status messages
    current_changed = QtCore.pyqtSignal(int)  # Signal to confirm current change

    def __init__(self, current_mA, parent=None):
        super().__init__(parent)
        self.sp = None
        self.server_running = False
        self.current_mA = current_mA
        self.new_current_mA = None
        self.current_change_requested = False
        self.mutex = QtCore.QMutex()

    def set_current(self, new_current_mA):
        """Thread-safe method to request current change"""
        self.mutex.lock()
        self.new_current_mA = new_current_mA
        self.current_change_requested = True
        self.mutex.unlock()

    def check_and_update_current(self):
        """Check if current change is requested and apply it"""
        self.mutex.lock()
        if self.current_change_requested and self.sp is not None:
            try:
                self.sp.set_sld_setpoints(current_mA=self.new_current_mA)
                self.current_mA = self.new_current_mA
                self.current_change_requested = False
                self.current_changed.emit(self.current_mA)
                self.status_updated.emit(f"Current updated to {self.current_mA} mA")
                print(f"Current updated to {self.current_mA} mA")
            except Exception as e:
                self.status_updated.emit(f"Failed to update current: {str(e)}")
                print(f"Failed to update current: {str(e)}")
                self.current_change_requested = False
        self.mutex.unlock()

    def run(self):
        try:
            self.sp = SiphogUnit.auto_detect()
            assert self.sp is not None, "No SiPhOG Detected"

            print("\n\tSiPhOG Connected :) V02")
            self.sp.set_factory_unlock()
            self.sp.set_odr(200)
            self.sp.set_control_mode(sled_mode=0x01, tec_mode=0x03)
            self.sp.set_sld_setpoints(current_mA=self.current_mA)
            self.sp.set_tec_setpoints(temperature_C=25)
            self.sp.set_modulator("Off")
            self.sp.set_output_factory()

            SERVER = socket.socket()
            SERVER.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # Allow socket reuse
            SERVER.bind((host, port))
            SERVER.listen(2)

            self.server_running = True
            self.status_updated.emit("Server running, waiting for connection...")

            while self.server_running:
                try:
                    SERVER.settimeout(1.0)  # Set timeout to allow periodic checks
                    conn, address = SERVER.accept()  # accept new connection
                    conn.settimeout(0.1)  # Set short timeout for non-blocking recv
                    self.status_updated.emit(f"Connected with {address}")
                    print("\tConnected with " + str(address) + "\n")
                    
                    while self.server_running:
                        try:
                            # Check for current change requests
                            self.check_and_update_current()
                            
                            msg = self.sp.read_valid_one_message()

                            out_msg_str = ""

                            msg.data_dict["Photo Current (uA)"] = msg.data_dict["SLD_PWR (V)"] / PWR_MON_TRANSFER_FUNC * 1e6
                            msg.data_dict["Target SAG_PWR (V)"] = TARGET_LOSS_IN_FRACTION * msg.data_dict["SLD_PWR (V)"] / PWR_MON_TRANSFER_FUNC * SAGNAC_TIA_GAIN

                            for key in data_key:
                                out_msg_str += "{:f},".format(msg.data_dict[key])
                            
                            try:
                                conn.send(out_msg_str[:-1].encode())  # send data to the client
                            except socket.error:
                                # Client disconnected, break inner loop to wait for new connection
                                self.status_updated.emit("Client disconnected, waiting for new connection...")
                                break

                            # Process out_msg and emit parsed values
                            values = process_out_msg(out_msg_str)
                            self.data_updated.emit(values)
                            
                        except socket.timeout:
                            # Normal timeout, continue loop
                            continue
                        except Exception as e:
                            self.status_updated.emit(f"Communication error: {str(e)}")
                            print(f"\tCommunication error: {str(e)}")
                            break
                    
                    try:
                        conn.close()
                    except:
                        pass
                        
                except socket.timeout:
                    # No connection within timeout, continue main loop
                    continue
                except Exception as e:
                    if self.server_running:  # Only show error if we're still supposed to be running
                        self.status_updated.emit(f"Server error: {str(e)}")
                        print(f"\tServer error: {str(e)}")

            self.status_updated.emit("Server stopped")
            SERVER.close()
            
        except Exception as e:
            self.status_updated.emit(f"Initialization error: {str(e)}")
            print(f"Initialization error: {str(e)}")
        finally:
            self.server_running = False

    def stop(self):
        self.server_running = False


class RealTimePlotWidget(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)

        # Layouts
        self.layout = QtWidgets.QVBoxLayout(self)
        self.control_layout = QtWidgets.QHBoxLayout()
        self.current_control_layout = QtWidgets.QHBoxLayout()
        self.plot_layout = QtWidgets.QVBoxLayout()

        # Drop-down for initial current_mA
        self.initial_current_label = QtWidgets.QLabel("Initial Current (mA):")
        self.current_mA_dropdown = QtWidgets.QComboBox()
        self.current_mA_dropdown.addItems(["100", "110", "120", "130", "140", "150","160","170","180","190", "200"])
        self.current_mA_dropdown.setEnabled(True)
        
        # Live current control
        self.live_current_label = QtWidgets.QLabel("Live Current (mA):")
        self.live_current_dropdown = QtWidgets.QComboBox()
        self.live_current_dropdown.addItems(["100", "110", "120", "130", "140", "150","160","170","180","190", "200"])
        self.live_current_dropdown.setEnabled(False)  # Disabled until server starts
        self.apply_current_button = QtWidgets.QPushButton("Apply Current")
        self.apply_current_button.setEnabled(False)
        
        # Current status display
        self.current_status_label = QtWidgets.QLabel("Current: -- mA")

        # Start/Stop buttons
        self.start_button = QtWidgets.QPushButton("Start Server")
        self.stop_button = QtWidgets.QPushButton("Stop Server")
        self.stop_button.setEnabled(False)

        # Status label
        self.status_label = QtWidgets.QLabel("Server stopped")

        # Add controls to layouts
        self.control_layout.addWidget(self.initial_current_label)
        self.control_layout.addWidget(self.current_mA_dropdown)
        self.control_layout.addWidget(self.start_button)
        self.control_layout.addWidget(self.stop_button)
        self.control_layout.addWidget(self.status_label)
        
        self.current_control_layout.addWidget(self.live_current_label)
        self.current_control_layout.addWidget(self.live_current_dropdown)
        self.current_control_layout.addWidget(self.apply_current_button)
        self.current_control_layout.addWidget(self.current_status_label)

        # Real-time plot widget
        self.plot_widget = pg.GraphicsLayoutWidget()
        self.plotItem = self.plot_widget.addPlot(title="Real-time Data Plot")
        self.plotDataItem1 = self.plotItem.plot(pen={'color': 'pink', 'width': 8}, name='Line 1')
        self.plotDataItem2 = self.plotItem.plot(pen={'color': 'g', 'width': 8}, name='Line 2')

        self.label1 = TextItem(anchor=(0, 0.5))
        self.label2 = TextItem(anchor=(0, 0.5))
        font = QtGui.QFont()
        font.setPointSize(72)
        self.label1.setFont(font)
        self.label2.setFont(font)
        self.plotItem.addItem(self.label1)
        self.plotItem.addItem(self.label2)

        self.max_data_points = 1000
        self.data_x1 = []
        self.data_x2 = []

        # Add layouts to the main layout
        self.layout.addLayout(self.control_layout)
        self.layout.addLayout(self.current_control_layout)
        self.plot_layout.addWidget(self.plot_widget)
        self.layout.addLayout(self.plot_layout)

        # Thread and signal connections
        self.server_thread = None
        self.start_button.clicked.connect(self.start_server)
        self.stop_button.clicked.connect(self.stop_server)
        self.apply_current_button.clicked.connect(self.apply_current_change)

    def update_plot(self, values):
        self.data_x1.append(values[3])
        self.data_x2.append(values[4])

        if len(self.data_x1) > self.max_data_points:
            self.data_x1.pop(0)
            self.data_x2.pop(0)

        self.plotDataItem1.setData(self.data_x1)
        self.plotDataItem2.setData(self.data_x2)

        if self.data_x1:
            self.label1.setText(f'T: {self.data_x1[-1]:.3f}')
            self.label1.setPos(len(self.data_x1) - 1, self.data_x1[-1])

        if self.data_x2:
            self.label2.setText(f'A: {self.data_x2[-1]:.3f}')
            self.label2.setPos(len(self.data_x2) - 1, self.data_x2[-1])

    def update_status(self, status):
        self.status_label.setText(status)

    def update_current_status(self, current_mA):
        self.current_status_label.setText(f"Current: {current_mA} mA")
        # Update the live dropdown to reflect the current value
        index = self.live_current_dropdown.findText(str(current_mA))
        if index >= 0:
            self.live_current_dropdown.setCurrentIndex(index)

    def apply_current_change(self):
        if self.server_thread is not None and self.server_thread.isRunning():
            new_current = int(self.live_current_dropdown.currentText())
            self.server_thread.set_current(new_current)
            self.apply_current_button.setEnabled(False)  # Disable until change is applied
            self.status_label.setText(f"Changing current to {new_current} mA...")

    def on_current_changed(self, current_mA):
        self.update_current_status(current_mA)
        self.apply_current_button.setEnabled(True)  # Re-enable the button

    def start_server(self):
        self.current_mA_dropdown.setEnabled(False)
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        self.live_current_dropdown.setEnabled(True)
        self.apply_current_button.setEnabled(True)
        
        selected_current_mA = int(self.current_mA_dropdown.currentText())
        self.live_current_dropdown.setCurrentText(str(selected_current_mA))
        self.update_current_status(selected_current_mA)

        self.server_thread = SiPhOGThread(selected_current_mA)
        self.server_thread.data_updated.connect(self.update_plot)
        self.server_thread.status_updated.connect(self.update_status)
        self.server_thread.current_changed.connect(self.on_current_changed)
        self.server_thread.start()

    def stop_server(self):
        if self.server_thread is not None:
            self.server_thread.stop()
            self.server_thread.wait()

        self.current_mA_dropdown.setEnabled(True)
        self.start_button.setEnabled(True)
        self.stop_button.setEnabled(False)
        self.live_current_dropdown.setEnabled(False)
        self.apply_current_button.setEnabled(False)
        self.update_status("Server stopped")
        self.current_status_label.setText("Current: -- mA")

def main():
    app = QtWidgets.QApplication([])

    pg.setConfigOptions(antialias=True)

    win = RealTimePlotWidget()
    win.show()
    win.resize(800, 600)
    win.raise_()

    app.exec_()

if __name__ == "__main__":
    main()