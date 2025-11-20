#!/usr/bin/env python3
"""
Single-Channel TCP Server Template
Compatible with bobcat_engine TcpClientManager (protocol_type: "single")

Protocol: One float value per line
Format: "value\n"
Example: "1.234\n5.678\n"

How to use:
1. Modify generate_data() to return your actual sensor value
2. Adjust HOST/PORT as needed
3. Update UPDATE_RATE_HZ for your application
4. Run: python server_template_single.py
"""

import socket
import time

# ========== CONFIGURATION ==========
HOST = "127.0.0.1"  # Server IP address
PORT = 8888         # Server port
UPDATE_RATE_HZ = 100  # Data sending rate (samples per second)
# ===================================

def generate_data():
    """
    Replace this with your actual data source!

    Examples:
    - Read from serial port
    - Query hardware device
    - Read from file
    - Generate from algorithm

    Returns:
        float: The value to send to client
    """
    # TODO: Replace with your actual data source
    import random
    return 5.0 + random.uniform(-0.5, 0.5)


def run_server():
    """Main server loop"""

    # Create TCP socket
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Bind and listen
    server.bind((HOST, PORT))
    server.listen(1)

    print("=" * 60)
    print("Single-Channel TCP Server")
    print("=" * 60)
    print(f"Listening on {HOST}:{PORT}")
    print(f"Update rate: {UPDATE_RATE_HZ} Hz")
    print("Waiting for client connection...")
    print()

    # Accept connection (blocking)
    conn, address = server.accept()
    print(f"✓ Client connected: {address}")
    print("Streaming data... (Ctrl+C to stop)")
    print()

    sample_count = 0
    update_interval = 1.0 / UPDATE_RATE_HZ

    try:
        while True:
            # Get data from your source
            value = generate_data()

            # Format: single float + newline
            message = f"{value:.6f}\n"

            # Send to client
            try:
                conn.send(message.encode())
                sample_count += 1

                # Print status every 100 samples
                if sample_count % 100 == 0:
                    print(f"Sent {sample_count} samples | Latest: {value:.6f}")

            except socket.error as e:
                print(f"\n✗ Client disconnected: {e}")
                print("Waiting for new connection...")
                conn.close()

                # Wait for new connection
                conn, address = server.accept()
                print(f"✓ Client reconnected: {address}")
                sample_count = 0

            # Maintain update rate
            time.sleep(update_interval)

    except KeyboardInterrupt:
        print("\n\nShutting down server...")
    except Exception as e:
        print(f"\n✗ Error: {e}")
    finally:
        conn.close()
        server.close()
        print("Server stopped.")


if __name__ == "__main__":
    run_server()
