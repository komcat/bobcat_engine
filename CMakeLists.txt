cmake_minimum_required(VERSION 3.8)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("bobcat_engine")

# Add this after your project() or set(CMAKE_CXX_STANDARD ...) line

if(MSVC)
    # Suppress warning C4828 (disallowed character in source file)
    add_compile_options(/wd4828)
    
    # If you also want to suppress C4566 (which you had before)
    add_compile_options(/wd4566)
endif()

# Point to MSVC version
set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3/msvc2022_64")
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add MSVC-specific flags
if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
endif()

# Find Qt
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts)

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
# Include Directories
# ============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/camera
    ${CMAKE_SOURCE_DIR}/include/AppUI
    ${CMAKE_SOURCE_DIR}/include/AppUI/ConfigEditor
    ${CMAKE_SOURCE_DIR}/include/AppUI/Forms
    ${CMAKE_SOURCE_DIR}/include/AppUI/MotionPages
    ${CMAKE_SOURCE_DIR}/include/AppUI/IOPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/PneumaticPage
    ${CMAKE_SOURCE_DIR}/include/AppUI/JogWindow
    ${CMAKE_SOURCE_DIR}/include/AppUI/RecipePage
    ${CMAKE_SOURCE_DIR}/include/context
    ${CMAKE_SOURCE_DIR}/include/logger
    ${CMAKE_SOURCE_DIR}/include/data
    ${CMAKE_SOURCE_DIR}/include/motions
    ${CMAKE_SOURCE_DIR}/include/motions/PI
    ${CMAKE_SOURCE_DIR}/include/motions/ACS
    ${CMAKE_SOURCE_DIR}/include/motions/interfaces
    ${CMAKE_SOURCE_DIR}/include/motions/scanning
    ${CMAKE_SOURCE_DIR}/include/products
    ${CMAKE_SOURCE_DIR}/include/IO
    ${CMAKE_SOURCE_DIR}/include/pneumatics
    ${CMAKE_SOURCE_DIR}/include/recipe
    ${CMAKE_SOURCE_DIR}/include/sequence
)

# Add nlohmann json include directory if found
if(NLOHMANN_JSON_FOUND)
    include_directories(${NLOHMANN_JSON_DIR})
endif()

# Add ACS include directory if found
if(ACSC_FOUND)
    include_directories(${ACSC_DIR})
endif()

# Add PI include directory if found
if(PI_FOUND)
    include_directories(${PI_INCLUDE_DIR})
endif()

# Add Pylon include directory if found
if(PYLON_FOUND)
    include_directories(${PYLON_INCLUDE_DIR})
endif()

# Add IDS uEye include directory if found
if(IDS_UEYE_FOUND)
    include_directories(${IDS_UEYE_INCLUDE_DIR})
endif()

# Add EziIO include directory if found
if(EZIIO_FOUND)
    include_directories(${EZIIO_INCLUDE_DIR})
endif()

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
    include/camera/MultiCameraView.cpp
    include/camera/LiveVideoSubscriber.cpp
    include/camera/CameraImageWidget.cpp
    include/AppUI/MainWindow.cpp
    include/AppUI/MainMenuPage.cpp
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
    include/sequence/SequenceEngine.cpp
    include/sequence/MotionCommandHandler.cpp
    include/sequence/IOCommandHandler.cpp
    include/sequence/PneumaticCommandHandler.cpp
    include/sequence/CameraCommandHandler.cpp
    include/sequence/CommandDispatcher.cpp
    include/motions/TransformationMatrixLoader.cpp
    include/motions/CoordinateTransformer.cpp
    include/AppUI/JogWindow/JogPanelWindow.cpp
    include/AppUI/JogWindow/JogKeyBindingHandler.cpp
    include/AppUI/MotionPages/PositionDot.cpp
    include/AppUI/MotionPages/MotionGraphicsView.cpp
    include/AppUI/MotionPages/MotionControlPanel.cpp
    include/AppUI/MotionPages/NewPositionDialog.cpp
    include/AppUI/MotionPages/MotionPage.cpp
    include/AppUI/MotionPages/DevicePositionMarker.cpp
    include/motions/MotionJsonLoader.cpp
    include/AppUI/MotionPages/PositionAnchor.cpp
    include/AppUI/MotionPages/PositionLabel.cpp
    include/products/product_position_manager.cpp
    include/products/product_json_loader.cpp
    include/AppUI/MotionPages/WaypointEditorDialog.cpp
    include/AppUI/MotionPages/PathPreview.cpp
    include/AppUI/MotionPages/OffsetMoveDialog.cpp
    include/motions/scanning/AdaptiveScanController.cpp
    include/motions/scanning/ScanContext.cpp
    include/motions/scanning/ScanDataCollector.cpp
    include/motions/scanning/CoarseToFineStrategy.cpp
    include/logger/ILogger.cpp
    include/AppUI/MotionPages/ScanControlPanel.cpp
    include/sequence/SystemCommandHandler.cpp
    include/sequence/SequencePromptDialog.cpp
    include/sequence/GripperSequenceHelpers.cpp
    include/AppUI/RecipePage/RecipePage.cpp
 "include/recipe/RecipeManager.cpp"
 "include/recipe/ProcessSequenceFactory.cpp"
 "include/AppUI/RecipePage/ProcessConfigWidget.cpp"
 "include/AppUI/RecipePage/DeviceSelectorWidgets.cpp" "include/recipe/RecipeWorker.cpp" "include/AppUI/RecipePage/RecipeProgressWidget.cpp")

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
    include/camera/MultiCameraView.h
    include/camera/LiveVideoSubscriber.h
    include/camera/FrameConverter.h
    include/camera/CameraImageWidget.h
    include/AppUI/MainMenuPage.h    
    include/AppUI/MainWindow.h
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
    include/sequence/SequenceEngine.h
    include/sequence/ICommandHandler.h
    include/sequence/MotionCommandHandler.h
    include/sequence/IOCommandHandler.h
    include/sequence/PneumaticCommandHandler.h
    include/sequence/CameraCommandHandler.h
    include/sequence/CommandDispatcher.h
    include/motions/TransformationMatrixLoader.h
    include/motions/CoordinateTransformer.h
    include/AppUI/JogWindow/JogPanelWindow.h
    include/AppUI/JogWindow/JogKeyBindingHandler.h
    include/AppUI/MotionPages/PositionDot.h 
    include/AppUI/MotionPages/MotionGraphicsView.h
    include/AppUI/MotionPages/MotionControlPanel.h
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
    include/AppUI/MotionPages/ScanControlPanel.h 
    include/sequence/SystemCommandHandler.h
    include/sequence/SequencePromptDialog.h
    include/sequence/GripperSequenceHelpers.h 
    include/AppUI/RecipePage/RecipePage.h
 "include/recipe/RecipeManager.h"
 "include/recipe/ProcessSequenceFactory.h" 
 "include/AppUI/RecipePage/ProcessConfigWidget.h"
 "include/AppUI/RecipePage/DeviceSelectorWidgets.h" "include/recipe/RecipeWorker.h" "include/AppUI/RecipePage/RecipeProgressWidget.h" "include/sequence/SequenceBuilder.h")

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
    )
    list(APPEND HEADERS
        include/motions/PI/pi_controller_manager.h
        include/motions/PI/pi_controller.h
        include/motions/PI/PI_GCS2_DLL.h
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
    )
    list(APPEND HEADERS
        include/IO/EziIO_Types.h
        include/IO/IEziIODevice.h
        include/IO/EziIODevice.h
        include/IO/EziIOManager.h
        include/IO/EziIO_JsonLoader.h
    )
    message(STATUS "EziIO support enabled")
else()
    message(STATUS "EziIO support disabled - library not found")
endif()

# ============================================================================
# Create Executable
# ============================================================================
add_executable(bobcat_engine ${SOURCES} ${HEADERS})

# ============================================================================
# Link Libraries
# ============================================================================

# Link Qt
target_link_libraries(bobcat_engine PRIVATE Qt6::Widgets Qt6::Charts)

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

# ============================================================================
# Copy Configuration Files
# ============================================================================

# Copy camera_config.json to output directory
add_custom_command(TARGET bobcat_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:bobcat_engine>/config
    COMMENT "Copying config files to output directory"
)

# Copy assets folder (icons, images, etc.) to output directory
add_custom_command(TARGET bobcat_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:bobcat_engine>/assets
    COMMENT "Copying assets folder to output directory"
)