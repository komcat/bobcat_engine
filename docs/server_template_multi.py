#!/usr/bin/env python3
"""
Multi-Channel TCP Server Template
Compatible with bobcat_engine TcpClientManager (protocol_type: "multi")

Protocol: CSV data with start/end markers
Format: "$START,val1,val2,...,valN,$END"
Example: "$START,1.234,5.678,9.012,$END"

How to use:
1. Modify CHANNEL_NAMES to match your channels
2. Modify generate_data() to return your actual sensor values
3. Adjust HOST/PORT as needed
4. Update UPDATE_RATE_HZ for your application
5. Run: python server_template_multi.py
"""

import socket
import time

# ========== CONFIGURATION ==========
HOST = "127.0.0.1"  # Server IP address
PORT = 65432        # Server port
UPDATE_RATE_HZ = 200  # Data sending rate (samples per second)

# Define your channel names (MUST match order in generate_data())
CHANNEL_NAMES = [
    "Channel_1",
    "Channel_2",
    "Channel_3",
    "Channel_4",
    "Channel_5",
    "Channel_6"
]
# ===================================

def generate_data():
    """
    Replace this with your actual data source!

    IMPORTANT: Must return list of floats matching CHANNEL_NAMES length

    Examples:
    - Read from multi-channel ADC
    - Query multiple sensors
    - Calculate derived values
    - Read from data acquisition system

    Returns:
        list[float]: List of values (one per channel)
    """
    # TODO: Replace with your actual data source
    import random

    return [
        random.uniform(90, 110),    # Channel_1
        random.uniform(100, 110),   # Channel_2
        random.uniform(24, 26),     # Channel_3
        random.uniform(0.5, 0.6),   # Channel_4
        random.uniform(0.05, 0.07), # Channel_5
        random.uniform(-80, -60)    # Channel_6
    ]


def format_message(values):
    """
    Format multi-channel data into protocol message

    Args:
        values: List of float values

    Returns:
        str: Formatted message "$START,v1,v2,...,vN,$END"
    """
    values_str = ",".join([f"{v:f}" for v in values])
    return f"$START,{values_str},$END"


def run_server():
    """Main server loop"""

    # Validate configuration
    num_channels = len(CHANNEL_NAMES)
    if num_channels == 0:
        print("ERROR: CHANNEL_NAMES is empty!")
        return

    # Create TCP socket
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Bind and listen
    server.bind((HOST, PORT))
    server.listen(1)

    print("=" * 60)
    print("Multi-Channel TCP Server")
    print("=" * 60)
    print(f"Listening on {HOST}:{PORT}")
    print(f"Update rate: {UPDATE_RATE_HZ} Hz")
    print(f"Channels: {num_channels}")
    for i, name in enumerate(CHANNEL_NAMES):
        print(f"  [{i}] {name}")
    print()
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
            values = generate_data()

            # Validate channel count
            if len(values) != num_channels:
                print(f"WARNING: Expected {num_channels} values, got {len(values)}")
                values = values[:num_channels]  # Truncate
                # Pad with zeros if too short
                while len(values) < num_channels:
                    values.append(0.0)

            # Format message
            message = format_message(values)

            # Send to client
            try:
                conn.send(message.encode())
                sample_count += 1

                # Print status every 200 samples
                if sample_count % 200 == 0:
                    print(f"\n--- Sample #{sample_count} ---")
                    for i, val in enumerate(values):
                        print(f"  {CHANNEL_NAMES[i]:20s}: {val:10.3f}")

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
        import traceback
        traceback.print_exc()
    finally:
        conn.close()
        server.close()
        print("Server stopped.")


if __name__ == "__main__":
    run_server()
