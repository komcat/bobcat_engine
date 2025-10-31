import socket
import time
import random
import threading
import sys
import struct  # For binary data packing

def handle_client(client_socket, client_address, binary_mode=False):
    """Handle an individual client connection by sending random float values.
    
    Args:
        client_socket: The socket connected to the client
        client_address: The address of the client
        binary_mode: If True, send binary-packed floats; if False, send text
    """
    print(f"Client connected: {client_address}")
    try:
        while True:
            # Generate random float between 0.1 and 0.9
            random_value = random.uniform(0.5, 1.6)
            
            if binary_mode:
                # Pack as 4-byte binary float (C/C++ compatible)
                message = struct.pack('f', random_value)
            else:
                # Send as text with newline delimiter
                message = f"{random_value}\n".encode('utf-8')
            
            try:
                # Send the random value to the client
                client_socket.sendall(message)
                
                # Print the value on the console
                sys.stdout.write(f"\rEmitting: {random_value:.4f}  ")
                sys.stdout.flush()
                
                # Wait for 200ms
                time.sleep(0.150)
            except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError, OSError) as e:
                print(f"\nClient {client_address} disconnected: {str(e)}")
                break
    except Exception as e:
        print(f"\nError handling client {client_address}: {str(e)}")
    finally:
        try:
            client_socket.close()
            print(f"Connection closed with {client_address}")
        except:
            pass  # Socket might already be closed

def start_server(host='127.0.0.2', port=8888, binary_mode=False):
    """Start a TCP server that listens for connections and spawns client handlers.
    
    Args:
        host: Host IP to bind to (default: all interfaces)
        port: Port to listen on
        binary_mode: If True, send binary floats; if False, send text
    """
    # Create a TCP/IP socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    # Set socket option to reuse address
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    # Bind the socket to the address and port
    server_address = (host, port)
    try:
        server_socket.bind(server_address)
        
        # Listen for incoming connections (max 5 queued connections)
        server_socket.listen(5)
        print(f"Server started on {host}:{port}")
        print(f"Mode: {'Binary' if binary_mode else 'Text'}")
        print("Waiting for clients to connect...")
        
        try:
            while True:
                # Wait for a connection
                try:
                    client_socket, client_address = server_socket.accept()
                    
                    # Set a timeout on the socket
                    client_socket.settimeout(2.0)
                    
                    # Start a new thread to handle the client
                    client_thread = threading.Thread(
                        target=handle_client,
                        args=(client_socket, client_address, binary_mode)
                    )
                    client_thread.daemon = True
                    client_thread.start()
                except Exception as e:
                    print(f"Error accepting connection: {str(e)}")
                    continue
        except KeyboardInterrupt:
            print("\nServer shutting down...")
        finally:
            server_socket.close()
    except OSError as e:
        print(f"Could not start server: {str(e)}")
        if e.errno == 98:  # Address already in use
            print("Port already in use. Try again later or use a different port.")
        server_socket.close()

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='Start a TCP server that sends random float values')
    parser.add_argument('--host', default='127.0.0.2', help='Host to bind to')
    parser.add_argument('--port', type=int, default=8888, help='Port to listen on')
    parser.add_argument('--binary', action='store_true', help='Send binary floats instead of text')
    
    args = parser.parse_args()
    
    start_server(args.host, args.port, args.binary)