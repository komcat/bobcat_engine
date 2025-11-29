from PyQt5 import QtCore, QtWidgets, QtGui
import pyqtgraph as pg
import numpy as np
import socket
import siphog
import serial
from pyqtgraph import GraphicsLayoutWidget, PlotItem, TextItem
import contextlib

data_key = ["SLED_Current (mA)", "Photo Current (uA)", "SLED_Temp (C)", "Target SAG_PWR (V)", "SAG_PWR (V)", "TEC_Current (mA)"]
host = "127.0.0.63"
port = 65432

# helpers (computing values to be unit tested)
class ComputeHelp:
    def __init__(self, fw, sagnac_tia_gain, sled_resp, pm_tia_gain):
        self.fw = fw
        self.sagnac_tia_gain = sagnac_tia_gain
        self.sled_resp = sled_resp
        self.pm_tia_gain = pm_tia_gain

    def get_target_loss_in_db(self):
        return -5.7 if self.fw < siphog.fwp(1,0,0) else -8.7

    def get_target_loss_fraction(self):
        return 10**(self.get_target_loss_in_db()/10)

    def get_sagnac_power_v(self, sag_pwr_uW):
        return sag_pwr_uW * self.sagnac_tia_gain * self.sled_resp * 1e-6

    def get_sled_power_v(self, sld_pwr_uW):
        return sld_pwr_uW * self.pm_tia_gain / self.sled_resp / 1e6

    def get_target_sagnac_pwr(self, sld_pwr_uW):
        return self.get_target_loss_fraction() * self.get_sled_power_v(sld_pwr_uW) / self.pm_tia_gain * self.sagnac_tia_gain

    def get_photo_current(self, sld_pwr_uW):
        return sld_pwr_uW / self.sled_resp

class SiPhOGThread(QtCore.QThread):
    data_updated = QtCore.pyqtSignal(list)  # Emitting list of floats
    status_updated = QtCore.pyqtSignal(str)  # Emitting status messages
    current_changed = QtCore.pyqtSignal(int)  # Signal to confirm current change

    def __init__(self, current_mA, parent=None):
        super().__init__(parent)
        self.sp = None
        self.spd = None
        self._stack = None
        self._exc = None
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
                self.sp.sld_setpoints = (self.new_current_mA, self.spd['sld_setpoints'][1])
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
            with contextlib.ExitStack() as stack:
                self._stack = stack
                self._run()
        except Exception as e:
            self._exc = e
            raise

    def _run(self):
        try:
            # auto-detect unit
            units = siphog.discover()
            if len(units) != 1:
                raise AssertionError(
                    f"Expecting one unit, found {len(units)}"
                )
            d = units[0]
            serial_port = d.get("port").get("device")

            self.sp = siphog.BaseSiphog(serial.Serial(serial_port))
            assert self.sp is not None, "No SiPhOG Detected"

            # enter siphog context (this opens serial connection)
            self.sp = self._stack.enter_context(self.sp)

            # Record all settings
            self.spd = self.sp.get_dict() # snapshot of settings at startup
            self.sp._serial.timeout = 1.0

            self.sp.set_odr = 200
            self.sp.control_mode = ((1, 'sld_const_curr'), (3, 'tec_const_temp'))
            self.sp.sld_setpoints = (self.current_mA, self.spd['sld_setpoints'][1])
            self.sp.tec_setpoints = (25, self.spd['tec_setpoints'][1])
            self.sp.mod_control = (0, 'off')
            self.sp.fw = siphog.fwp(*self.sp.firmware_ver)

            print("\n\tSiPhOG Connected :) V08")


            # Conversion factors
            helper = ComputeHelp(self.sp.fw, self.sp.sagnac_tg, self.sp.sld_resp, self.sp.pm_tg)
            TARGET_LOSS_IN_dB = helper.get_target_loss_in_db()
            TARGET_LOSS_IN_FRACTION = helper.get_target_loss_fraction()

            # For debugging purposes:
            if True:
                print(f"\n\t SiPhOG fw {self.sp.firmware_ver}, hw {self.sp.hardware_rev[0:2]}")
                print(f"\t Target loss in db: {TARGET_LOSS_IN_dB} (fraction: {TARGET_LOSS_IN_FRACTION})")
                print(f"\t Sagnac TIA gain: {self.sp.sagnac_tg}")
                print(f"\t PM TIA gain: {self.sp.pm_tg}")
                print(f"\t SLED responsivity: {self.sp.sld_resp}\n")
                print(f"\t Control modes: {self.sp.control_mode}, {self.sp.mod_control}")
                print(f"\t Setpoints: {self.sp.sld_setpoints}, {self.sp.tec_setpoints}")
                print(f"\t Thermistor: {self.sp.thermistor}")

            # Start socket server in non-blocking mode
            SERVER = socket.socket()
            SERVER.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # Allow socket reuse
            SERVER.bind((host, port))
            SERVER.listen(2)
            SERVER.settimeout(0.001)  # Very short timeout for non-blocking accept

            self.server_running = True
            self.status_updated.emit("Device connected, reading data...")

            outmsg_mode = 'factory'
            gen_msg = self.sp.generate_outmsg(outmsg_mode, duration_s = 0, counter_reset_ok=True)
            self._stack.callback(gen_msg.close)

            # generate parsed output
            gen_msg = self.sp.wrap_parser(outmsg_mode, gen_msg, spdict=self.spd)

            conn = None
            accept_check_counter = 0

            # Main data reading loop - runs immediately after device connection
            while self.server_running:
                try:
                    # Try to accept new connections (non-blocking) - only check every 10 iterations
                    if conn is None and accept_check_counter % 10 == 0:
                        try:
                            conn, address = SERVER.accept()
                            conn.settimeout(0.01)
                            self.status_updated.emit(f"Client connected: {address}")
                            print("\tClient connected: " + str(address))
                        except socket.timeout:
                            pass  # No connection yet, continue reading data
                    accept_check_counter += 1

                    # Check for current change requests
                    self.check_and_update_current()

                    # Read data from device
                    msg = next(gen_msg)

                    # translate from new names to old names
                    tmp_dict = {}
                    tmp_dict["SLED_Current (mA)"] = msg.parsed['sld_current_sense']
                    tmp_dict["SLED_Temp (C)"] = msg.parsed['sld_temp']
                    tmp_dict["TEC_Current (mA)"] = msg.parsed['tec_current_sense']

                    sag_pwr_uW = msg.parsed['sagnac_power_monitor']
                    tmp_dict["SAG_PWR (V)"] = helper.get_sagnac_power_v(sag_pwr_uW)

                    sld_pwr_uW = msg.parsed['sld_power_sense']
                    tmp_dict["Target SAG_PWR (V)"] = helper.get_target_sagnac_pwr(sld_pwr_uW)

                    tmp_dict["Photo Current (uA)"] = helper.get_photo_current(sld_pwr_uW)

                    # Create values list directly from tmp_dict
                    values = [tmp_dict[key] for key in data_key]

                    # Create string for client if needed with $START/$END format
                    if conn is not None:
                        values_str = ",".join(["{:f}".format(tmp_dict[key]) for key in data_key])
                        out_msg_str = f"$START,{values_str},$END"
                        try:
                            conn.send(out_msg_str.encode())
                        except socket.error:
                            # Client disconnected
                            self.status_updated.emit("Client disconnected")
                            print("\tClient disconnected")
                            try:
                                conn.close()
                            except:
                                pass
                            conn = None

                    # Always emit data for GUI update (regardless of client connection)
                    self.data_updated.emit(values)

                except StopIteration:
                    # End of data stream
                    break
                except Exception as e:
                    if self.server_running:
                        self.status_updated.emit(f"Communication error: {str(e)}")
                        print(f"\tCommunication error: {str(e)}")
                    break

            self.status_updated.emit("Server stopped")
            if conn is not None:
                try:
                    conn.close()
                except:
                    pass
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

        # Create grid layout to show all values on one page
        self.values_widget = QtWidgets.QWidget()
        self.values_layout = QtWidgets.QGridLayout(self.values_widget)
        self.values_layout.setSpacing(20)

        # Create a text display for each data key
        self.value_labels = []

        colors = ['#FF6B6B', '#4ECDC4', '#45B7D1', '#FFA07A', '#98D8C8', '#C3B1E1']  # Different colors for each value

        for i, key in enumerate(data_key):
            # Create container for this value
            value_container = QtWidgets.QWidget()
            value_container.setStyleSheet(f"background-color: #f5f5f5; border-radius: 10px;")
            container_layout = QtWidgets.QVBoxLayout(value_container)

            # Create label for key name
            name_label = QtWidgets.QLabel(key)
            name_label.setAlignment(QtCore.Qt.AlignCenter)
            name_font = QtGui.QFont()
            name_font.setPointSize(12)
            name_font.setBold(True)
            name_label.setFont(name_font)
            name_label.setStyleSheet("color: #333; padding: 10px;")

            # Create large label for displaying value
            value_label = QtWidgets.QLabel("--")
            value_label.setAlignment(QtCore.Qt.AlignCenter)
            font = QtGui.QFont()
            font.setPointSize(32)
            font.setBold(True)
            value_label.setFont(font)
            value_label.setStyleSheet(f"color: {colors[i]}; padding: 20px;")

            container_layout.addWidget(name_label)
            container_layout.addWidget(value_label)

            # Add to grid layout (3 columns, 2 rows)
            row = i // 3
            col = i % 3
            self.values_layout.addWidget(value_container, row, col)

            # Store reference to value label
            self.value_labels.append(value_label)

        # Add layouts to the main layout
        self.layout.addLayout(self.control_layout)
        self.layout.addLayout(self.current_control_layout)
        self.layout.addWidget(self.values_widget)

        # Thread and signal connections
        self.server_thread = None
        self.start_button.clicked.connect(self.start_server)
        self.stop_button.clicked.connect(self.stop_server)
        self.apply_current_button.clicked.connect(self.apply_current_change)

    def update_plot(self, values):
        """Update all text displays with new data"""
        for i in range(len(data_key)):
            # Update text label with current value
            self.value_labels[i].setText(f'{values[i]:.3f}')

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
    win.setWindowTitle("FNW Server V08 - Real-time Monitor")
    win.show()
    win.resize(1000, 600)
    win.raise_()

    app.exec_()

if __name__ == "__main__":
    main()
