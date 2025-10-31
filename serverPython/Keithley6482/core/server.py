"""TCP server module for handling client connections."""

import socket
import threading
import time
from typing import Optional, Callable


class DataServer:
    """TCP server for distributing data to clients."""
    
    def __init__(self, host: str, port: int):
        """
        Initialize the server.
        
        Args:
            host: Server host address
            port: Server port number
        """
        self.host = host
        self.port = port
        self.server_socket = None
        self.is_running = False
        self.accept_thread = None
        self.client_handler = None
        
    def setup(self) -> tuple[bool, str]:
        """
        Setup the server socket.
        
        Returns:
            Tuple of (success, message)
        """
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind((self.host, self.port))
            self.server_socket.listen(5)
            return True, f"Server listening on {self.host}:{self.port}"
        except Exception as e:
            return False, f"Failed to setup server: {e}"
    
    def start(self, client_handler: Callable):
        """
        Start accepting client connections.
        
        Args:
            client_handler: Callback function for handling new clients
        """
        self.is_running = True
        self.client_handler = client_handler
        self.accept_thread = threading.Thread(target=self._accept_clients, daemon=True)
        self.accept_thread.start()
    
    def stop(self):
        """Stop the server."""
        self.is_running = False
        if self.server_socket:
            try:
                self.server_socket.close()
            except:
                pass
    
    def _accept_clients(self):
        """Accept client connections."""
        while self.is_running:
            try:
                self.server_socket.settimeout(1.0)
                client_socket, addr = self.server_socket.accept()
                print(f"Accepted connection from {addr}")
                
                if self.client_handler:
                    self.client_handler(client_socket, addr)
                    
            except socket.timeout:
                continue
            except Exception as e:
                if self.is_running:
                    print(f"Server error: {e}")
                break