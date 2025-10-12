cmake_minimum_required(VERSION 3.8)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("bobcat_engine")

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
find_package(Qt6 REQUIRED COMPONENTS Widgets)

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
# Include Directories
# ============================================================================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/camera
    ${CMAKE_SOURCE_DIR}/include/AppUI      
    ${CMAKE_SOURCE_DIR}/include/context
)

# Add nlohmann json include directory if found
if(NLOHMANN_JSON_FOUND)
    include_directories(${NLOHMANN_JSON_DIR})
endif()

# Add Pylon include directory if found
if(PYLON_FOUND)
    include_directories(${PYLON_INCLUDE_DIR})
endif()

# Add IDS uEye include directory if found
if(IDS_UEYE_FOUND)
    include_directories(${IDS_UEYE_INCLUDE_DIR})
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
    include/camera/MultiCameraView.h
    include/camera/LiveVideoSubscriber.h
    include/camera/FrameConverter.h
    include/camera/CameraImageWidget.h
    include/AppUI/MainMenuPage.h    
    include/AppUI/MainWindow.h
    include/AppUI/HardwareInstrumentPage.h
    include/AppUI/PlaceholderPage.h
    include/context/ApplicationContext.h
)

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

# ============================================================================
# Create Executable
# ============================================================================
add_executable(bobcat_engine ${SOURCES} ${HEADERS})

# ============================================================================
# Link Libraries
# ============================================================================

# Link Qt
target_link_libraries(bobcat_engine PRIVATE Qt6::Widgets)

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