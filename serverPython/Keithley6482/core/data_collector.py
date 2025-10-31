"""Data collection module for handling dual-channel multimeter measurements."""

import threading
import time
import numpy as np
from typing import List, Optional, Dict
from PyQt5.QtCore import QObject, pyqtSignal


class DualChannelDataCollector(QObject):
    """Handles data collection from dual-channel multimeter with separate client distribution."""
    
    # Signals for channel 1
    ch1_data_updated = pyqtSignal(float)
    ch1_histogram_updated = pyqtSignal(list)
    ch1_client_count_changed = pyqtSignal(int)
    
    # Signals for channel 2
    ch2_data_updated = pyqtSignal(float)
    ch2_histogram_updated = pyqtSignal(list)
    ch2_client_count_changed = pyqtSignal(int)
    
    def __init__(self, multimeter, parent=None):
        """
        Initialize the dual-channel data collector.
        
        Args:
            multimeter: Multimeter interface instance
            parent: Parent QObject
        """
        super().__init__(parent)
        self.multimeter = multimeter
        self.running = False
        
        # Separate client lists for each channel
        self.ch1_clients = []
        self.ch2_clients = []
        
        # Separate locks for thread safety
        self.ch1_lock = threading.Lock()
        self.ch2_lock = threading.Lock()
        
        # Separate timing data for each channel
        self.ch1_elapsed_times = []
        self.ch2_elapsed_times = []
        
        # Data counts
        self.ch1_data_count = 0
        self.ch2_data_count = 0
        
        self.measurement_delay = 0.05
        self.histogram_interval = 10
        
    def start(self):
        """Start data collection threads."""
        self.running = True
        
        # Start data collection thread
        data_thread = threading.Thread(target=self._collect_data, daemon=True)
        data_thread.start()
        
        # Start histogram calculation threads for each channel
        ch1_hist_thread = threading.Thread(
            target=lambda: self._calculate_histogram(1), daemon=True
        )
        ch1_hist_thread.start()
        
        ch2_hist_thread = threading.Thread(
            target=lambda: self._calculate_histogram(2), daemon=True
        )
        ch2_hist_thread.start()
    
    def stop(self):
        """Stop data collection."""
        self.running = False
    
    def _collect_data(self):
        """Main data collection loop for both channels."""
        while self.running:
            start_time = time.time()
            
            # Get measurements from both channels
            measurements = self.multimeter.read_measurement()
            
            # Process channel 1
            if 1 in measurements and measurements[1] is not None:
                value_ch1 = measurements[1]
                self.ch1_data_updated.emit(value_ch1)
                self.ch1_data_count += 1
                self._broadcast_to_channel_clients(1, value_ch1)
                
                # Track timing for channel 1
                elapsed_ch1 = time.time() - start_time
                self.ch1_elapsed_times.append(elapsed_ch1)
            
            # Process channel 2
            if 2 in measurements and measurements[2] is not None:
                value_ch2 = measurements[2]
                self.ch2_data_updated.emit(value_ch2)
                self.ch2_data_count += 1
                self._broadcast_to_channel_clients(2, value_ch2)
                
                # Track timing for channel 2
                elapsed_ch2 = time.time() - start_time
                self.ch2_elapsed_times.append(elapsed_ch2)
            
            time.sleep(self.measurement_delay)
    
    def _broadcast_to_channel_clients(self, channel: int, value: float):
        """
        Send data to clients connected to a specific channel.
        
        Args:
            channel: Channel number (1 or 2)
            value: Measurement value to send
        """
        if channel == 1:
            lock = self.ch1_lock
            clients = self.ch1_clients
            signal = self.ch1_client_count_changed
        else:
            lock = self.ch2_lock
            clients = self.ch2_clients
            signal = self.ch2_client_count_changed
        
        with lock:
            disconnected = []
            
            for client in clients:
                try:
                    client.sendall(f"{value}\n".encode('utf-8'))
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    print(f"Channel {channel} client disconnected unexpectedly")
                    disconnected.append(client)
            
            # Remove disconnected clients
            for client in disconnected:
                if client in clients:
                    clients.remove(client)
            
            if disconnected:
                signal.emit(len(clients))
    
    def _calculate_histogram(self, channel: int):
        """
        Calculate and emit histogram data for a specific channel.
        
        Args:
            channel: Channel number (1 or 2)
        """
        while self.running:
            time.sleep(self.histogram_interval)
            
            if channel == 1:
                lock = self.ch1_lock
                elapsed_times = self.ch1_elapsed_times
                signal = self.ch1_histogram_updated
            else:
                lock = self.ch2_lock
                elapsed_times = self.ch2_elapsed_times
                signal = self.ch2_histogram_updated
            
            with lock:
                if elapsed_times:
                    hist, bin_edges = np.histogram(elapsed_times, bins=20)
                    signal.emit([hist.tolist(), bin_edges.tolist()])
                    elapsed_times.clear()
    
    def add_client(self, channel: int, client_socket):
        """
        Add a new client socket to a specific channel.
        
        Args:
            channel: Channel number (1 or 2)
            client_socket: Client socket object
        """
        if channel == 1:
            with self.ch1_lock:
                self.ch1_clients.append(client_socket)
                self.ch1_client_count_changed.emit(len(self.ch1_clients))
        else:
            with self.ch2_lock:
                self.ch2_clients.append(client_socket)
                self.ch2_client_count_changed.emit(len(self.ch2_clients))
    
    def remove_client(self, channel: int, client_socket):
        """
        Remove a client socket from a specific channel.
        
        Args:
            channel: Channel number (1 or 2)
            client_socket: Client socket object
        """
        if channel == 1:
            with self.ch1_lock:
                if client_socket in self.ch1_clients:
                    self.ch1_clients.remove(client_socket)
                    self.ch1_client_count_changed.emit(len(self.ch1_clients))
        else:
            with self.ch2_lock:
                if client_socket in self.ch2_clients:
                    self.ch2_clients.remove(client_socket)
                    self.ch2_client_count_changed.emit(len(self.ch2_clients))
    
    def get_client_count(self, channel: int) -> int:
        """
        Get the current number of connected clients for a channel.
        
        Args:
            channel: Channel number (1 or 2)
            
        Returns:
            Number of connected clients
        """
        if channel == 1:
            with self.ch1_lock:
                return len(self.ch1_clients)
        else:
            with self.ch2_lock:
                return len(self.ch2_clients)
    
    def get_data_count(self, channel: int) -> int:
        """Get the data count for a specific channel."""
        return self.ch1_data_count if channel == 1 else self.ch2_data_count