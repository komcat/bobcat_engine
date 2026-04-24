cmake_minimum_required(VERSION 3.8)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# ============================================================================ 
# Version Configuration
# ============================================================================
set(BOBCAT_VERSION_MAJOR 1)
set(BOBCAT_VERSION_MINOR 9)

# Auto-increment build number from file
set(BUILD_NUMBER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/build_number.txt")

if(EXISTS ${BUILD_NUMBER_FILE})
    # Read current build number
    file(READ ${BUILD_NUMBER_FILE} BOBCAT_VERSION_BUILD)
    string(STRIP ${BOBCAT_VERSION_BUILD} BOBCAT_VERSION_BUILD)

    # Increment build number
    math(EXPR BOBCAT_VERSION_BUILD "${BOBCAT_VERSION_BUILD} + 1")

    # Write new build number back to file
    file(WRITE ${BUILD_NUMBER_FILE} "${BOBCAT_VERSION_BUILD}\n")

    message(STATUS "Build number auto-incremented to: ${BOBCAT_VERSION_BUILD}")
else()
    # Create file if it doesn't exist
    set(BOBCAT_VERSION_BUILD 1)
    file(WRITE ${BUILD_NUMBER_FILE} "${BOBCAT_VERSION_BUILD}\n")
    message(STATUS "Created build number file starting at: ${BOBCAT_VERSION_BUILD}")
endif()

# Allow override via command line: cmake -DBOBCAT_VERSION_BUILD_OVERRIDE=123
if(DEFINED BOBCAT_VERSION_BUILD_OVERRIDE)
    set(BOBCAT_VERSION_BUILD ${BOBCAT_VERSION_BUILD_OVERRIDE})
    message(STATUS "Build number overridden to: ${BOBCAT_VERSION_BUILD}")
endif()

project("bobcat_engine" VERSION ${BOBCAT_VERSION_MAJOR}.${BOBCAT_VERSION_MINOR}.${BOBCAT_VERSION_BUILD})

message(STATUS "========================================")
message(STATUS "  Bobcat Engine Version: ${PROJECT_VERSION}")
message(STATUS "  Major: ${BOBCAT_VERSION_MAJOR}")
message(STATUS "  Minor: ${BOBCAT_VERSION_MINOR}")
message(STATUS "  Build: ${BOBCAT_VERSION_BUILD}")
message(STATUS "========================================")

# Configure version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/version.h"
    @ONLY
)

# ============================================================================
# Simulation Mode Configuration
# ============================================================================
# Option to enable simulation mode (default: OFF for hardware mode)
option(BOBCAT_SIMULATION "Enable simulation mode (skip hardware connections)" OFF)

if(BOBCAT_SIMULATION)
    message(STATUS "========================================")
    message(STATUS "  BOBCAT SIMULATION MODE: ENABLED")
    message(STATUS "  Hardware connections will be skipped")
    message(STATUS "========================================")
else()
    message(STATUS "========================================")
    message(STATUS "  BOBCAT HARDWARE MODE: ENABLED")
    message(STATUS "  Will connect to physical devices")
    message(STATUS "========================================")
endif()

# Add this after your project() or set(CMAKE_CXX_STANDARD ...) line

if(MSVC)
    # Suppress warning C4828 (disallowed character in source file)
    add_compile_options(/wd4828)

    # If you also want to suppress C4566 (which you had before)
    add_compile_options(/wd4566)
endif()

# Point to MSVC version - support multiple Qt installations
if(EXISTS "C:/Qt/6.10.0/msvc2022_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.0/msvc2022_64")
    message(STATUS "Using Qt 6.10.0")
elseif(EXISTS "C:/Qt/6.10.1/msvc2022_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.1/msvc2022_64")
    message(STATUS "Using Qt 6.10.1")
elseif(EXISTS "C:/Qt/6.9.3/msvc2022_64")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3/msvc2022_64")
    message(STATUS "Using Qt 6.9.3")
else()
    message(FATAL_ERROR "Qt not found. Please install Qt 6.9.3 or 6.10.0 at the expected location.")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add MSVC-specific flags
if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

# Find Qt
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts OpenGLWidgets)

# ============================================================================
# Lua 5.4 Configuration (embedded scripting engine)
# ============================================================================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildLua.cmake)

# ============================================================================
# spdlog Configuration (header-only)
# ============================================================================
set(SPDLOG_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/include")
if(EXISTS "${SPDLOG_INCLUDE_DIR}/spdlog/spdlog.h")
    message(STATUS "spdlog found at: ${SPDLOG_INCLUDE_DIR}")
    set(SPDLOG_FOUND TRUE)
else()
    message(WARNING "spdlog not found at ${SPDLOG_INCLUDE_DIR}")
    set(SPDLOG_FOUND FALSE)
endif()

# ============================================================================
# nlohmann JSON Configuration
# ============================================================================
set(NLOHMANN_JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

# Check if nlohmann json exists
if(EXISTS "${NLOHMANN_JSON_DIR}/nlohmann/json.hpp")
    message(STATUS "nlohmann json found at: ${NLOHMANN_JSON_DIR}/nlohmann")
    set(NLOHMANN_JSON_FOUND TRUE)
else()
    message(WARNING "nlohmann json not found at ${NLOHMANN_JSON_DIR}/nlohmann")
    set(NLOHMANN_JSON_FOUND FALSE)
endif()

# ============================================================================
# ACS Controller Configuration
# ============================================================================
set(ACSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/ACSC")

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        set(ACSC_LIBRARY "${ACSC_DIR}/ACSCL_x64.LIB")
    else()
        # 32-bit build
        set(ACSC_LIBRARY "${ACSC_DIR}/ACSCL_x86.LIB")
    endif()
endif()

# Check if ACS library exists
if(EXISTS "${ACSC_LIBRARY}")
    message(STATUS "ACS Controller library found at: ${ACSC_LIBRARY}")
    set(ACSC_FOUND TRUE)
else()
    message(WARNING "ACS Controller library not found at ${ACSC_LIBRARY}. ACS features will be disabled.")
    set(ACSC_FOUND FALSE)
endif()

# ============================================================================
# PI GCS2 Controller Configuration
# ============================================================================
set(PI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/PI")
set(PI_INCLUDE_DIR "${PI_ROOT}")
set(PI_LIB_DIR "${PI_ROOT}")

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        set(PI_LIBRARY "${PI_LIB_DIR}/PI_GCS2_DLL_x64.lib")
        set(PI_DLL "${PI_LIB_DIR}/PI_GCS2_DLL_x64.dll")
    else()
        # 32-bit build
        set(PI_LIBRARY "${PI_LIB_DIR}/PI_GCS2_DLL.lib")
        set(PI_DLL "${PI_LIB_DIR}/PI_GCS2_DLL.dll")
    endif()
endif()

# Check if PI library exists
if(EXISTS "${PI_LIBRARY}")
    message(STATUS "PI GCS2 Controller library found at: ${PI_LIBRARY}")
    set(PI_FOUND TRUE)
else()
    message(WARNING "PI GCS2 Controller library not found at ${PI_LIBRARY}. PI features will be disabled.")
    set(PI_FOUND FALSE)
endif()

# ============================================================================
# Pylon SDK Configuration
# ============================================================================
set(PYLON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/pylon 8")
set(PYLON_INCLUDE_DIR "${PYLON_ROOT}/Development/include")
set(PYLON_LIB_DIR "${PYLON_ROOT}/Development/lib/x64")

# Check if Pylon installation exists
if(NOT EXISTS ${PYLON_ROOT})
    message(WARNING "Pylon SDK not found at ${PYLON_ROOT}. Pylon camera features will be disabled.")
    set(PYLON_FOUND FALSE)
else()
    message(STATUS "Pylon SDK found at: ${PYLON_ROOT}")
    message(STATUS "  - Include directory: ${PYLON_INCLUDE_DIR}")
    message(STATUS "  - Library directory: ${PYLON_LIB_DIR}")
    
    # Set Pylon libraries
    set(PYLON_LIBRARIES
        "${PYLON_LIB_DIR}/PylonBase_v9.lib"
        "${PYLON_LIB_DIR}/PylonUtility_v9.lib"
        "${PYLON_LIB_DIR}/GCBase_MD_VC141_v3_1_Basler_pylon.lib"
        "${PYLON_LIB_DIR}/GenApi_MD_VC141_v3_1_Basler_pylon.lib"
    )
    
    set(PYLON_FOUND TRUE)
endif()

# ============================================================================
# IDS uEye SDK Configuration
# ============================================================================
set(IDS_UEYE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/uEye")
set(IDS_UEYE_INCLUDE_DIR "${IDS_UEYE_DIR}/include")

# Handle different platforms for IDS uEye library files
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        set(IDS_UEYE_LIB_DIR "${IDS_UEYE_DIR}/lib")
        set(IDS_UEYE_BIN_DIR "${IDS_UEYE_DIR}/bin")
        set(IDS_UEYE_LIBRARY "${IDS_UEYE_LIB_DIR}/ueye_api_64.lib")
        set(IDS_UEYE_DLL "${IDS_UEYE_BIN_DIR}/ueye_api_64.dll")
    else()
        # 32-bit build
        set(IDS_UEYE_LIB_DIR "${IDS_UEYE_DIR}/lib")
        set(IDS_UEYE_BIN_DIR "${IDS_UEYE_DIR}/bin")
        set(IDS_UEYE_LIBRARY "${IDS_UEYE_LIB_DIR}/ueye_api.lib")
        set(IDS_UEYE_DLL "${IDS_UEYE_BIN_DIR}/ueye_api.dll")
    endif()
else()
    # Linux configuration (if needed later)
    set(IDS_UEYE_LIB_DIR "${IDS_UEYE_DIR}/lib")
    set(IDS_UEYE_LIBRARY "${IDS_UEYE_LIB_DIR}/libueye_api.so")
endif()

# Check if IDS uEye installation exists
if(NOT EXISTS ${IDS_UEYE_INCLUDE_DIR})
    message(WARNING "IDS uEye SDK not found at ${IDS_UEYE_DIR}. IDS camera features will be disabled.")
    set(IDS_UEYE_FOUND FALSE)
else()
    message(STATUS "IDS uEye SDK found at: ${IDS_UEYE_DIR}")
    message(STATUS "  - Include directory: ${IDS_UEYE_INCLUDE_DIR}")
    message(STATUS "  - Library: ${IDS_UEYE_LIBRARY}")
    if(WIN32)
        message(STATUS "  - DLL: ${IDS_UEYE_DLL}")
    endif()
    set(IDS_UEYE_FOUND TRUE)
endif()

# ============================================================================
# VISA SDK Configuration (Local)
# ============================================================================
set(VISA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/visa")
set(VISA_INCLUDE_DIR "${VISA_DIR}/include")

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        set(VISA_LIB_DIR "${VISA_DIR}/Lib_x64")
        set(VISA_LIBRARY "${VISA_LIB_DIR}/visa64.lib")
    else()
        # 32-bit build
        set(VISA_LIB_DIR "${VISA_DIR}/lib")
        set(VISA_LIBRARY "${VISA_LIB_DIR}/visa32.lib")
    endif()
endif()

# Check if VISA installation exists
if(NOT EXISTS ${VISA_INCLUDE_DIR})
    message(WARNING "VISA SDK not found at ${VISA_DIR}. Direct VISA adapter will be disabled.")
    set(VISA_FOUND FALSE)
else()
    message(STATUS "VISA SDK found at: ${VISA_DIR}")
    message(STATUS "  - Include directory: ${VISA_INCLUDE_DIR}")
    message(STATUS "  - Library: ${VISA_LIBRARY}")

    # Verify library file exists
    if(EXISTS ${VISA_LIBRARY})
        set(VISA_FOUND TRUE)
    else()
        message(WARNING "VISA library not found at ${VISA_LIBRARY}. Direct VISA adapter will be disabled.")
        set(VISA_FOUND FALSE)
    endif()
endif()

# ============================================================================
# VISA Device Scanner Utility
# ============================================================================
# Simple tool to find all connected VISA devices and their resource strings
if(VISA_FOUND)
    add_executable(find_visa_devices
        find_visa_devices.cpp
    )

    target_include_directories(find_visa_devices PRIVATE
        ${VISA_INCLUDE_DIR}
    )

    target_link_libraries(find_visa_devices PRIVATE
        ${VISA_LIBRARY}
    )

    set_target_properties(find_visa_devices PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )

    if(WIN32 AND MSVC)
        set_target_properties(find_visa_devices PROPERTIES
            LINK_FLAGS "/SUBSYSTEM:CONSOLE"
        )
    endif()

    message(STATUS "VISA Device Scanner utility: 'find_visa_devices' - run to discover connected VISA instruments")
else()
    message(STATUS "VISA Device Scanner utility disabled - VISA SDK not found")
endif()

# ============================================================================
# EziIO SDK Configuration
# ============================================================================
set(EZIIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/eziio")
set(EZIIO_INCLUDE_DIR_x86 "${EZIIO_DIR}/Include_x86_CPP")
set(EZIIO_INCLUDE_DIR_x64 "${EZIIO_DIR}/Include_x64_CPP")

# Handle different platforms for EziIO library files
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        set(EZIIO_INCLUDE_DIR "${EZIIO_INCLUDE_DIR_x64}")
        set(EZIIO_LIBRARY "${EZIIO_INCLUDE_DIR_x64}/EziMOTIONPlusE.lib")
        set(EZIIO_DLL "${EZIIO_INCLUDE_DIR_x64}/EziMOTIONPlusE.dll")
    else()
        # 32-bit build
        set(EZIIO_INCLUDE_DIR "${EZIIO_INCLUDE_DIR_x86}")
        set(EZIIO_LIBRARY "${EZIIO_INCLUDE_DIR_x86}/EziMOTIONPlusE.lib")
        set(EZIIO_DLL "${EZIIO_INCLUDE_DIR_x86}/EziMOTIONPlusE.dll")
    endif()
endif()

# Check if EziIO installation exists
if(NOT EXISTS ${EZIIO_INCLUDE_DIR})
    message(WARNING "EziIO SDK not found at ${EZIIO_DIR}. EziIO features will be disabled.")
    set(EZIIO_FOUND FALSE)
else()
    message(STATUS "EziIO SDK found at: ${EZIIO_DIR}")
    message(STATUS "  - Include directory: ${EZIIO_INCLUDE_DIR}")
    message(STATUS "  - Library: ${EZIIO_LIBRARY}")
    if(WIN32)
        message(STATUS "  - DLL: ${EZIIO_DLL}")
    endif()
    set(EZIIO_FOUND TRUE)
endif()

# ============================================================================
# Halcon SDK Configuration
# ============================================================================
option(BOBCAT_ENABLE_HALCON "Enable Halcon vision features" ON)

set(HALCON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/halcon")
set(HALCON_INCLUDE_DIR "${HALCON_ROOT}/include")
set(HALCON_LIB_DIR "${HALCON_ROOT}/lib/x64-win64")

# Check if Halcon is enabled and installation exists
if(NOT BOBCAT_ENABLE_HALCON)
    message(STATUS "Halcon disabled by BOBCAT_ENABLE_HALCON=OFF")
    set(HALCON_FOUND FALSE)
elseif(NOT EXISTS ${HALCON_INCLUDE_DIR})
    message(WARNING "Halcon SDK not found at ${HALCON_ROOT}. Halcon vision features will be disabled.")
    set(HALCON_FOUND FALSE)
else()
    message(STATUS "Halcon SDK found at: ${HALCON_ROOT}")
    message(STATUS "  - Include directory: ${HALCON_INCLUDE_DIR}")
    message(STATUS "  - Library directory: ${HALCON_LIB_DIR}")

    # Set Halcon libraries
    set(HALCON_LIBRARY "${HALCON_LIB_DIR}/halconcpp.lib")
    set(HALCON_CORE_LIBRARY "${HALCON_LIB_DIR}/halcon.lib")

    if(EXISTS ${HALCON_LIBRARY})
        set(HALCON_FOUND TRUE)
        message(STATUS "  - C++ Library: ${HALCON_LIBRARY}")
        message(STATUS "  - Core Library: ${HALCON_CORE_LIBRARY}")
    else()
        message(WARNING "Halcon library not found at ${HALCON_LIBRARY}. Halcon features will be disabled.")
        set(HALCON_FOUND FALSE)
    endif()
endif()

# ============================================================================
# OpenCV Configuration
# ============================================================================
set(OPENCV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/opencv")
set(OPENCV_INCLUDE_DIR "${OPENCV_ROOT}/include")
set(OPENCV_LIB_DIR "${OPENCV_ROOT}/lib")
set(OPENCV_BIN_DIR "${OPENCV_ROOT}/bin")

# Check if OpenCV installation exists
if(NOT EXISTS ${OPENCV_INCLUDE_DIR})
    message(WARNING "OpenCV not found at ${OPENCV_ROOT}. OpenCV features will be disabled.")
    set(OPENCV_FOUND FALSE)
else()
    message(STATUS "OpenCV found at: ${OPENCV_ROOT}")
    message(STATUS "  - Include directory: ${OPENCV_INCLUDE_DIR}")
    message(STATUS "  - Library directory: ${OPENCV_LIB_DIR}")

    # Set OpenCV libraries (world package includes everything)
    # Version 4.12.0 = 4120
    set(OPENCV_LIBRARY_RELEASE "${OPENCV_LIB_DIR}/opencv_world4120.lib")
    set(OPENCV_LIBRARY_DEBUG "${OPENCV_LIB_DIR}/opencv_world4120d.lib")
    set(OPENCV_DLL_RELEASE "${OPENCV_BIN_DIR}/opencv_world4120.dll")
    set(OPENCV_DLL_DEBUG "${OPENCV_BIN_DIR}/opencv_world4120d.dll")
    set(OPENCV_FFMPEG_DLL "${OPENCV_BIN_DIR}/opencv_videoio_ffmpeg4120_64.dll")

    # Use generator expression to select correct library based on build type
    set(OPENCV_LIBRARY "$<IF:$<CONFIG:Debug>,${OPENCV_LIBRARY_DEBUG},${OPENCV_LIBRARY_RELEASE}>")
    set(OPENCV_DLL "$<IF:$<CONFIG:Debug>,${OPENCV_DLL_DEBUG},${OPENCV_DLL_RELEASE}>")

    if(EXISTS ${OPENCV_LIBRARY_RELEASE} OR EXISTS ${OPENCV_LIBRARY_DEBUG})
        set(OPENCV_FOUND TRUE)
        message(STATUS "  - Release Library: ${OPENCV_LIBRARY_RELEASE}")
        message(STATUS "  - Debug Library: ${OPENCV_LIBRARY_DEBUG}")
        message(STATUS "  - FFmpeg DLL: ${OPENCV_FFMPEG_DLL}")
    else()
        message(WARNING "OpenCV library not found. OpenCV features will be disabled.")
        set(OPENCV_FOUND FALSE)
    endif()
endif()

# ============================================================================
# Include Directories
# ============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include  # For generated version.h
    ${CMAKE_SOURCE_DIR}/include/camera
    ${CMAKE_SOURCE_DIR}/include/AppUI
    ${CMAKE_SOURCE_DIR}/include/AppUI/Common
    ${CMAKE_SOURCE_DIR}/include/AppUI/ConfigEditor
    ${CMAKE_SOURCE_DIR}/include/AppUI/CoordinateSystem
    ${CMAKE_SOURCE_DIR}/include/AppUI/Forms
    ${CMAKE_SOURCE_DIR}/include/AppUI/MotionPages
    ${CMAKE_SOURCE_DIR}/include/AppUI/IOPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/PneumaticPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/JogWindow
    ${CMAKE_SOURCE_DIR}/include/AppUI/RecipePage
    ${CMAKE_SOURCE_DIR}/include/AppUI/TECPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/LightingPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/PowerSupplyControlPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/SMUControlPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/RunProgramPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/SettingsPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/AccuracyTrainer
    ${CMAKE_SOURCE_DIR}/include/AppUI/CameraOffsetTrainer
    ${CMAKE_SOURCE_DIR}/include/AppUI/PlanarAdjustmentTrainer
    ${CMAKE_SOURCE_DIR}/include/AppUI/ZOffsetTrainer
    ${CMAKE_SOURCE_DIR}/include/AppUI/YawTrainer
    ${CMAKE_SOURCE_DIR}/include/AppUI/PlacementCorrectionTrainer
    ${CMAKE_SOURCE_DIR}/include/AppUI/EdgeTransferTrainer
    ${CMAKE_SOURCE_DIR}/include/AuxWindow
    ${CMAKE_SOURCE_DIR}/include/context
    ${CMAKE_SOURCE_DIR}/include/logger
    ${CMAKE_SOURCE_DIR}/include/data
    ${CMAKE_SOURCE_DIR}/include/dut
    ${CMAKE_SOURCE_DIR}/include/motions
    ${CMAKE_SOURCE_DIR}/include/motions/PI
    ${CMAKE_SOURCE_DIR}/include/motions/ACS
    ${CMAKE_SOURCE_DIR}/include/motions/interfaces
    ${CMAKE_SOURCE_DIR}/include/motions/scanning
    ${CMAKE_SOURCE_DIR}/include/products    
    ${CMAKE_SOURCE_DIR}/include/ProgramManager
    ${CMAKE_SOURCE_DIR}/include/IO
    ${CMAKE_SOURCE_DIR}/include/pneumatics
    ${CMAKE_SOURCE_DIR}/include/recipe
    ${CMAKE_SOURCE_DIR}/include/sequence
    ${CMAKE_SOURCE_DIR}/include/PSU
    ${CMAKE_SOURCE_DIR}/include/TEC
    ${CMAKE_SOURCE_DIR}/include/SMU
    ${CMAKE_SOURCE_DIR}/include/vision
    ${CMAKE_SOURCE_DIR}/include/vision/ui
    ${CMAKE_SOURCE_DIR}/include/lighting
    ${CMAKE_SOURCE_DIR}/include/settings
    ${CMAKE_SOURCE_DIR}/include/common
    ${CMAKE_SOURCE_DIR}/include/scripting
    ${CMAKE_SOURCE_DIR}/include/AppUI/ScriptRunnerPage
)

# External SDK include directories — stored for SYSTEM includes after target creation
# (suppresses third-party warnings with /external:W0)
if(NLOHMANN_JSON_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${NLOHMANN_JSON_DIR})
endif()
if(SPDLOG_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${SPDLOG_INCLUDE_DIR})
endif()
if(ACSC_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${ACSC_DIR})
endif()
if(PI_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${PI_INCLUDE_DIR})
endif()
if(PYLON_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${PYLON_INCLUDE_DIR})
endif()
if(IDS_UEYE_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${IDS_UEYE_INCLUDE_DIR})
endif()
if(VISA_FOUND)
    set(EXTERNAL_SDK_INCLUDES ${EXTERNAL_SDK_INCLUDES} ${VISA_INCLUDE_DIR})
endif()

# External SDK include directories are added as SYSTEM includes after the target
# (see target_include_directories below) to suppress third-party warnings with /external:W0

# ============================================================================
# Source Files
# ============================================================================
set(SOURCES
    bobcat_engine.cpp
    Application.cpp
    include/camera/CameraLiveView.cpp
    include/camera/CameraManager.cpp
    include/camera/CameraConfigManager.cpp
    include/camera/CameraHardwareFactory.cpp
    include/camera/CameraWorker.cpp
    include/camera/CameraView.cpp
    include/camera/CameraParameterWorker.cpp
    include/camera/MultiCameraView.cpp
    include/camera/LiveVideoSubscriber.cpp
    include/camera/CameraImageWidget.cpp
    include/camera/CameraImageWidgetGL.cpp
    include/camera/CameraSettingsWidget.cpp
    include/AppUI/MainWindow.cpp
    include/AppUI/TopBar.cpp
    include/AppUI/DarkTheme.cpp
    include/AppUI/ThemeManager.cpp
    include/AppUI/ConnectionSplashScreen.cpp
    include/AppUI/MainMenuPage.cpp
    include/AppUI/DataBackupPage.cpp
    include/AppUI/ScriptRunnerPage/ScriptRunnerPage.cpp
    include/AppUI/ScriptRunnerPage/LuaSyntaxHighlighter.cpp
    include/AppUI/HardwareInstrumentPage.cpp
    include/AppUI/PlaceholderPage.cpp
    include/logger/LogManager.cpp
    include/logger/LogStatusBar.cpp
    include/logger/LogViewerPage.cpp
    include/data/DataStore.cpp
    include/data/TcpDataClient.cpp
    include/data/TcpClientManager.cpp
    include/data/TcpClientWidget.cpp
    include/data/DataStoreWidget.cpp
    src/dut/DeviceUnderTest.cpp
    include/AppUI/ConfigEditor/ConfigEditorPage.cpp
    include/AppUI/Forms/TcpClientFormWidget.cpp
    include/AppUI/Forms/CameraFormWidget.cpp
    include/AppUI/Forms/SavedPositionsFormWidget.cpp
    include/AppUI/Forms/MotorDeviceFormWidget.cpp        
    include/AppUI/MotionPages/MotionControllerPageBase.cpp
    include/AppUI/MotionPages/PIControllerPage.cpp
    include/AppUI/MotionPages/ACSControllerPage.cpp
    include/AppUI/IOPage/IOControllerPage.cpp
    include/pneumatics/PneumaticSlide.cpp
    include/pneumatics/PneumaticManager.cpp
    include/AppUI/Forms/PneumaticSlidesFormWidget.cpp
    include/AppUI/Forms/IODevicesFormWidget.cpp
    include/AppUI/Forms/PowerSupplyFormWidget.cpp
    include/AppUI/Forms/TECDevicesFormWidget.cpp
    include/sequence/SequenceEngine.cpp
    include/sequence/SequenceContext.cpp
    include/scripting/ScriptEngine.cpp
    include/scripting/ScriptCommandHandler.cpp
    include/scripting/ScriptWorker.cpp
    include/sequence/SequenceContextCommandHandler.cpp
    include/sequence/SequenceWorker.cpp
    include/sequence/GraphMoveWorker.cpp
    include/sequence/MotionCommandHandler.cpp
    include/sequence/IOCommandHandler.cpp
    include/sequence/PneumaticCommandHandler.cpp
    include/sequence/CameraCommandHandler.cpp
    include/sequence/CalibrationCommandHandler.cpp
    include/sequence/CommandDispatcher.cpp
    include/motions/TransformationMatrixLoader.cpp
    include/motions/CoordinateTransformer.cpp
    include/motions/DeviceLinkManager.cpp
    include/motions/InterlockManager.cpp
    src/motions/CoordinateSystemManager.cpp
    include/AppUI/JogWindow/JogPanelWindow.cpp
    include/AppUI/JogWindow/JogWorker.cpp
    include/AppUI/JogWindow/JogKeyBindingHandler.cpp
    include/AppUI/JogWindow/QuickOutputWidget.cpp
    include/AppUI/MotionPages/PositionDot.cpp
    include/AppUI/MotionPages/MotionGraphicsView.cpp
    include/AppUI/MotionPages/MotionControlPanel.cpp
    include/AppUI/MotionPages/DeviceButtonStack.cpp
    include/AppUI/MotionPages/NewPositionDialog.cpp
    include/AppUI/MotionPages/MotionPage.cpp
    include/AppUI/MotionPages/InterlockWidget.cpp
    include/AppUI/MotionPages/CollapsiblePanel.cpp
    include/AppUI/CoordinateSystem/CreateSystemWidget.cpp
    include/AppUI/CoordinateSystem/TestSystemWidget.cpp
    include/AppUI/MotionPages/DevicePositionMarker.cpp
    include/motions/MotionJsonLoader.cpp
    include/AppUI/MotionPages/PositionAnchor.cpp
    include/AppUI/MotionPages/PositionLabel.cpp
    include/products/product_position_manager.cpp
    include/products/product_json_loader.cpp
    include/graph/MotionNode.cpp
    include/graph/MotionEdge.cpp
    include/graph/MotionGraph.cpp
    include/graph/MotionGraphManager.cpp
    include/AppUI/MotionPages/MotionNodeItem.cpp
    include/AppUI/MotionPages/MotionEdgeItem.cpp
    include/AppUI/MotionPages/GraphWidget.cpp
    include/AppUI/MotionPages/WaypointEditorDialog.cpp
    include/AppUI/MotionPages/PathPreview.cpp
    include/AppUI/MotionPages/OffsetMoveDialog.cpp
    include/motions/scanning/AdaptiveScanController.cpp
    include/motions/scanning/ScanContext.cpp
    include/motions/scanning/ScanDataCollector.cpp
    include/motions/scanning/CoarseToFineStrategy.cpp
    include/motions/scanning/RotationScanStrategy.cpp
    include/motions/scanning/RotationScanController.cpp
    include/logger/ILogger.cpp
    include/AppUI/MotionPages/ScanControlPanel.cpp
    include/sequence/SystemCommandHandler.cpp
    include/sequence/SequencePromptDialog.cpp
    include/sequence/GripperSequenceHelpers.cpp
    include/AppUI/RecipePage/RecipePage.cpp
    include/AppUI/RecipePage/RecipeTreeItemFactory.cpp
    include/AppUI/RecipePage/RecipeStepDelegate.cpp
    include/AppUI/RecipePage/ProcessSummary.cpp
    include/AppUI/RecipePage/RecipeRunWindow.cpp
    include/AppUI/RecipePage/ProcessVisibilityDialog.cpp
    include/AppUI/RecipePage/RecipeClipboard.cpp
    include/AppUI/RecipePage/PropertyTableWidget.cpp
    include/recipe/RecipeManager.cpp
    include/recipe/ProcessSequenceFactory.cpp
    include/recipe/ProcessPluginRegistry.cpp
    include/recipe/processes/SystemProcesses.cpp
    include/recipe/processes/ScriptProcesses.cpp
    include/recipe/processes/SubRecipeProcesses.cpp
    include/recipe/processes/MotionProcesses.cpp
    include/recipe/processes/DeviceLinkProcesses.cpp
    include/recipe/processes/IOProcesses.cpp
    include/recipe/processes/TECProcesses.cpp
    include/recipe/processes/PSUProcesses.cpp
    include/recipe/processes/DataStoreProcesses.cpp
    include/recipe/processes/DUTProcesses.cpp
    include/recipe/processes/ConditionalProcesses.cpp
    include/recipe/processes/CalibrationProcesses.cpp
    include/recipe/processes/AccuracyTrainerProcesses.cpp
    include/recipe/processes/EdgeTransferProcesses.cpp
    include/recipe/processes/PlacementCorrectionProcesses.cpp
    include/recipe/processes/PlanarAdjustmentProcesses.cpp
    include/recipe/processes/ZOffsetProcesses.cpp
    include/recipe/processes/YawTrainerProcesses.cpp
    include/recipe/processes/CameraOffsetProcesses.cpp
    include/recipe/processes/CoordinateAlignProcesses.cpp
    include/recipe/processes/YawCorrectionProcesses.cpp
    include/recipe/processes/YawCorrectionToTemplateProcesses.cpp
    include/recipe/processes/AutoFocusProcesses.cpp
    include/recipe/processes/UpdatePositionProcesses.cpp
    include/recipe/processes/CalibrateCameraToNeedleOffset.cpp
    include/recipe/processes/DispenseProcesses.cpp
    include/recipe/processes/SMUProcesses.cpp
    include/recipe/processes/AuxWindowProcesses.cpp
    include/recipe/processes/CameraProcesses.cpp
    include/recipe/processes/LightingProcesses.cpp
    include/recipe/processes/CustomServerProcesses.cpp
    include/customserver/BeamServerClient.cpp
    include/sequence/CustomServerCommandHandler.cpp
    include/AppUI/RecipePage/ProcessConfigWidget.cpp
    include/AppUI/RecipePage/DeviceSelectorWidgets.cpp
    include/recipe/RecipeWorker.cpp
    include/AppUI/RecipePage/RecipeProgressWidget.cpp
    include/AppUI/RecipePage/ContextViewerWindow.cpp
    include/AppUI/RecipePage/ContextVariablePreviewWidget.cpp
    include/AppUI/MotionPages/EditPositionDialog.cpp
    include/AppUI/RecipePage/PneumaticSlideSelectorDialog.cpp
    include/AppUI/RecipePage/MotionDeviceSelectorDialog.cpp
    include/TEC/CLD101xAdapter.cpp
    include/TEC/TECManager.cpp
    include/TEC/TECJsonLoader.cpp
    include/TEC/TECConnectionWorker.cpp
    include/TEC/TECParameterWorker.cpp
    include/sequence/TECCommandHandler.cpp
    include/sequence/LightingCommandHandler.cpp
    include/AppUI/TECPage/TECControlPage.cpp
    include/sequence/DataStoreCommandHandler.cpp
    include/sequence/DUTCommandHandler.cpp
    include/sequence/PSUCommandHandler.cpp
    include/sequence/SMUCommandHandler.cpp
    include/sequence/AuxWindowCommandHandler.cpp
    include/sequence/RelativePositionCommandHandler.cpp
    include/sequence/CoordinateAlignCommandHandler.cpp
    include/sequence/AccuracyTrainerCommandHandler.cpp
    include/sequence/EdgeTransferCommandHandler.cpp
    include/sequence/PlanarAdjustmentCommandHandler.cpp
    include/sequence/ZOffsetCommandHandler.cpp
    include/sequence/YawTrainerCommandHandler.cpp
    include/sequence/CameraOffsetCommandHandler.cpp
    include/AppUI/Common/FileSelectorDialog.cpp
    include/AppUI/Common/RecipeSaveAsDialog.cpp
    include/AppUI/Common/CreateProductDialog.cpp
    include/AppUI/Common/ShutdownConfirmDialog.cpp
    include/AppUI/Common/ProductSelectorDialog.cpp
    include/AuxWindow/AuxWindowManager.cpp
    include/AuxWindow/AuxViewWindow.cpp
    include/AuxWindow/LiveChartWidget.cpp
    include/AuxWindow/LiveViewCameraWidget.cpp
    include/AuxWindow/CameraClickToMoveWidget.cpp
    include/AuxWindow/WorldOverlay.cpp
    include/AuxWindow/WorldOverlayManager.cpp
    include/AuxWindow/WorldOverlayWidget.cpp
    include/AuxWindow/WorldOverlayRenderer.cpp
    include/AuxWindow/ShapeCreatorDialog.cpp
    include/AuxWindow/AlignmentWizardDialog.cpp
    include/GdiWindow/GdiCameraWindow.cpp
    include/GdiWindow/GdiCameraWindowManager.cpp
    include/GdiWindow/ChartOverlay.cpp
    include/GdiWindow/VisionResultOverlay.cpp
    include/GdiWindow/BeamStreamOverlay.cpp
    include/QtCameraWindow/QtCameraViewport.cpp
    include/QtCameraWindow/QtCameraWindow.cpp
    include/SwirBeamWindow/SwirBeamWindow.cpp
    include/SwirBeamWindow/SwirBeamWindowManager.cpp
    include/SwirBeamWindow/SwirPeakFinder.cpp
    include/SwirBeamWindow/SwirBeamDetector.cpp
    include/SwirBeamWindow/SwirBeamOverlay.cpp
    include/BeamOptimizer/BeamResultBroker.cpp
    include/BeamOptimizer/BeamOptimizerManager.cpp
    include/BeamOptimizer/BeamHillClimb.cpp
    include/PSU/PowerSupplyManager.cpp
    include/PSU/PowerSupplyJsonLoader.cpp
    include/PSU/KeysightPSUAdapter.cpp
    include/PSU/KeysightE36103B.cpp
    include/PSU/KeysightE36441A.cpp
    include/PSU/PSUParameterWorker.cpp
    include/AppUI/PowerSupplyControlPage/PowerSupplyControlPage.cpp
    include/AppUI/SMUControlPage/SMUControlPage.cpp
    include/SMU/SMUManager.cpp
    include/SMU/SMUJsonLoader.cpp
    include/SMU/SMUPollingManager.cpp
    include/SMU/SMUConnectionWorker.cpp
    include/SMU/SMUParameterWorker.cpp
    include/ProgramManager/ProgramWorker.cpp
    include/ProgramManager/ProgramManager.cpp
    include/ProgramManager/RecipeExecutionStateMachine.cpp
    include/ProgramManager/ConditionExpressionEvaluator.cpp
    include/AppUI/RunProgramPage/RunProgramPage.cpp
    include/AppUI/RunProgramPage/UserPromptWidget.cpp
    include/AppUI/RunProgramPage/UserInputWidget.cpp
    include/AppUI/RunProgramPage/ManualAdjustWidget.cpp
    include/AppUI/RunProgramPage/ManualAlignPointWidget.cpp
    include/AppUI/RunProgramPage/RetraceEditWidget.cpp
    include/AppUI/RunProgramPage/CaptureConfirmWidget.cpp
    include/AppUI/RunProgramPage/RecipeNodeItem.cpp
    include/AppUI/RunProgramPage/DependencyEdgeItem.cpp
    include/AppUI/RunProgramPage/DependencyGraphScene.cpp
    include/AppUI/RunProgramPage/DependencyGraphWindow.cpp
    include/AppUI/RunProgramPage/LoopConfigDialog.cpp
    include/motions/scanning/ScanExecutor.cpp
    # Lighting system
    src/lighting/CCSPD3Adapter.cpp
    src/lighting/LightingManager.cpp
    src/lighting/LightingJsonLoader.cpp
    src/lighting/LightingParameterWorker.cpp
    # Lighting UI
    include/AppUI/LightingPage/LightingControlWidget.cpp
    include/AppUI/LightingPage/CameraLightingPage.cpp
    # Safety system
    include/safety/SafetyWatchManager.cpp
    include/safety/SafetyEventLog.cpp
    include/safety/SafetyAlertDialog.cpp
    # Common utilities
    include/common/ScanActivityTracker.cpp
    # Application settings
    include/settings/ApplicationSettings.cpp
    # Settings UI page
    include/AppUI/SettingsPage/ApplicationSettingsPage.cpp
    include/AppUI/SettingsPage/SettingsContainerPage.cpp
    include/AppUI/SettingsPage/IoWatchSettingsWidget.cpp
    include/AppUI/SettingsPage/ConfigBackupManager.cpp
    # Trainer container (side panel navigation)
    include/AppUI/TrainerContainerPage.cpp
    # Accuracy Trainer
    include/AppUI/AccuracyTrainer/AccuracyTrainerStore.cpp
    include/AppUI/AccuracyTrainer/AccuracyTrainerPage.cpp
    include/AppUI/AccuracyTrainer/TeachBondingWidget.cpp
    include/AppUI/AccuracyTrainer/TeachReferenceWidget.cpp
    include/AppUI/AccuracyTrainer/RunAlignmentWidget.cpp
    include/AppUI/AccuracyTrainer/MultiDeviceRunAlignmentWidget.cpp
    include/AppUI/AccuracyTrainer/TeachMultiDeviceWidget.cpp
    include/AppUI/AccuracyTrainer/EditZOffsetWidget.cpp
    include/AppUI/AccuracyTrainer/AccuracyTrainerLogger.cpp
    # SQLite3 amalgamation (single-file C library)
    external/sqlite3/sqlite3.c
    # Camera Offset Trainer
    include/AppUI/CameraOffsetTrainer/CameraOffsetStore.cpp
    include/AppUI/CameraOffsetTrainer/CameraOffsetTrainerPage.cpp
    include/AppUI/CameraOffsetTrainer/CalibrateOffsetWidget.cpp
    include/AppUI/CameraOffsetTrainer/TestOffsetWidget.cpp
    # Planar Adjustment Trainer
    include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentStore.cpp
    include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentPage.cpp
    include/AppUI/PlanarAdjustmentTrainer/TeachPointWidget.cpp
    include/AppUI/PlanarAdjustmentTrainer/TiltResultWidget.cpp
    include/AppUI/PlanarAdjustmentTrainer/ApplyCorrectionWidget.cpp
    include/AppUI/PlanarAdjustmentTrainer/RetraceAndApplyWidget.cpp
    # Z Offset Trainer
    include/AppUI/ZOffsetTrainer/ZOffsetStore.cpp
    include/AppUI/ZOffsetTrainer/ZOffsetPage.cpp
    include/AppUI/ZOffsetTrainer/TeachZPointWidget.cpp
    include/AppUI/ZOffsetTrainer/ZOffsetResultWidget.cpp
    include/AppUI/ZOffsetTrainer/ZOffsetApplyWidget.cpp
    include/AppUI/ZOffsetTrainer/ZOffsetRetraceWidget.cpp
    # Yaw Trainer
    include/AppUI/YawTrainer/YawTrainerStore.cpp
    include/AppUI/YawTrainer/YawTrainerPage.cpp
    include/AppUI/YawTrainer/TeachLineWidget.cpp
    include/AppUI/YawTrainer/YawResultWidget.cpp
    include/AppUI/YawTrainer/YawApplyCorrectionWidget.cpp
    include/AppUI/YawTrainer/YawRetraceWidget.cpp
    # Placement Correction Trainer
    include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionStore.cpp
    include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionPage.cpp
    include/AppUI/PlacementCorrectionTrainer/TeachPlacementLineWidget.cpp
    include/AppUI/PlacementCorrectionTrainer/PlacementResultWidget.cpp
    include/AppUI/PlacementCorrectionTrainer/PlacementRetraceWidget.cpp
    include/AppUI/PlacementCorrectionTrainer/PlacementRetraceLogger.cpp
    include/AppUI/PlacementCorrectionTrainer/RetraceLogViewer.cpp
    include/AppUI/PlacementCorrectionTrainer/ExposureAdjustWidget.cpp
    include/AppUI/PlacementCorrectionTrainer/ImageGalleryWidget.cpp
    include/AppUI/PlacementCorrectionTrainer/SummaryDiagramWidget.cpp
    # Edge Transfer Trainer
    include/AppUI/EdgeTransferTrainer/EdgeTransferStore.cpp
    include/AppUI/EdgeTransferTrainer/EdgeTeachWidget.cpp
    include/AppUI/EdgeTransferTrainer/EdgeResultWidget.cpp
    include/AppUI/EdgeTransferTrainer/EdgeImageGalleryWidget.cpp
    include/AppUI/EdgeTransferTrainer/EdgeSummaryDiagramWidget.cpp
    include/AppUI/EdgeTransferTrainer/EdgeRetraceWidget.cpp
    include/AppUI/EdgeTransferTrainer/EdgeTransferPage.cpp
    # Startup/Connection system
    include/startup/ConnectionTask.cpp
    include/startup/ConnectionConfig.cpp
    include/startup/ConnectionWorkerPool.cpp
)

set(HEADERS
    bobcat_engine.h
    Application.h
    include/camera/CameraLiveView.h
    include/camera/CameraManager.h
    include/camera/CameraConfigManager.h
    include/camera/CameraHardwareFactory.h
    include/camera/ICameraHardware.h
    include/camera/CameraFrameData.h
    include/camera/CameraWorker.h
    include/camera/CameraView.h
    include/camera/CameraParameterWorker.h
    include/camera/MultiCameraView.h
    include/camera/LiveVideoSubscriber.h
    include/camera/FrameConverter.h
    include/camera/CameraImageWidget.h
    include/camera/CameraImageWidgetGL.h
    include/AppUI/MainMenuPage.h
    include/AppUI/MainWindow.h
    include/AppUI/ScriptRunnerPage/ScriptRunnerPage.h
    include/AppUI/ScriptRunnerPage/LuaSyntaxHighlighter.h
    include/AppUI/ScriptRunnerPage/ScriptEditorWidget.h
    include/scripting/ScriptWorker.h
    include/scripting/ScriptEngine.h
    include/AppUI/DarkTheme.h
    include/AppUI/Theme.h
    include/AppUI/ThemeManager.h
    include/AppUI/ThemeButtonStyles.h
    include/AppUI/ConnectionSplashScreen.h
    include/AppUI/HardwareInstrumentPage.h
    include/AppUI/PlaceholderPage.h
    include/context/ApplicationContext.h
    include/logger/LogManager.h
    include/logger/LogStatusBar.h
    include/logger/LogViewerPage.h
    include/data/DataStore.h
    include/data/TcpDataClient.h
    include/data/TcpClientManager.h
    include/data/TcpClientWidget.h
    include/data/DataStoreWidget.h
    include/dut/DeviceUnderTest.h
    include/AppUI/ConfigEditor/ConfigEditorPage.h 
    include/AppUI/Forms/TcpClientFormWidget.h 
    include/AppUI/Forms/CameraFormWidget.h
    include/AppUI/Forms/SavedPositionsFormWidget.h
    include/AppUI/Forms/MotorDeviceFormWidget.h 
    include/motions/MotionTypes.h
    include/logger/ILogger.h
    include/motions/interfaces/IMotionControllerManager.h
    include/AppUI/MotionPages/MotionControllerPageBase.h
    include/AppUI/MotionPages/PIControllerPage.h
    include/AppUI/MotionPages/ACSControllerPage.h
    include/AppUI/IOPage/IOControllerPage.h
    include/pneumatics/Pneumatic_Types.h
    include/pneumatics/IPneumaticSlide.h
    include/pneumatics/PneumaticSlide.h
    include/pneumatics/PneumaticManager.h
    include/pneumatics/PneumaticJsonLoader.h
    include/AppUI/PneumaticPage/PneumaticControlPage.h
    include/AppUI/PneumaticPage/PneumaticControlPage.cpp
    include/AppUI/Forms/PneumaticSlidesFormWidget.h
    include/AppUI/Forms/IODevicesFormWidget.h
    include/AppUI/Forms/PowerSupplyFormWidget.h
    include/AppUI/Forms/TECDevicesFormWidget.h
    include/sequence/SequenceEngine.h
    include/sequence/SequenceContext.h
    include/sequence/SequenceContextCommandHandler.h
    include/sequence/ICommandHandler.h
    include/sequence/MotionCommandHandler.h
    include/sequence/IOCommandHandler.h
    include/sequence/PneumaticCommandHandler.h
    include/sequence/CameraCommandHandler.h
    include/sequence/CalibrationCommandHandler.h
    include/sequence/CommandDispatcher.h
    include/motions/TransformationMatrixLoader.h
    include/motions/CoordinateTransformer.h
    include/motions/DeviceLinkManager.h
    include/AppUI/JogWindow/JogPanelWindow.h
    include/AppUI/JogWindow/JogWorker.h
    include/AppUI/JogWindow/JogKeyBindingHandler.h
    include/AppUI/JogWindow/QuickOutputWidget.h
    include/AppUI/MotionPages/PositionDot.h 
    include/AppUI/MotionPages/MotionGraphicsView.h
    include/AppUI/MotionPages/MotionControlPanel.h
    include/AppUI/MotionPages/DeviceButtonStack.h
    include/AppUI/MotionPages/NewPositionDialog.h
    include/AppUI/MotionPages/MotionPage.h
    include/AppUI/MotionPages/DevicePositionMarker.h
    include/motions/MotionJsonLoader.h 
    include/AppUI/MotionPages/PositionAnchor.h
    include/AppUI/MotionPages/PositionLabel.h 
    include/products/product_position_manager.h 
    include/products/product_json_loader.h
    include/AppUI/MotionPages/WaypointEditorDialog.h
    include/AppUI/MotionPages/PathPreview.h 
    include/AppUI/MotionPages/OffsetMoveDialog.h
    include/motions/scanning/AdaptiveScanController.h
    include/motions/scanning/ScanTypes.h
    include/motions/scanning/ScanStrategy.h
    include/motions/scanning/ScanStateMachine.h
    include/motions/scanning/ScanContext.h
    include/motions/scanning/ScanDataCollector.h
    include/motions/scanning/RotationScanTypes.h
    include/motions/scanning/RotationScanStrategy.h
    include/motions/scanning/RotationScanController.h
    include/AppUI/MotionPages/ScanControlPanel.h 
    include/sequence/SystemCommandHandler.h
    include/sequence/SequencePromptDialog.h
    include/sequence/GripperSequenceHelpers.h 
    include/AppUI/RecipePage/RecipePage.h
    include/recipe/RecipeManager.h
    include/recipe/ProcessSequenceFactory.h
    include/recipe/IProcessPlugin.h
    include/recipe/ProcessPluginRegistry.h 
    include/AppUI/RecipePage/ProcessConfigWidget.h
    include/AppUI/RecipePage/DeviceSelectorWidgets.h
    include/recipe/RecipeWorker.h
    include/AppUI/RecipePage/RecipeProgressWidget.h
    include/AppUI/RecipePage/ContextViewerWindow.h
    include/AppUI/RecipePage/ContextVariablePreviewWidget.h
    include/sequence/SequenceBuilder.h
    include/AppUI/MotionPages/EditPositionDialog.h
    include/AppUI/RecipePage/PneumaticSlideSelectorDialog.h
    include/AppUI/RecipePage/MotionDeviceSelectorDialog.h
    include/TEC/TECTypes.h
    include/TEC/ITECDevice.h
    include/TEC/Cld101xadapter.h
    include/TEC/TECManager.h
    include/TEC/TECJsonLoader.h
    include/TEC/TECConnectionWorker.h
    include/TEC/TECParameterWorker.h
    include/sequence/TECCommandHandler.h
    include/sequence/LightingCommandHandler.h
    include/AppUI/TECPage/TECControlPage.h
    include/sequence/DataStoreCommandHandler.h
    include/sequence/DUTCommandHandler.h
    include/sequence/PSUCommandHandler.h
    include/sequence/SMUCommandHandler.h
    include/sequence/AuxWindowCommandHandler.h
    include/sequence/AccuracyTrainerCommandHandler.h
    include/sequence/EdgeTransferCommandHandler.h
    include/sequence/PlanarAdjustmentCommandHandler.h
    include/sequence/ZOffsetCommandHandler.h
    include/sequence/YawTrainerCommandHandler.h
    include/sequence/CameraOffsetCommandHandler.h
    include/sequence/CustomServerCommandHandler.h
    include/customserver/BeamServerClient.h
    include/AppUI/Common/FileSelectorDialog.h
    include/AppUI/Common/RecipeSaveAsDialog.h
    include/AppUI/Common/CreateProductDialog.h
    include/AppUI/Common/ShutdownConfirmDialog.h
    include/AppUI/Common/ProductSelectorDialog.h
    include/AuxWindow/AuxWindowManager.h
    include/AuxWindow/AuxViewWindow.h
    include/AuxWindow/LiveChartWidget.h
    include/AuxWindow/LiveViewCameraWidget.h
    include/AuxWindow/CameraClickToMoveWidget.h
    include/AuxWindow/WorldOverlay.h
    include/AuxWindow/WorldOverlayManager.h
    include/AuxWindow/WorldOverlayWidget.h
    include/AuxWindow/WorldOverlayRenderer.h
    include/AuxWindow/ShapeCreatorDialog.h
    include/AuxWindow/AlignmentWizardDialog.h
    include/PSU/IPowerSupply.h
    include/PSU/PowerSupplyManager.h
    include/PSU/PowerSupplyJsonLoader.h
    include/PSU/KeysightPSUAdapter.h
    include/PSU/KeysightE36103B.h
    include/PSU/KeysightE36441A.h
    include/PSU/PSUParameterWorker.h
    include/AppUI/PowerSupplyControlPage/PowerSupplyControlPage.h
    include/AppUI/SMUControlPage/SMUControlPage.h
    include/SMU/ISMUDevice.h
    include/SMU/SMUTypes.h
    include/SMU/SMUManager.h
    include/SMU/SMUJsonLoader.h
    include/SMU/SMUPollingManager.h
    include/SMU/SMUConnectionWorker.h
    include/SMU/SMUParameterWorker.h
    include/ProgramManager/ProgramWorker.h
    include/ProgramManager/ProgramManager.h
    include/ProgramManager/ConditionExpressionEvaluator.h
    include/AppUI/RunProgramPage/RunProgramPage.h
    include/AppUI/RunProgramPage/UserPromptWidget.h
    include/AppUI/RunProgramPage/UserInputWidget.h
    include/AppUI/RunProgramPage/ManualAdjustWidget.h
    include/AppUI/RunProgramPage/ManualAlignPointWidget.h
    include/AppUI/RunProgramPage/RetraceEditWidget.h
    include/AppUI/RunProgramPage/CaptureConfirmWidget.h
    include/AppUI/RunProgramPage/ScanControlWidget.h
    include/AppUI/RunProgramPage/RollCorrectionWidget.h
    include/AppUI/RunProgramPage/RecipeNodeItem.h
    include/AppUI/RunProgramPage/DependencyEdgeItem.h
    include/AppUI/RunProgramPage/DependencyGraphScene.h
    include/AppUI/RunProgramPage/DependencyGraphWindow.h
    include/AppUI/RunProgramPage/LoopConfigDialog.h
    include/AppUI/RunProgramPage/ScanControlWidget.cpp
    include/AppUI/RunProgramPage/RollCorrectionWidget.cpp
    include/AppUI/RunProgramPage/MotionInspectorWidget.h
    include/AppUI/RunProgramPage/MotionInspectorWidget.cpp
    include/motions/scanning/ScanExecutor.h
    # Lighting system
    include/lighting/LightingTypes.h
    include/lighting/ILightingController.h
    include/lighting/CCSPD3Adapter.h
    include/lighting/LightingManager.h
    include/lighting/LightingJsonLoader.h
    include/lighting/LightingParameterWorker.h
    # Lighting UI
    include/AppUI/LightingPage/LightingControlWidget.h
    include/AppUI/LightingPage/CameraLightingPage.h
    # Safety system
    include/safety/SafetyWatchManager.h
    include/safety/SafetyEventLog.h
    include/common/ErrorCode.h
    include/common/ThreadPriority.h
    include/common/ScanActivityTracker.h
    include/common/ScanActivityGuard.h
    include/safety/SafetyAlertDialog.h
    # Application settings
    include/settings/ApplicationSettings.h
    # Settings UI page
    include/AppUI/SettingsPage/ApplicationSettingsPage.h
    include/AppUI/SettingsPage/ConfigBackupManager.h
    # Accuracy Trainer
    include/AppUI/AccuracyTrainer/AccuracyTrainerData.h
    include/AppUI/AccuracyTrainer/AccuracyTrainerStore.h
    include/AppUI/AccuracyTrainer/AccuracyTrainerPage.h
    include/AppUI/AccuracyTrainer/TeachBondingWidget.h
    include/AppUI/AccuracyTrainer/TeachReferenceWidget.h
    include/AppUI/AccuracyTrainer/RunAlignmentWidget.h
    include/AppUI/AccuracyTrainer/MultiDeviceRunAlignmentWidget.h
    include/AppUI/AccuracyTrainer/TeachMultiDeviceWidget.h
    include/AppUI/AccuracyTrainer/AccuracyTrainerLogger.h
    # Camera Offset Trainer
    include/AppUI/CameraOffsetTrainer/CameraOffsetData.h
    include/AppUI/CameraOffsetTrainer/CameraOffsetStore.h
    include/AppUI/CameraOffsetTrainer/CameraOffsetTrainerPage.h
    include/AppUI/CameraOffsetTrainer/CalibrateOffsetWidget.h
    include/AppUI/CameraOffsetTrainer/TestOffsetWidget.h
    # Planar Adjustment Trainer
    include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentData.h
    include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentStore.h
    include/AppUI/PlanarAdjustmentTrainer/PlanarAdjustmentPage.h
    include/AppUI/PlanarAdjustmentTrainer/TeachPointWidget.h
    include/AppUI/PlanarAdjustmentTrainer/TiltResultWidget.h
    include/AppUI/PlanarAdjustmentTrainer/ApplyCorrectionWidget.h
    include/AppUI/PlanarAdjustmentTrainer/RetraceAndApplyWidget.h
    # Z Offset Trainer
    include/AppUI/ZOffsetTrainer/ZOffsetData.h
    include/AppUI/ZOffsetTrainer/ZOffsetStore.h
    include/AppUI/ZOffsetTrainer/ZOffsetPage.h
    include/AppUI/ZOffsetTrainer/TeachZPointWidget.h
    include/AppUI/ZOffsetTrainer/ZOffsetResultWidget.h
    include/AppUI/ZOffsetTrainer/ZOffsetApplyWidget.h
    include/AppUI/ZOffsetTrainer/ZOffsetRetraceWidget.h
    # Yaw Trainer
    include/AppUI/YawTrainer/YawTrainerData.h
    include/AppUI/YawTrainer/YawTrainerStore.h
    include/AppUI/YawTrainer/YawTrainerPage.h
    include/AppUI/YawTrainer/TeachLineWidget.h
    include/AppUI/YawTrainer/YawResultWidget.h
    include/AppUI/YawTrainer/YawApplyCorrectionWidget.h
    include/AppUI/YawTrainer/YawRetraceWidget.h
    # Placement Correction Trainer
    include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionData.h
    include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionStore.h
    include/AppUI/PlacementCorrectionTrainer/PlacementCorrectionPage.h
    include/AppUI/PlacementCorrectionTrainer/TeachPlacementLineWidget.h
    include/AppUI/PlacementCorrectionTrainer/PlacementResultWidget.h
    include/AppUI/PlacementCorrectionTrainer/PlacementRetraceWidget.h
    include/AppUI/PlacementCorrectionTrainer/ExposureAdjustWidget.h
    include/AppUI/PlacementCorrectionTrainer/ImageGalleryWidget.h
    include/AppUI/PlacementCorrectionTrainer/SummaryDiagramWidget.h
    # Edge Transfer Trainer
    include/AppUI/EdgeTransferTrainer/EdgeTransferData.h
    include/AppUI/EdgeTransferTrainer/EdgeTransferStore.h
    include/AppUI/EdgeTransferTrainer/EdgeTeachWidget.h
    include/AppUI/EdgeTransferTrainer/EdgeResultWidget.h
    include/AppUI/EdgeTransferTrainer/EdgeImageGalleryWidget.h
    include/AppUI/EdgeTransferTrainer/EdgeSummaryDiagramWidget.h
    include/AppUI/EdgeTransferTrainer/EdgeRetraceWidget.h
    include/AppUI/EdgeTransferTrainer/EdgeTransferPage.h
    # Startup/Connection system
    include/startup/ConnectionResult.h
    include/startup/ConnectionTask.h
    include/startup/ConnectionConfig.h
    include/startup/ConnectionWorkerPool.h
)

# Conditionally add ACS controller files if library is found
if(ACSC_FOUND)
    list(APPEND SOURCES
        include/motions/ACS/acs_controller_manager.cpp
        include/motions/ACS/acs_controller.cpp
    )
    list(APPEND HEADERS
        include/motions/ACS/acs_controller_manager.h
        include/motions/ACS/acs_controller.h
        include/motions/ACS/ACSC.h
        include/motions/ACS/ACSC_wrapper.h
    )
    message(STATUS "ACS Controller support enabled")
else()
    message(STATUS "ACS Controller support disabled - library not found")
endif()

# Conditionally add PI controller files if library is found
if(PI_FOUND)
    list(APPEND SOURCES
        include/motions/PI/pi_controller_manager.cpp
        include/motions/PI/pi_controller.cpp
        include/motions/PI/PIPivotJsonLoader.cpp
        include/motions/PI/PIAlignExecutor.cpp
    )
    list(APPEND HEADERS
        include/motions/PI/pi_controller_manager.h
        include/motions/PI/pi_controller.h
        include/motions/PI/PI_GCS2_DLL.h
        include/motions/PI/PIPivotJsonLoader.h
        include/motions/PI/PIAlignExecutor.h
    )
    message(STATUS "PI Controller support enabled")
else()
    message(STATUS "PI Controller support disabled - library not found")
endif()

# Add unified motion manager only if BOTH ACS and PI are found
if(ACSC_FOUND AND PI_FOUND)
    list(APPEND SOURCES
        include/motions/unified_motion_manager.cpp
    )
    list(APPEND HEADERS
        include/motions/unified_motion_manager.h
        include/motions/IPositionSubscriber.h
    )
    message(STATUS "Unified Motion Manager enabled (requires both ACS and PI)")
elseif(ACSC_FOUND OR PI_FOUND)
    message(STATUS "Unified Motion Manager disabled (requires both ACS and PI libraries)")
endif()

# Conditionally add Pylon camera files if SDK is found
if(PYLON_FOUND)
    list(APPEND SOURCES
        include/camera/PylonCameraAdapter.cpp
        include/camera/pylon_camera.cpp
    )
    list(APPEND HEADERS
        include/camera/PylonCameraAdapter.h
        include/camera/pylon_camera.h
    )
    message(STATUS "Pylon Camera support enabled")
else()
    message(STATUS "Pylon Camera support disabled - SDK not found")
endif()

# Conditionally add IDS camera files if SDK is found
if(IDS_UEYE_FOUND)
    list(APPEND SOURCES
        include/camera/IDSCameraAdapter.cpp
        include/camera/ids_camera_test.cpp
    )
    list(APPEND HEADERS
        include/camera/IDSCameraAdapter.h
        include/camera/ids_camera_test.h
    )
    message(STATUS "IDS Camera support enabled")
else()
    message(STATUS "IDS Camera support disabled - SDK not found")
endif()

# Conditionally add EziIO files if library is found
if(EZIIO_FOUND)
    list(APPEND SOURCES
        include/IO/EziIODevice.cpp
        include/IO/EziIOManager.cpp
        include/IO/WagoIODevice.cpp
        include/IO/QuickOutputManager.cpp
        include/IO/QuickOutput_JsonLoader.cpp
        include/IO/IoWatchManager.cpp
    )
    list(APPEND HEADERS
        include/IO/EziIO_Types.h
        include/IO/IEziIODevice.h
        include/IO/EziIODevice.h
        include/IO/SocketGuard.h
        include/IO/WagoIODevice.h
        include/IO/EziIOManager.h
        include/IO/EziIO_JsonLoader.h
        include/IO/QuickOutputManager.h
        include/IO/QuickOutput_JsonLoader.h
        include/IO/IoWatchManager.h
    )
    message(STATUS "EziIO support enabled (includes WAGO Modbus TCP adapter)")
else()
    message(STATUS "EziIO support disabled - library not found")
endif()


# Conditionally add CLD101x Direct VISA adapter if VISA is found
if(VISA_FOUND)
    list(APPEND SOURCES
        include/TEC/CLD101xDirectAdapter.cpp
        include/TEC/TED4015DirectAdapter.cpp
    )
    list(APPEND HEADERS
        include/TEC/CLD101xDirectAdapter.h
        include/TEC/TED4015DirectAdapter.h
    )
    message(STATUS "CLD101x Direct VISA adapter enabled")
    message(STATUS "TED4015 Direct VISA adapter enabled")
else()
    message(STATUS "CLD101x Direct VISA adapter disabled - NI-VISA SDK not found")
    message(STATUS "TED4015 Direct VISA adapter disabled - NI-VISA SDK not found")
endif()


# Conditionally add SMU system (Keithley adapters) if VISA is found
if(VISA_FOUND)
    list(APPEND SOURCES
        include/SMU/Keithley6482Adapter.cpp
        include/SMU/KeithleySMUAdapter.cpp
    )
    list(APPEND HEADERS
        include/SMU/Keithley6482Adapter.h
        include/SMU/KeithleySMUAdapter.h
    )
    message(STATUS "SMU system enabled (Keithley 6482 + Generic SMU adapters)")
else()
    message(STATUS "SMU system disabled - NI-VISA SDK not found")
endif()

# Conditionally add OpenCV test files if OpenCV is found
if(OPENCV_FOUND)
    list(APPEND SOURCES
        include/vision/OpenCVTest.cpp
        include/vision/VisionManager.cpp
        include/vision/plugins/CircleDetectionPlugin.cpp
        include/vision/plugins/EdgeDetectionPlugin.cpp
        include/vision/plugins/FiducialDetectorPlugin.cpp
        include/vision/plugins/LineDetectorPlugin.cpp
        include/vision/plugins/CornerDetectorPlugin.cpp
        include/vision/plugins/PatternMatchingPlugin.cpp
        include/vision/plugins/PatternMatchingEdgePlugin.cpp
        include/vision/plugins/TemplateMatchingBase.cpp
        include/vision/plugins/ImageProcessingPlugin.cpp
        include/vision/plugins/CalibrationPlatePlugin.cpp
        include/vision/VisionProcessingWorker.cpp
        include/sequence/VisionCommandHandler.cpp
        include/sequence/ContextImageViewer.cpp
        include/recipe/processes/VisionProcesses.cpp
        # UI files (Phase 2)
        include/vision/ui/VisionManagerPage.cpp
        include/vision/ui/VisionToolbarWidget.cpp
        include/vision/ui/VisionProfileListWidget.cpp
        include/vision/ui/VisionLeftPanelWidget.cpp
        include/vision/ui/VisionImageWidget.cpp
        include/vision/ui/VisionRightPanelWidget.cpp
        include/vision/ui/VisionReliabilityTestDialog.cpp
        # Wizard step widgets
        include/vision/ui/VisionNavWidget.cpp
        include/vision/ui/CameraLightingStepWidget.cpp
        include/vision/ui/CaptureROIStepWidget.cpp
        include/vision/ui/PluginConfigStepWidget.cpp
        include/vision/ui/ExecuteTuneStepWidget.cpp
    )
    list(APPEND HEADERS
        include/vision/OpenCVTest.h
        include/vision/IVisionPlugin.h
        include/vision/VisionResult.h
        include/vision/VisionManager.h
        include/vision/PluginParameter.h
        include/vision/VisionROI.h
        include/vision/plugins/CircleDetectionPlugin.h
        include/vision/plugins/EdgeDetectionPlugin.h
        include/vision/plugins/FiducialDetectorPlugin.h
        include/vision/plugins/LineDetectorPlugin.h
        include/vision/plugins/TemplateMatchingBase.h
        include/vision/plugins/ImageProcessingPlugin.h
        include/vision/plugins/CalibrationPlatePlugin.h
        include/vision/VisionProcessingWorker.h
        include/sequence/VisionCommandHandler.h
        include/sequence/ContextImageViewer.h
        # UI headers (Phase 2)
        include/vision/ui/VisionManagerPage.h
        include/vision/ui/VisionToolbarWidget.h
        include/vision/ui/VisionProfileListWidget.h
        include/vision/ui/VisionLeftPanelWidget.h
        include/vision/ui/VisionImageWidget.h
        include/vision/ui/VisionRightPanelWidget.h
        include/vision/ui/VisionReliabilityTestDialog.h
        # Wizard step widget headers
        include/vision/ui/VisionWizardState.h
        include/vision/ui/VisionNavWidget.h
        include/vision/ui/CameraLightingStepWidget.h
        include/vision/ui/CaptureROIStepWidget.h
        include/vision/ui/PluginConfigStepWidget.h
        include/vision/ui/ExecuteTuneStepWidget.h
    )
    message(STATUS "OpenCV vision support enabled (v4.12.0)")
else()
    message(STATUS "OpenCV vision support disabled - OpenCV not found")
endif()

# Conditionally add Halcon vision plugin files if BOTH Halcon and OpenCV are found
# (Halcon plugins still use OpenCV for image conversion and UI)
if(HALCON_FOUND AND OPENCV_FOUND)
    list(APPEND SOURCES
        include/vision/plugins/HalconCircleDetectionPlugin.cpp
        include/vision/plugins/HalconCircleDetectionRobustPlugin.cpp
        include/vision/plugins/HalconLineDetectionPlugin.cpp
        include/vision/plugins/HalconSingleLineAnglePlugin.cpp
        include/vision/plugins/HalconPatternMatchingEdgePlugin.cpp
        include/vision/plugins/HalconPatternMatchingNCCPlugin.cpp
        include/vision/plugins/HalconPatternMatchingDescriptorPlugin.cpp
        include/vision/plugins/HalconPatternMatchingGrayPlugin.cpp
        include/vision/plugins/HalconBeamDetectionPlugin.cpp
        include/vision/plugins/HalconD4SigmaBeamDetectionPlugin.cpp
    )
    list(APPEND HEADERS
        include/vision/plugins/HalconCircleDetectionPlugin.h
        include/vision/plugins/HalconCircleDetectionRobustPlugin.h
        include/vision/plugins/HalconLineDetectionPlugin.h
        include/vision/plugins/HalconSingleLineAnglePlugin.h
        include/vision/plugins/HalconPatternMatchingEdgePlugin.h
        include/vision/plugins/HalconPatternMatchingNCCPlugin.h
        include/vision/plugins/HalconPatternMatchingDescriptorPlugin.h
        include/vision/plugins/HalconPatternMatchingGrayPlugin.h
        include/vision/plugins/HalconBeamDetectionPlugin.h
        include/vision/plugins/HalconD4SigmaBeamDetectionPlugin.h
    )
    message(STATUS "Halcon vision plugins enabled")
else()
    if(NOT HALCON_FOUND)
        message(STATUS "Halcon vision plugins disabled - Halcon SDK not found")
    endif()
endif()

# ============================================================================
# Create Executable
# ============================================================================
# Add Windows application icon
if(WIN32)
    set(APP_ICON_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
endif()

add_executable(bobcat_engine ${SOURCES} ${HEADERS} ${APP_ICON_RESOURCE} icons.qrc)

# Ensure the .rc is recompiled when the .ico file changes
if(WIN32)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc"
                 PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app_icon.ico")
endif()

# ============================================================================
# Set Debugger Environment for Visual Studio
# ============================================================================
# Configure simulation mode environment variable for Visual Studio debugger
if(BOBCAT_SIMULATION)
    set_target_properties(bobcat_engine PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "BOBCAT_SIMULATION=1"
    )
    message(STATUS "Visual Studio debugger environment: BOBCAT_SIMULATION=1")
else()
    set_target_properties(bobcat_engine PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "BOBCAT_SIMULATION=0"
    )
    message(STATUS "Visual Studio debugger environment: BOBCAT_SIMULATION=0")
endif()

# ============================================================================
# Compile Definitions
# ============================================================================
# Prevent Windows min/max macros from conflicting with std::min/std::max in OpenCV/Halcon
# WIN32_LEAN_AND_MEAN prevents windows.h from including winsock.h (v1),
# allowing winsock2.h to be safely included in any order
if(WIN32)
    target_compile_definitions(bobcat_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN USE_QT_CAMERA_WINDOW)
endif()

# ============================================================================
# Compiler Warning Level (safety compliance)
# ============================================================================
if(MSVC)
    # Remove default /W3 to avoid D9025 override warning
    string(REGEX REPLACE "/W[0-3]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(bobcat_engine PRIVATE /W4 /external:anglebrackets /external:W0)
endif()

# External SDK includes (SYSTEM to suppress third-party warnings)
if(EXTERNAL_SDK_INCLUDES)
    target_include_directories(bobcat_engine SYSTEM PRIVATE ${EXTERNAL_SDK_INCLUDES})
endif()
if(EZIIO_FOUND)
    target_include_directories(bobcat_engine SYSTEM PRIVATE ${EZIIO_INCLUDE_DIR})
endif()
if(OPENCV_FOUND)
    target_include_directories(bobcat_engine SYSTEM PRIVATE ${OPENCV_INCLUDE_DIR})
endif()
if(HALCON_FOUND)
    target_include_directories(bobcat_engine SYSTEM PRIVATE ${HALCON_INCLUDE_DIR})
endif()
# SQLite3 amalgamation (SYSTEM to suppress third-party warnings)
target_include_directories(bobcat_engine SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/external/sqlite3)
set_source_files_properties(external/sqlite3/sqlite3.c PROPERTIES COMPILE_FLAGS "/W0")

# ============================================================================
# Link Libraries
# ============================================================================

# Link Qt
target_link_libraries(bobcat_engine PRIVATE Qt6::Widgets Qt6::Charts Qt6::OpenGLWidgets)

# Link Lua 5.4 scripting engine
if(LUA_FOUND)
    target_link_libraries(bobcat_engine PRIVATE lua54)
    target_include_directories(bobcat_engine SYSTEM PRIVATE ${LUA_INCLUDE_DIR})
    target_compile_definitions(bobcat_engine PRIVATE HAS_LUA)
    message(STATUS "Linked Lua 5.4 scripting engine")
endif()

# Link ACS library if found
if(ACSC_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${ACSC_LIBRARY})
endif()

# Link PI library if found
if(PI_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${PI_LIBRARY})
    
    # On Windows, copy PI DLL to the output directory
    if(WIN32 AND EXISTS ${PI_DLL})
        add_custom_command(TARGET bobcat_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PI_DLL}
                $<TARGET_FILE_DIR:bobcat_engine>
            COMMENT "Copying PI GCS2 DLL to output directory"
        )
    endif()
endif()

# Link Pylon SDK if found
if(PYLON_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${PYLON_LIBRARIES})
    
    # On Windows, copy Pylon DLLs to the output directory
    if(WIN32)
        add_custom_command(TARGET bobcat_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${PYLON_ROOT}/Runtime/x64"
                $<TARGET_FILE_DIR:bobcat_engine>
            COMMENT "Copying Pylon DLLs to output directory"
        )
    endif()
endif()

# Link IDS uEye SDK if found
if(IDS_UEYE_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${IDS_UEYE_LIBRARY})
    
    # On Windows, copy the DLL to the output directory
    if(WIN32 AND EXISTS ${IDS_UEYE_DLL})
        add_custom_command(TARGET bobcat_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${IDS_UEYE_DLL}
                $<TARGET_FILE_DIR:bobcat_engine>
            COMMENT "Copying IDS uEye DLL to output directory"
        )
    endif()
endif()

# Link EziIO SDK if found
if(EZIIO_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${EZIIO_LIBRARY})

    # On Windows, copy the DLL to the output directory
    if(WIN32 AND EXISTS ${EZIIO_DLL})
        add_custom_command(TARGET bobcat_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${EZIIO_DLL}
                $<TARGET_FILE_DIR:bobcat_engine>
            COMMENT "Copying EziIO DLL to output directory"
        )
    endif()
endif()

# Link NI-VISA SDK if found
if(VISA_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${VISA_LIBRARY})
    message(STATUS "Linked NI-VISA library for CLD101x Direct adapter")
endif()

# Link OpenCV if found
if(OPENCV_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${OPENCV_LIBRARY})
    target_compile_definitions(bobcat_engine PRIVATE HAS_OPENCV)
    message(STATUS "Linked OpenCV library")

    # On Windows, copy the DLLs to the output directory
    if(WIN32)
        # Copy main OpenCV DLL (debug or release based on config)
        add_custom_command(TARGET bobcat_engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${OPENCV_DLL}
                $<TARGET_FILE_DIR:bobcat_engine>
            COMMENT "Copying OpenCV DLL to output directory"
        )

        # Copy FFmpeg DLL (needed for video I/O)
        if(EXISTS ${OPENCV_FFMPEG_DLL})
            add_custom_command(TARGET bobcat_engine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${OPENCV_FFMPEG_DLL}
                    $<TARGET_FILE_DIR:bobcat_engine>
                COMMENT "Copying OpenCV FFmpeg DLL to output directory"
            )
        endif()
    endif()
endif()

# Link Halcon SDK if found (with delay loading for optional runtime availability)
if(HALCON_FOUND)
    target_link_libraries(bobcat_engine PRIVATE ${HALCON_LIBRARY} ${HALCON_CORE_LIBRARY})
    target_compile_definitions(bobcat_engine PRIVATE HAS_HALCON)

    # Use delay-loaded DLLs so the program can run even without Halcon installed
    # The DLLs are only loaded when Halcon functions are actually called
    if(MSVC)
        target_link_options(bobcat_engine PRIVATE
            "/DELAYLOAD:halconcpp.dll"
            "/DELAYLOAD:halcon.dll"
        )
        target_link_libraries(bobcat_engine PRIVATE delayimp.lib)
        message(STATUS "Linked Halcon library with delay loading (optional at runtime)")
    else()
        message(STATUS "Linked Halcon library")
    endif()
endif()

# ============================================================================
# Copy Configuration Files
# ============================================================================

# Copy config files to output directory (EXCLUDING *.local.json to preserve user settings)
add_custom_command(TARGET bobcat_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:bobcat_engine>/config
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:bobcat_engine>/config/SYSTEM
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:bobcat_engine>/config/system
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/SYSTEM/needlexyoffset.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/SYSTEM/needlexyoffset.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/system/applicationSettings.ini
        $<TARGET_FILE_DIR:bobcat_engine>/config/system/applicationSettings.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/camera_config.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/camera_config.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/motor_devices.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/motor_devices.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/io_devices.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/io_devices.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/pneumatic_slides.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/pneumatic_slides.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/tec_devices.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/tec_devices.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/power_supply_config.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/power_supply_config.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/smu_devices.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/smu_devices.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/tcp_clients.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/tcp_clients.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/transformation_matrix.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/transformation_matrix.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/pi_pivot_config.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/pi_pivot_config.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/lighting_config.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/lighting_config.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/safety_watch.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/safety_watch.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/beam_stream.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/beam_stream.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/beam_detection.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/beam_detection.json
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/config/jog_stepsize.json
        $<TARGET_FILE_DIR:bobcat_engine>/config/jog_stepsize.json
    COMMENT "Copying config templates (preserving *.local.json files)"
)

# Copy Lua scripts to output directory
add_custom_command(TARGET bobcat_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/scripts
        $<TARGET_FILE_DIR:bobcat_engine>/scripts
    COMMENT "Copying Lua scripts to output directory"
)

# Copy assets folder (icons, images, etc.) to output directory
add_custom_command(TARGET bobcat_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:bobcat_engine>/assets
    COMMENT "Copying assets folder to output directory"
)

# Copy CHANGELOG.md to output directory (for in-app changelog viewer)
add_custom_command(TARGET bobcat_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/CHANGELOG.md
        $<TARGET_FILE_DIR:bobcat_engine>/CHANGELOG.md
    COMMENT "Copying CHANGELOG.md to output directory"
)

# ============================================================================
# Config Editor Utility
# ============================================================================
# Standalone JSON configuration file editor
add_executable(config_editor
    tools/config_editor/main.cpp
    tools/config_editor/ConfigEditorWindow.cpp
    tools/config_editor/JsonFileListWidget.cpp
    tools/config_editor/JsonEditorWidget.cpp
    include/AppUI/DarkTheme.cpp
)

target_include_directories(config_editor PRIVATE
    ${CMAKE_SOURCE_DIR}/include/AppUI
    ${NLOHMANN_JSON_DIR}
)

target_link_libraries(config_editor PRIVATE Qt6::Widgets)

set_target_properties(config_editor PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    AUTOMOC ON
)

if(WIN32 AND MSVC)
    set_target_properties(config_editor PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

message(STATUS "Config Editor utility: 'config_editor' - standalone JSON config file editor")

# ============================================================================
# Data Explorer Utility
# ============================================================================
# Browse and filter serial number result data
add_executable(data_explorer
    tools/data_explorer/main.cpp
    tools/data_explorer/DataExplorerWindow.cpp
    tools/data_explorer/SerialDataLoader.cpp
    include/AppUI/DarkTheme.cpp
)

target_include_directories(data_explorer PRIVATE
    ${CMAKE_SOURCE_DIR}/include/AppUI
    ${CMAKE_SOURCE_DIR}/tools/data_explorer
    ${NLOHMANN_JSON_DIR}
)

target_link_libraries(data_explorer PRIVATE Qt6::Widgets)

set_target_properties(data_explorer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    AUTOMOC ON
)

if(WIN32 AND MSVC)
    set_target_properties(data_explorer PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

message(STATUS "Data Explorer utility: 'data_explorer' - browse serial number result data")

# ============================================================================
# Vision Profile Tester Utility
# ============================================================================
# Offline vision profile testing tool - load images from disk, run vision plugins
if(OPENCV_FOUND)
    set(VISION_TESTER_SOURCES
        tools/vision_profile_tester/main.cpp
        tools/vision_profile_tester/VisionProfileTesterWindow.cpp
        include/AppUI/DarkTheme.cpp
        include/vision/VisionManager.cpp
        # OpenCV plugins (excludes PatternMatching/TemplateMatchingBase - they need VisionManagerPage)
        include/vision/plugins/CircleDetectionPlugin.cpp
        include/vision/plugins/EdgeDetectionPlugin.cpp
        include/vision/plugins/FiducialDetectorPlugin.cpp
        include/vision/plugins/LineDetectorPlugin.cpp
        include/vision/plugins/CornerDetectorPlugin.cpp
        include/vision/plugins/ImageProcessingPlugin.cpp
        include/vision/plugins/CalibrationPlatePlugin.cpp
    )

    if(HALCON_FOUND)
        list(APPEND VISION_TESTER_SOURCES
            include/vision/plugins/HalconCircleDetectionPlugin.cpp
            include/vision/plugins/HalconCircleDetectionRobustPlugin.cpp
            include/vision/plugins/HalconLineDetectionPlugin.cpp
            include/vision/plugins/HalconSingleLineAnglePlugin.cpp
            include/vision/plugins/HalconBeamDetectionPlugin.cpp
            include/vision/plugins/HalconD4SigmaBeamDetectionPlugin.cpp
        )
    endif()

    add_executable(vision_profile_tester EXCLUDE_FROM_ALL ${VISION_TESTER_SOURCES})

    target_include_directories(vision_profile_tester PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/AppUI
        ${CMAKE_SOURCE_DIR}/include/camera
        ${CMAKE_SOURCE_DIR}/include/vision
        ${CMAKE_SOURCE_DIR}/include/vision/plugins
        ${CMAKE_SOURCE_DIR}/tools/vision_profile_tester
        ${NLOHMANN_JSON_DIR}
        ${OPENCV_INCLUDE_DIR}
    )

    target_link_libraries(vision_profile_tester PRIVATE Qt6::Widgets ${OPENCV_LIBRARY})
    target_compile_definitions(vision_profile_tester PRIVATE HAS_OPENCV NOMINMAX WIN32_LEAN_AND_MEAN)

    if(HALCON_FOUND)
        target_include_directories(vision_profile_tester SYSTEM PRIVATE ${HALCON_INCLUDE_DIR})
        target_link_libraries(vision_profile_tester PRIVATE ${HALCON_LIBRARY} ${HALCON_CORE_LIBRARY})
        target_compile_definitions(vision_profile_tester PRIVATE HAS_HALCON)
    endif()

    set_target_properties(vision_profile_tester PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        AUTOMOC ON
    )

    if(WIN32 AND MSVC)
        set_target_properties(vision_profile_tester PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
    endif()

    message(STATUS "Vision Profile Tester: 'vision_profile_tester' - offline vision profile testing tool")
endif()