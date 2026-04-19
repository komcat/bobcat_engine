# ============================================================================
# Lua 5.4 Build Configuration
# ============================================================================
# Builds Lua 5.4 as a static library for embedding in bobcat_engine.
#
# Source priority:
#   1. Local: external/lua54/src/lua.h  (manually placed or previously fetched)
#   2. Auto-download via FetchContent from lua.org
#
# If download fails, manually extract lua-5.4.7.tar.gz into external/lua54/
# so that external/lua54/src/lua.h exists.
# ============================================================================

include(FetchContent)

set(LUA_LOCAL_DIR "${CMAKE_SOURCE_DIR}/external/lua54")

if(EXISTS "${LUA_LOCAL_DIR}/src/lua.h")
    message(STATUS "Lua 5.4: Using local source from external/lua54/")
    set(LUA_SRC_DIR "${LUA_LOCAL_DIR}/src")
else()
    message(STATUS "Lua 5.4: Downloading source...")
    FetchContent_Declare(
        lua54_src
        URL "https://www.lua.org/ftp/lua-5.4.7.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(lua54_src)
    set(LUA_SRC_DIR "${lua54_src_SOURCE_DIR}/src")
    message(STATUS "Lua 5.4: Downloaded to ${LUA_SRC_DIR}")
endif()

# Core Lua library sources (excludes lua.c and luac.c which are standalone programs)
set(LUA_CORE_SOURCES
    ${LUA_SRC_DIR}/lapi.c
    ${LUA_SRC_DIR}/lauxlib.c
    ${LUA_SRC_DIR}/lbaselib.c
    ${LUA_SRC_DIR}/lcode.c
    ${LUA_SRC_DIR}/lcorolib.c
    ${LUA_SRC_DIR}/lctype.c
    ${LUA_SRC_DIR}/ldblib.c
    ${LUA_SRC_DIR}/ldebug.c
    ${LUA_SRC_DIR}/ldo.c
    ${LUA_SRC_DIR}/ldump.c
    ${LUA_SRC_DIR}/lfunc.c
    ${LUA_SRC_DIR}/lgc.c
    ${LUA_SRC_DIR}/linit.c
    ${LUA_SRC_DIR}/liolib.c
    ${LUA_SRC_DIR}/llex.c
    ${LUA_SRC_DIR}/lmathlib.c
    ${LUA_SRC_DIR}/lmem.c
    ${LUA_SRC_DIR}/loadlib.c
    ${LUA_SRC_DIR}/lobject.c
    ${LUA_SRC_DIR}/lopcodes.c
    ${LUA_SRC_DIR}/loslib.c
    ${LUA_SRC_DIR}/lparser.c
    ${LUA_SRC_DIR}/lstate.c
    ${LUA_SRC_DIR}/lstring.c
    ${LUA_SRC_DIR}/lstrlib.c
    ${LUA_SRC_DIR}/ltable.c
    ${LUA_SRC_DIR}/ltablib.c
    ${LUA_SRC_DIR}/ltm.c
    ${LUA_SRC_DIR}/lundump.c
    ${LUA_SRC_DIR}/lutf8lib.c
    ${LUA_SRC_DIR}/lvm.c
    ${LUA_SRC_DIR}/lzio.c
)

add_library(lua54 STATIC ${LUA_CORE_SOURCES})
target_include_directories(lua54 PUBLIC ${LUA_SRC_DIR})

if(MSVC)
    target_compile_options(lua54 PRIVATE /W0)
endif()

set(LUA_FOUND TRUE)
set(LUA_INCLUDE_DIR ${LUA_SRC_DIR})
message(STATUS "Lua 5.4: Static library target 'lua54' configured")
