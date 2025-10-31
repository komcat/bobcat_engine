import pyvisa
import socket

# Function to handle client requests
def handle_client(conn, addr, instr):
    print(f"Connected by {addr}")
    while True:
        try:
            data = conn.recv(1024).decode()
            if not data:
                print(f"Client {addr} disconnected gracefully")
                break
            #print(f"Received command: {data}")
            parts = data.split()
            command = parts[0]
            
            if len(parts) > 1:
                value = parts[1]
            else:
                value = None
            
            try:
                if command == "SET_LASER_CURRENT" and value is not None:
                    instr.write(f"source1:current:level:amplitude {value}")
                    response = f"Set LD current [A]: {instr.query('source1:current:level:amplitude?')}"
                elif command == "SET_TEC_TEMPERATURE" and value is not None:
                    instr.write(f"source2:temperature:spoint {value}")
                    response = f"Set TEC temperature [C]: {instr.query('source2:temperature:spoint?')}"
                elif command == "LASER_ON":
                    instr.write("output1:state on")
                    response = f"Laser state: {instr.query('output1:state?')}"
                elif command == "LASER_OFF":
                    instr.write("output1:state off")
                    response = f"Laser state: {instr.query('output1:state?')}"
                elif command == "TEC_ON":
                    instr.write("output2:state on")
                    response = f"TEC state: {instr.query('output2:state?')}"
                elif command == "TEC_OFF":
                    instr.write("output2:state off")
                    response = f"TEC state: {instr.query('output2:state?')}"
                elif command == "READ_LASER_CURRENT":
                    current = instr.query("sense3:current:dc:data?")
                    response = f"Current laser current [A]: {current}"
                elif command == "READ_TEC_TEMPERATURE":
                    try:
                        # Store original timeout
                        original_timeout = instr.timeout
                        # Set new timeout for temperature reading (2000 ms)
                        instr.timeout = 200
                        temp = instr.query("SENSe2:temperature:data?")
                        response = f"Current TEC temperature [C]: {temp}"
                    except pyvisa.errors.VisaIOError as e:
                        if "timeout" in str(e).lower():
                            response = "ERROR: Temperature reading timeout"
                        else:
                            response = f"ERROR: VISA error - {str(e)}"
                    finally:
                        # Restore original timeout
                        instr.timeout = original_timeout
                else:
                    response = "Unknown command or missing value"
                
                conn.sendall(response.encode())
            except pyvisa.errors.VisaIOError as e:
                error_response = f"ERROR: VISA communication error - {str(e)}"
                conn.sendall(error_response.encode())
                
        except (ConnectionResetError, ConnectionAbortedError):
            print(f"Client {addr} disconnected forcefully")
            break
        except Exception as e:
            print(f"Error: {e}")
            break
    conn.close()
    print(f"Connection with {addr} closed")

def main():
    # Setup VISA connection
    rm = pyvisa.ResourceManager()
    instr = rm.open_resource('USB0::0x1313::0x804F::M00930341::INSTR')
    # Set default timeout (optional)
    instr.timeout = 1000  # 1 second default timeout
    print("Used device:", instr.query("*IDN?"))

    # Setup socket server
    HOST = '127.0.0.88'
    PORT = 65432
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind((HOST, PORT))
        s.listen()
        print(f"Server listening on {HOST}:{PORT}")
        while True:
            conn, addr = s.accept()
            handle_client(conn, addr, instr)
    
    # Close VISA connection
    instr.close()
    rm.close()

if __name__ == "__main__":
    main()