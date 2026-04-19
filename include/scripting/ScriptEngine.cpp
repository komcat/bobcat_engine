// ScriptEngine.cpp - Lua 5.4 scripting engine implementation
#include "ScriptEngine.h"
#include "SequenceEngine.h"
#include "SequenceContext.h"
#include "ApplicationContext.h"
#include "LogManager.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>

// ============================================================================
// Construction / Destruction
// ============================================================================

ScriptEngine::ScriptEngine(ApplicationContext* appContext)
    : m_appContext(appContext)
{
    if (appContext) {
        m_seqEngine = appContext->sequenceEngine;
        m_logger = appContext->logManager;
    }
}

ScriptEngine::~ScriptEngine()
{
    if (m_L) {
        lua_close(m_L);
        m_L = nullptr;
    }
}

bool ScriptEngine::Initialize()
{
    if (m_L) {
        lua_close(m_L);
    }

    m_L = luaL_newstate();
    if (!m_L) return false;

    luaL_openlibs(m_L);
    Sandbox();
    RegisterBindings();
    RegisterConvenienceModules();

    lua_sethook(m_L, StopCheckHook, LUA_MASKCOUNT, 1000);

    return true;
}

// ============================================================================
// Execution
// ============================================================================

bool ScriptEngine::ExecuteFile(const std::string& scriptPath, std::string& errorMessage)
{
    if (!m_L) {
        errorMessage = "Lua state not initialized";
        return false;
    }

    m_context = std::make_unique<SequenceContext>();
    m_context->SetSequenceName("script:" + scriptPath);
    m_context->SetExecutionStartTime();

    if (m_seqEngine) {
        m_seqEngine->BuildDeviceRegistry();
    }

    m_running.store(true);
    m_stopRequested.store(false);

    int result = luaL_dofile(m_L, scriptPath.c_str());

    m_running.store(false);
    m_context->SetExecutionEndTime();

    if (result != LUA_OK) {
        errorMessage = lua_tostring(m_L, -1);
        lua_pop(m_L, 1);

        if (m_logger) {
            m_logger->Error("[ScriptEngine] " + errorMessage, "ScriptEngine");
        }
        return false;
    }

    if (m_logger) {
        auto duration = m_context->GetExecutionDuration();
        m_logger->Info("[ScriptEngine] Script completed in " +
            std::to_string(duration.count()) + "ms", "ScriptEngine");
    }

    return true;
}

bool ScriptEngine::ExecuteString(const std::string& code, std::string& errorMessage)
{
    if (!m_L) {
        errorMessage = "Lua state not initialized";
        return false;
    }

    m_context = std::make_unique<SequenceContext>();
    m_context->SetSequenceName("script:inline");
    m_context->SetExecutionStartTime();

    if (m_seqEngine) {
        m_seqEngine->BuildDeviceRegistry();
    }

    m_running.store(true);
    m_stopRequested.store(false);

    int result = luaL_dostring(m_L, code.c_str());

    m_running.store(false);
    m_context->SetExecutionEndTime();

    if (result != LUA_OK) {
        errorMessage = lua_tostring(m_L, -1);
        lua_pop(m_L, 1);

        if (m_logger) {
            m_logger->Error("[ScriptEngine] " + errorMessage, "ScriptEngine");
        }
        return false;
    }

    return true;
}

void ScriptEngine::RequestStop()
{
    m_stopRequested.store(true);
}

void ScriptEngine::ClearStopFlag()
{
    m_stopRequested.store(false);
}

// ============================================================================
// Helpers
// ============================================================================

ScriptEngine* ScriptEngine::GetEngine(lua_State* L)
{
    return static_cast<ScriptEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
}

std::map<std::string, std::string> ScriptEngine::TableToParams(lua_State* L, int index)
{
    std::map<std::string, std::string> params;
    if (!lua_istable(L, index)) return params;

    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        std::string key;
        if (lua_isstring(L, -2)) {
            key = lua_tostring(L, -2);
        } else if (lua_isnumber(L, -2)) {
            key = std::to_string(static_cast<int>(lua_tonumber(L, -2)));
        }

        std::string value;
        if (lua_isboolean(L, -1)) {
            value = lua_toboolean(L, -1) ? "true" : "false";
        } else if (lua_isnumber(L, -1)) {
            std::ostringstream oss;
            oss << lua_tonumber(L, -1);
            value = oss.str();
        } else if (lua_isstring(L, -1)) {
            value = lua_tostring(L, -1);
        }

        if (!key.empty()) {
            params[key] = value;
        }

        lua_pop(L, 1);
    }

    return params;
}

std::string ScriptEngine::FormatLuaString(lua_State* L, int startArg)
{
    int nargs = lua_gettop(L);
    if (nargs < startArg) return "";

    const char* fmt = luaL_checkstring(L, startArg);
    if (nargs == startArg) return fmt;

    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_remove(L, -2);

    for (int i = startArg; i <= nargs; ++i) {
        lua_pushvalue(L, i);
    }

    if (lua_pcall(L, nargs - startArg + 1, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return fmt;
    }

    std::string result = lua_tostring(L, -1);
    lua_pop(L, 1);
    return result;
}

// ============================================================================
// Stop check hook — called every 1000 Lua instructions
// ============================================================================

void ScriptEngine::StopCheckHook(lua_State* L, lua_Debug* /*ar*/)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "ScriptEngine");
    auto* engine = static_cast<ScriptEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (engine && engine->IsStopRequested()) {
        luaL_error(L, "Script stopped by user");
    }
}

// ============================================================================
// Core dispatch — bridges Lua calls to CommandDispatcher
// ============================================================================

int ScriptEngine::LuaDispatch(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_seqEngine) {
        return luaL_error(L, "ScriptEngine not initialized");
    }

    const char* deviceName = luaL_checkstring(L, 1);
    const char* methodName = luaL_checkstring(L, 2);

    std::map<std::string, std::string> params;
    if (lua_gettop(L) >= 3 && lua_istable(L, 3)) {
        params = TableToParams(L, 3);
    }

    std::string errorMessage;
    bool ok = engine->m_seqEngine->DispatchCommand(
        deviceName, methodName, params, errorMessage, engine->m_context.get());

    if (!ok) {
        return luaL_error(L, "%s", errorMessage.c_str());
    }

    lua_pushboolean(L, 1);
    return 1;
}

// ============================================================================
// Print override — routes to logger + output callback
// ============================================================================

int ScriptEngine::LuaPrint(lua_State* L)
{
    auto* engine = GetEngine(L);

    int nargs = lua_gettop(L);
    std::string output;

    for (int i = 1; i <= nargs; ++i) {
        if (i > 1) output += "\t";

        if (lua_isstring(L, i)) {
            output += lua_tostring(L, i);
        } else if (lua_isnumber(L, i)) {
            std::ostringstream oss;
            oss << lua_tonumber(L, i);
            output += oss.str();
        } else if (lua_isboolean(L, i)) {
            output += lua_toboolean(L, i) ? "true" : "false";
        } else if (lua_isnil(L, i)) {
            output += "nil";
        } else {
            output += luaL_typename(L, i);
        }
    }

    if (engine && engine->m_outputCallback) {
        engine->m_outputCallback(output);
    }

    if (engine && engine->m_logger) {
        engine->m_logger->Info("[Script] " + output, "Script");
    } else {
        std::cout << "[Script] " << output << std::endl;
    }

    return 0;
}

// ============================================================================
// Context bindings — read/write SequenceContext from Lua
// ============================================================================

int ScriptEngine::LuaCtxSet(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);

    if (lua_isnumber(L, 2)) {
        engine->m_context->SetValue(key, static_cast<float>(lua_tonumber(L, 2)));
    } else if (lua_isboolean(L, 2)) {
        engine->m_context->SetValue(key, static_cast<bool>(lua_toboolean(L, 2)));
    } else if (lua_isstring(L, 2)) {
        engine->m_context->SetValue(key, std::string(lua_tostring(L, 2)));
    } else {
        return luaL_error(L, "ctx.set: unsupported value type (expected number, boolean, or string)");
    }

    return 0;
}

int ScriptEngine::LuaCtxSetString(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);
    const char* value = luaL_checkstring(L, 2);
    engine->m_context->SetValue(key, std::string(value));
    return 0;
}

int ScriptEngine::LuaCtxGet(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);

    if (!engine->m_context->HasValue(key)) {
        lua_pushnil(L);
        return 1;
    }

    auto val = engine->m_context->GetValue(key);
    switch (val.type) {
    case SequenceContext::ValueType::NUMERIC:
        lua_pushnumber(L, val.numericValue);
        break;
    case SequenceContext::ValueType::BOOLEAN:
        lua_pushboolean(L, val.booleanValue);
        break;
    case SequenceContext::ValueType::STRING:
        lua_pushstring(L, val.stringValue.c_str());
        break;
    default:
        lua_pushnil(L);
        break;
    }

    return 1;
}

int ScriptEngine::LuaCtxGetNumeric(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);
    float defaultVal = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    lua_pushnumber(L, engine->m_context->GetNumeric(key, defaultVal));
    return 1;
}

int ScriptEngine::LuaCtxGetString(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);
    const char* defaultVal = luaL_optstring(L, 2, "");
    lua_pushstring(L, engine->m_context->GetString(key, defaultVal).c_str());
    return 1;
}

int ScriptEngine::LuaCtxGetBoolean(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);
    bool defaultVal = lua_toboolean(L, 2) != 0;
    lua_pushboolean(L, engine->m_context->GetBoolean(key, defaultVal));
    return 1;
}

int ScriptEngine::LuaCtxHas(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    const char* key = luaL_checkstring(L, 1);
    lua_pushboolean(L, engine->m_context->HasValue(key));
    return 1;
}

int ScriptEngine::LuaCtxClear(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        engine->m_context->ClearValue(lua_tostring(L, 1));
    } else {
        engine->m_context->ClearAll();
    }
    return 0;
}

int ScriptEngine::LuaCtxGetAllKeys(lua_State* L)
{
    auto* engine = GetEngine(L);
    if (!engine || !engine->m_context) return luaL_error(L, "No execution context");

    auto keys = engine->m_context->GetAllKeys();
    lua_createtable(L, static_cast<int>(keys.size()), 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        lua_pushstring(L, keys[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

// ============================================================================
// Log bindings
// ============================================================================

int ScriptEngine::LuaLogInfo(lua_State* L)
{
    auto* engine = GetEngine(L);
    std::string msg = FormatLuaString(L, 1);

    if (engine && engine->m_logger) {
        engine->m_logger->Info("[Script] " + msg, "Script");
    } else {
        std::cout << "[Script INFO] " << msg << std::endl;
    }

    if (engine && engine->m_outputCallback) {
        engine->m_outputCallback("[INFO] " + msg);
    }
    return 0;
}

int ScriptEngine::LuaLogWarn(lua_State* L)
{
    auto* engine = GetEngine(L);
    std::string msg = FormatLuaString(L, 1);

    if (engine && engine->m_logger) {
        engine->m_logger->Warning("[Script] " + msg, "Script");
    } else {
        std::cout << "[Script WARN] " << msg << std::endl;
    }

    if (engine && engine->m_outputCallback) {
        engine->m_outputCallback("[WARN] " + msg);
    }
    return 0;
}

int ScriptEngine::LuaLogError(lua_State* L)
{
    auto* engine = GetEngine(L);
    std::string msg = FormatLuaString(L, 1);

    if (engine && engine->m_logger) {
        engine->m_logger->Error("[Script] " + msg, "Script");
    } else {
        std::cerr << "[Script ERROR] " << msg << std::endl;
    }

    if (engine && engine->m_outputCallback) {
        engine->m_outputCallback("[ERROR] " + msg);
    }
    return 0;
}

int ScriptEngine::LuaLogDebug(lua_State* L)
{
    auto* engine = GetEngine(L);
    std::string msg = FormatLuaString(L, 1);

    if (engine && engine->m_logger) {
        engine->m_logger->Debug("[Script] " + msg, "Script");
    } else {
        std::cout << "[Script DEBUG] " << msg << std::endl;
    }
    return 0;
}

// ============================================================================
// System bindings
// ============================================================================

int ScriptEngine::LuaSystemWait(lua_State* L)
{
    auto* engine = GetEngine(L);
    int ms = static_cast<int>(luaL_checknumber(L, 1));
    if (ms <= 0) return 0;

    const int CHUNK_MS = 50;
    for (int elapsed = 0; elapsed < ms; elapsed += CHUNK_MS) {
        if (engine && engine->IsStopRequested()) {
            return luaL_error(L, "Script stopped by user");
        }
        int sleepMs = (ms - elapsed < CHUNK_MS) ? (ms - elapsed) : CHUNK_MS;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }

    return 0;
}

// ============================================================================
// Binding registration
// ============================================================================

void ScriptEngine::RegisterBindings()
{
    lua_pushlightuserdata(m_L, this);
    lua_setfield(m_L, LUA_REGISTRYINDEX, "ScriptEngine");

    auto registerClosure = [this](const char* name, lua_CFunction func) {
        lua_pushlightuserdata(m_L, this);
        lua_pushcclosure(m_L, func, 1);
        lua_setglobal(m_L, name);
    };

    registerClosure("_dispatch", LuaDispatch);
    registerClosure("print", LuaPrint);

    // ctx table
    lua_newtable(m_L);
    auto registerField = [this](const char* name, lua_CFunction func) {
        lua_pushlightuserdata(m_L, this);
        lua_pushcclosure(m_L, func, 1);
        lua_setfield(m_L, -2, name);
    };
    registerField("set", LuaCtxSet);
    registerField("setString", LuaCtxSetString);
    registerField("get", LuaCtxGet);
    registerField("getNumeric", LuaCtxGetNumeric);
    registerField("getString", LuaCtxGetString);
    registerField("getBoolean", LuaCtxGetBoolean);
    registerField("has", LuaCtxHas);
    registerField("clear", LuaCtxClear);
    registerField("keys", LuaCtxGetAllKeys);
    lua_setglobal(m_L, "ctx");

    // log table
    lua_newtable(m_L);
    registerField("info", LuaLogInfo);
    registerField("warn", LuaLogWarn);
    registerField("error", LuaLogError);
    registerField("debug", LuaLogDebug);
    lua_setglobal(m_L, "log");

    // system table
    lua_newtable(m_L);
    registerField("wait", LuaSystemWait);
    lua_setglobal(m_L, "system");
}

// ============================================================================
// Convenience modules — Lua wrappers around _dispatch()
// ============================================================================

void ScriptEngine::RegisterConvenienceModules()
{
    const char* modules = R"lua(
-- Motion control
motion = {
    moveToNamed = function(device, posName)
        return _dispatch(device, "MoveToNamedPosition", {positionName = posName})
    end,
    moveAbs = function(device, axes)
        -- axes table: {x="10", y="20", z="5"} etc.
        return _dispatch(device, "MoveToAbs", axes)
    end,
    moveRel = function(device, axis, distance)
        return _dispatch(device, "MoveRelative", {axis = axis, distance = tostring(distance)})
    end,
    home = function(device, axis)
        return _dispatch(device, "HomeAxis", {axis = axis or "all"})
    end,
    getPosition = function(device)
        _dispatch(device, "SavePositionToContext", {key_prefix = "_sp", save_uvw = "true"})
        return {
            x = ctx.getNumeric("_sp_x"),
            y = ctx.getNumeric("_sp_y"),
            z = ctx.getNumeric("_sp_z"),
            u = ctx.getNumeric("_sp_u"),
            v = ctx.getNumeric("_sp_v"),
            w = ctx.getNumeric("_sp_w")
        }
    end,
    setVelocity = function(device, velocity)
        local devLower = device:lower()
        if devLower:find("hex") or devLower:find("pi") then
            return _dispatch(device, "SetSystemVelocity", {velocity = tostring(velocity)})
        else
            return _dispatch(device, "SetAllAxisVelocity", {velocity = tostring(velocity)})
        end
    end,
    waitDone = function(device, timeoutMs)
        local p = {}
        if timeoutMs then p.timeout_ms = tostring(timeoutMs) end
        return _dispatch(device, "WaitForMotionComplete", p)
    end
}

-- Digital I/O
io = {
    setOutput = function(device, pinName, state)
        return _dispatch(device, "SetOutput", {pinName = pinName, state = tostring(state)})
    end,
    setOutputPin = function(device, pin, state)
        return _dispatch(device, "SetOutputPin", {pin = tostring(pin), state = tostring(state)})
    end,
    getInput = function(device, pinName)
        -- GetInput does not support captureAs; returns success/fail only
        return _dispatch(device, "GetInput", {pinName = pinName})
    end,
    waitInput = function(device, pinName, state, timeoutMs)
        return _dispatch(device, "WaitForInput", {
            pinName = pinName,
            state = tostring(state),
            timeout = tostring(timeoutMs or 5000)
        })
    end,
    pulse = function(device, pinName, durationMs)
        return _dispatch(device, "Pulse", {pinName = pinName, milliseconds = tostring(durationMs)})
    end
}

-- Pneumatic slides
pneumatic = {
    extend = function(device)
        return _dispatch(device, "Extend", {})
    end,
    retract = function(device)
        return _dispatch(device, "Retract", {})
    end,
    retractAll = function()
        return _dispatch("pneumatic", "RetractAll", {})
    end,
    waitExtended = function(device, timeoutMs)
        local p = {}
        if timeoutMs then p.timeout = tostring(timeoutMs) end
        return _dispatch(device, "WaitForExtended", p)
    end,
    waitRetracted = function(device, timeoutMs)
        local p = {}
        if timeoutMs then p.timeout = tostring(timeoutMs) end
        return _dispatch(device, "WaitForRetracted", p)
    end
}

-- Camera
camera = {
    capture = function(device)
        return _dispatch(device, "Capture", {})
    end,
    captureToFile = function(device, filename)
        local p = {}
        if filename then p.filename = filename end
        return _dispatch(device, "CaptureToFile", p)
    end,
    captureToContext = function(device, contextKey)
        return _dispatch(device, "CaptureToContext", {context_key = contextKey or "camera_frame"})
    end,
    setExposure = function(device, exposureUs)
        return _dispatch(device, "SetExposure", {value = tostring(exposureUs)})
    end,
    setGain = function(device, gain)
        return _dispatch(device, "SetGain", {value = tostring(gain)})
    end
}

-- Vision (requires HAS_OPENCV)
vision = {
    runProfile = function(profilePath, cameraName)
        local p = {profile_path = profilePath, camera = cameraName or ""}
        _dispatch("vision", "execute_profile", p)
        -- Results are stored in context as vision.result.* keys
        local result = {}
        local keys = ctx.keys()
        for _, key in ipairs(keys) do
            local prefix = "vision.result."
            if key:sub(1, #prefix) == prefix then
                result[key:sub(#prefix + 1)] = ctx.get(key)
            end
        end
        return result
    end
}

-- TEC
tec = {
    setTemp = function(device, temperature)
        return _dispatch(device, "SetTemperature", {temperature = tostring(temperature)})
    end,
    enable = function(device)
        return _dispatch(device, "TECOn", {})
    end,
    disable = function(device)
        return _dispatch(device, "TECOff", {})
    end,
    readTemp = function(device)
        _dispatch(device, "ReadTemperature", {captureAs = "_st"})
        return ctx.getNumeric("_st")
    end,
    waitTemp = function(device, tolerance, timeoutMs)
        local p = {}
        if tolerance then p.tolerance = tostring(tolerance) end
        if timeoutMs then p.timeout = tostring(timeoutMs) end
        return _dispatch(device, "WaitForTemperature", p)
    end,
    setLaserCurrent = function(device, current)
        return _dispatch(device, "SetLaserCurrent", {current = tostring(current)})
    end,
    laserOn = function(device)
        return _dispatch(device, "LaserOn", {})
    end,
    laserOff = function(device)
        return _dispatch(device, "LaserOff", {})
    end
}

-- Power supply
psu = {
    setVoltage = function(device, voltage)
        return _dispatch(device, "SetVoltage", {voltage = tostring(voltage)})
    end,
    setCurrent = function(device, current)
        return _dispatch(device, "SetCurrent", {current = tostring(current)})
    end,
    enable = function(device)
        return _dispatch(device, "OutputOn", {})
    end,
    disable = function(device)
        return _dispatch(device, "OutputOff", {})
    end,
    readVoltage = function(device)
        _dispatch(device, "ReadVoltage", {captureAs = "_pv"})
        return ctx.getNumeric("_pv")
    end,
    readCurrent = function(device)
        _dispatch(device, "ReadCurrent", {captureAs = "_pc"})
        return ctx.getNumeric("_pc")
    end
}

-- Source measure unit
smu = {
    readCurrent = function(device, channel)
        local p = {captureAs = "_sm"}
        if channel then p.channel = tostring(channel) end
        _dispatch(device, "ReadCurrent", p)
        return ctx.getNumeric("_sm")
    end,
    readVoltage = function(device, channel)
        local p = {captureAs = "_sv"}
        if channel then p.channel = tostring(channel) end
        _dispatch(device, "ReadVoltage", p)
        return ctx.getNumeric("_sv")
    end,
    setVoltage = function(device, voltage, channel)
        local p = {voltage = tostring(voltage)}
        if channel then p.channel = tostring(channel) end
        return _dispatch(device, "SetVoltage", p)
    end,
    setCurrent = function(device, current, channel)
        local p = {current = tostring(current)}
        if channel then p.channel = tostring(channel) end
        return _dispatch(device, "SetCurrent", p)
    end,
    enable = function(device, channel)
        local p = {}
        if channel then p.channel = tostring(channel) end
        return _dispatch(device, "OutputOn", p)
    end,
    disable = function(device, channel)
        local p = {}
        if channel then p.channel = tostring(channel) end
        return _dispatch(device, "OutputOff", p)
    end
}

-- Lighting
lighting = {
    setIntensity = function(device, channel, intensity)
        return _dispatch(device, "SetIntensity", {
            channel = tostring(channel),
            intensity = tostring(intensity)
        })
    end,
    turnOn = function(device, channel)
        return _dispatch(device, "TurnOn", {channel = tostring(channel)})
    end,
    turnOff = function(device, channel)
        return _dispatch(device, "TurnOff", {channel = tostring(channel)})
    end,
    allOff = function(device)
        return _dispatch(device, "AllOff", {})
    end
}

-- DataStore (uses context commands, not datastore handler directly)
datastore = {
    set = function(key, value)
        ctx.set(key, value)
    end,
    get = function(key)
        return ctx.get(key)
    end,
    readAndLog = function(device, channelName)
        return _dispatch(device, "ReadAndLog", {channelName = channelName})
    end
}
)lua";

    if (luaL_dostring(m_L, modules) != LUA_OK) {
        std::string err = lua_tostring(m_L, -1);
        lua_pop(m_L, 1);
        if (m_logger) {
            m_logger->Error("[ScriptEngine] Failed to register convenience modules: " + err, "ScriptEngine");
        }
    }
}

// ============================================================================
// Sandbox — remove dangerous modules
// ============================================================================

void ScriptEngine::Sandbox()
{
    const char* removals[] = {"os", "io", "loadfile", "dofile", "require"};
    for (const char* name : removals) {
        lua_pushnil(m_L);
        lua_setglobal(m_L, name);
    }
}
