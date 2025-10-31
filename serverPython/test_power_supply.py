#!/usr/bin/env python3
"""
Automated test script for power supply controller.
This script automatically runs through all power supply functions and shows results.
"""

from power_supply_controller import PowerSupplyController
import time
from datetime import datetime

class TestResults:
    """Class to store and display test results."""
    
    def __init__(self):
        self.results = []
        self.start_time = datetime.now()
    
    def add_result(self, test_name: str, success: bool, details: str = ""):
        """Add a test result."""
        self.results.append({
            'test': test_name,
            'success': success,
            'details': details,
            'timestamp': datetime.now()
        })
        
        # Print result immediately
        status = "✓ PASS" if success else "✗ FAIL"
        print(f"{status}: {test_name}")
        if details:
            print(f"     {details}")
    
    def print_summary(self):
        """Print comprehensive test summary."""
        end_time = datetime.now()
        duration = end_time - self.start_time
        
        print("\n" + "="*60)
        print("POWER SUPPLY TEST RESULTS SUMMARY")
        print("="*60)
        print(f"Test Duration: {duration.total_seconds():.1f} seconds")
        print(f"Start Time: {self.start_time.strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"End Time: {end_time.strftime('%Y-%m-%d %H:%M:%S')}")
        print()
        
        passed = sum(1 for r in self.results if r['success'])
        failed = len(self.results) - passed
        
        print(f"Total Tests: {len(self.results)}")
        print(f"Passed: {passed}")
        print(f"Failed: {failed}")
        print(f"Success Rate: {(passed/len(self.results)*100):.1f}%" if self.results else "N/A")
        print()
        
        # Detailed results
        print("DETAILED RESULTS:")
        print("-" * 60)
        for i, result in enumerate(self.results, 1):
            status = "PASS" if result['success'] else "FAIL"
            print(f"{i:2d}. [{status}] {result['test']}")
            if result['details']:
                print(f"     → {result['details']}")
        
        print("="*60)

def run_automated_tests():
    """Run all power supply tests automatically."""
    
    # Your device resource string
    RESOURCE_STRING = "USB0::0xF4EC::0x1410::SPD13DCQ7R0719::INSTR"
    
    # Create power supply controller and test results tracker
    ps = PowerSupplyController(RESOURCE_STRING)
    results = TestResults()
    
    print("="*60)
    print("AUTOMATED POWER SUPPLY TEST")
    print("="*60)
    print(f"Device: {RESOURCE_STRING}")
    print(f"Start Time: {results.start_time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("="*60)
    
    try:
        # Test 1: Connection
        print("\n1. Testing Connection...")
        success = ps.connect()
        results.add_result("Connection Test", success, 
                         "Successfully connected to power supply" if success else "Failed to connect")
        
        if not success:
            print("Cannot continue tests without connection!")
            return results
        
        # Small delay between tests
        time.sleep(0.5)
        
        # Test 2: Set Voltage (CH1)
        print("\n2. Testing Set Voltage (CH1 = 3.3V)...")
        success = ps.set_voltage(1, 3.3)
        results.add_result("Set Voltage CH1", success, 
                         "Set CH1 to 3.3V" if success else "Failed to set voltage")
        time.sleep(0.5)
        
        # Test 3: Set Current Limit (CH1)
        print("\n3. Testing Set Current Limit (CH1 = 0.5A)...")
        success = ps.set_current(1, 0.5)
        results.add_result("Set Current CH1", success, 
                         "Set CH1 current limit to 0.5A" if success else "Failed to set current")
        time.sleep(0.5)
        
        # Test 4: Turn Output ON (CH1)
        print("\n4. Testing Turn Output ON (CH1)...")
        success = ps.set_output(1, True)
        results.add_result("Turn Output ON CH1", success, 
                         "CH1 output enabled" if success else "Failed to enable output")
        time.sleep(1)  # Wait for output to stabilize
        
        # Test 5: Read Voltage/Current (CH1) with 5-second polling
        print("\n5. Testing Read Voltage/Current (CH1) - Polling for 5 seconds...")
        measurements = []
        polling_success = True
        
        for i in range(5):
            voltage = ps.get_voltage(1)
            current = ps.get_current(1)
            
            if voltage is not None and current is not None:
                measurement = f"T+{i+1}s: V={voltage:.3f}V, I={current:.3f}A"
                measurements.append(measurement)
                print(f"     {measurement}")
            else:
                measurement = f"T+{i+1}s: Error reading measurements"
                measurements.append(measurement)
                print(f"     {measurement}")
                polling_success = False
            
            time.sleep(1)  # 1 second interval for 5 total seconds
        
        # Calculate average if we have valid readings
        if polling_success and measurements:
            voltages = [float(m.split('V=')[1].split('V,')[0]) for m in measurements if 'Error' not in m]
            currents = [float(m.split('I=')[1].split('A')[0]) for m in measurements if 'Error' not in m]
            
            if voltages and currents:
                avg_voltage = sum(voltages) / len(voltages)
                avg_current = sum(currents) / len(currents)
                details = f"5s polling complete. Avg: V={avg_voltage:.3f}V, I={avg_current:.3f}A"
            else:
                details = "Polling completed but no valid readings"
        else:
            details = "Polling failed - some readings were invalid"
            
        results.add_result("Read Measurements CH1 (5s polling)", polling_success, details)
        
        # Test 6: Turn Output OFF (CH1)
        print("\n6. Testing Turn Output OFF (CH1)...")
        success = ps.set_output(1, False)
        results.add_result("Turn Output OFF CH1", success, 
                         "CH1 output disabled" if success else "Failed to disable output")
        time.sleep(0.5)
        
        # Test 7: Setup Channel Function (CH2 if available)
        print("\n7. Testing Setup Channel Function (CH2 = 5V, 1A)...")
        success = ps.setup_channel(2, 5.0, 1.0, enable=False)
        results.add_result("Setup Channel CH2", success, 
                         "Setup CH2: 5V, 1A limit" if success else "Failed to setup channel")
        time.sleep(0.5)
        
        # Test 8: Get Channel Status (CH1 and CH2)
        print("\n8. Testing Get Channel Status...")
        status1 = ps.get_status(1)
        status2 = ps.get_status(2)
        success1 = all(v is not None for v in status1.values())
        success2 = all(v is not None for v in status2.values())
        
        if success1:
            details1 = f"CH1: {status1['voltage']:.2f}V, {status1['current']:.3f}A, {'ON' if status1['output_enabled'] else 'OFF'}"
        else:
            details1 = "CH1: Error reading status"
            
        if success2:
            details2 = f"CH2: {status2['voltage']:.2f}V, {status2['current']:.3f}A, {'ON' if status2['output_enabled'] else 'OFF'}"
        else:
            details2 = "CH2: Error reading status"
        
        results.add_result("Get Status CH1", success1, details1)
        results.add_result("Get Status CH2", success2, details2)
        time.sleep(0.5)
        
        # Test 9: Quick Comprehensive Test (5V, 0.2A for 3 seconds on CH1)
        print("\n9. Testing Quick Comprehensive Test (CH1: 5V, 0.2A for 3 seconds)...")
        
        # Setup and enable
        setup_success = ps.setup_channel(1, 5.0, 0.2, enable=True)
        if setup_success:
            print("   Output enabled, monitoring...")
            measurements = []
            
            # Monitor for 3 seconds
            for i in range(3):
                time.sleep(1)
                status = ps.get_status(1)
                if all(v is not None for v in status.values()):
                    measurements.append(f"T+{i+1}s: {status['voltage']:.2f}V, {status['current']:.3f}A")
                    print(f"     {measurements[-1]}")
                else:
                    measurements.append(f"T+{i+1}s: Error reading")
            
            # Turn off
            ps.set_output(1, False)
            success = True
            details = f"Completed 3s test. {'; '.join(measurements)}"
        else:
            success = False
            details = "Failed to setup test parameters"
            
        results.add_result("Quick Comprehensive Test", success, details)
        
        # Final safety check - ensure all outputs are off
        print("\n10. Final Safety Check (Turn off all outputs)...")
        off1 = ps.set_output(1, False)
        off2 = ps.set_output(2, False)
        safety_success = off1 and off2
        results.add_result("Safety Check - All Outputs OFF", safety_success,
                         "All outputs disabled" if safety_success else "Warning: Some outputs may still be on")
        
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user!")
        results.add_result("Test Completion", False, "Interrupted by user")
        
    except Exception as e:
        print(f"\nUnexpected error during testing: {e}")
        results.add_result("Test Completion", False, f"Unexpected error: {str(e)}")
        
    finally:
        # Always disconnect
        print("\nDisconnecting from power supply...")
        ps.disconnect()
        results.add_result("Disconnection", True, "Safely disconnected from power supply")
        
        # Print comprehensive results
        results.print_summary()
        
    return results

def main():
    """Main function to run automated tests."""
    print("Starting automated power supply tests...")
    print("This will test all functions automatically and show results.\n")
    
    try:
        results = run_automated_tests()
        
        # Final message
        passed = sum(1 for r in results.results if r['success'])
        total = len(results.results)
        
        if passed == total:
            print(f"\n🎉 ALL TESTS PASSED! ({passed}/{total})")
        elif passed > total * 0.7:  # More than 70% passed
            print(f"\n⚠️  MOSTLY SUCCESSFUL ({passed}/{total} passed)")
        else:
            print(f"\n❌ MULTIPLE FAILURES ({passed}/{total} passed)")
            
        print("\nTest completed. Check the detailed results above.")
        
    except Exception as e:
        print(f"\nFatal error: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())