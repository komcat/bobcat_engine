import socket

def connect_to_server(host='localhost', port=8888):
    # Create a TCP/IP socket
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    
    # Connect to the server
    server_address = (host, port)
    print(f"Connecting to {host}:{port}")
    client_socket.connect(server_address)
    
    try:
        # Receive and print data from the server
        while True:
            data = client_socket.recv(1024).decode('utf-8')
            if not data:
                break
            print(f"Received: {data.strip()}")
    except KeyboardInterrupt:
        print("Client shutting down...")
    finally:
        client_socket.close()

if __name__ == "__main__":
    connect_to_server()