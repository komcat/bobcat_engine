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

# Add source
add_executable(bobcat_engine "bobcat_engine.cpp" "bobcat_engine.h")

# Link Qt
target_link_libraries(bobcat_engine PRIVATE Qt6::Widgets)