// find_visa_devices.cpp
// Simple program to list all VISA devices connected to the system

#include <visa.h>
#include <iostream>
#include <string>

int main() {
    ViSession defaultRM;
    ViStatus status;
    ViFindList findList;
    ViUInt32 numInstrs;
    char instrDesc[VI_FIND_BUFLEN];

    std::cout << "=================================================\n";
    std::cout << "        VISA Device Scanner\n";
    std::cout << "=================================================\n\n";

    // Open VISA Resource Manager
    status = viOpenDefaultRM(&defaultRM);
    if (status < VI_SUCCESS) {
        std::cerr << "ERROR: Failed to open VISA Resource Manager!\n";
        std::cerr << "Status code: 0x" << std::hex << status << std::dec << "\n";
        std::cerr << "\nPossible causes:\n";
        std::cerr << "  - NI-VISA Runtime not installed properly\n";
        std::cerr << "  - VISA drivers not loaded\n";
        return 1;
    }

    std::cout << "✓ VISA Resource Manager opened successfully\n\n";

    // Find all USB devices
    std::cout << "Searching for USB devices...\n";
    std::cout << "-------------------------------------------------\n";

    status = viFindRsrc(defaultRM, "USB?*INSTR", &findList, &numInstrs, instrDesc);

    if (status >= VI_SUCCESS) {
        std::cout << "Found " << numInstrs << " USB instrument(s):\n\n";

        if (numInstrs > 0) {
            int deviceNum = 1;
            do {
                std::cout << "[" << deviceNum << "] " << instrDesc << "\n";

                // Try to open and query device info
                ViSession instr;
                if (viOpen(defaultRM, instrDesc, VI_NULL, VI_NULL, &instr) >= VI_SUCCESS) {
                    char idn[256];
                    viSetAttribute(instr, VI_ATTR_TMO_VALUE, 2000); // 2 second timeout

                    // Try to query *IDN?
                    ViUInt32 retCount;
                    if (viWrite(instr, (ViBuf)"*IDN?\n", 6, &retCount) >= VI_SUCCESS) {
                        if (viRead(instr, (ViBuf)idn, 255, &retCount) >= VI_SUCCESS) {
                            idn[retCount] = '\0';
                            std::cout << "    Identity: " << idn;
                            if (idn[retCount-1] != '\n') std::cout << "\n";
                        }
                    }
                    viClose(instr);
                }

                std::cout << "\n";
                deviceNum++;

            } while (viFindNext(findList, instrDesc) == VI_SUCCESS);

            viClose(findList);
        }
    } else {
        std::cout << "No USB instruments found.\n\n";
    }

    // Also search for GPIB devices
    std::cout << "Searching for GPIB devices...\n";
    std::cout << "-------------------------------------------------\n";

    status = viFindRsrc(defaultRM, "GPIB?*INSTR", &findList, &numInstrs, instrDesc);

    if (status >= VI_SUCCESS && numInstrs > 0) {
        std::cout << "Found " << numInstrs << " GPIB instrument(s):\n\n";

        int deviceNum = 1;
        do {
            std::cout << "[" << deviceNum << "] " << instrDesc << "\n\n";
            deviceNum++;
        } while (viFindNext(findList, instrDesc) == VI_SUCCESS);

        viClose(findList);
    } else {
        std::cout << "No GPIB instruments found.\n\n";
    }

    // Search for TCPIP devices
    std::cout << "Searching for TCPIP/LAN devices...\n";
    std::cout << "-------------------------------------------------\n";

    status = viFindRsrc(defaultRM, "TCPIP?*INSTR", &findList, &numInstrs, instrDesc);

    if (status >= VI_SUCCESS && numInstrs > 0) {
        std::cout << "Found " << numInstrs << " TCPIP instrument(s):\n\n";

        int deviceNum = 1;
        do {
            std::cout << "[" << deviceNum << "] " << instrDesc << "\n\n";
            deviceNum++;
        } while (viFindNext(findList, instrDesc) == VI_SUCCESS);

        viClose(findList);
    } else {
        std::cout << "No TCPIP instruments found.\n\n";
    }

    // Close VISA
    viClose(defaultRM);

    std::cout << "=================================================\n";
    std::cout << "Scan complete!\n";
    std::cout << "=================================================\n";
    std::cout << "\nCopy the VISA resource string above and paste it\n";
    std::cout << "into your config/tec_devices.json file.\n\n";

    std::cout << "Press Enter to exit...";
    std::cin.get();

    return 0;
}
