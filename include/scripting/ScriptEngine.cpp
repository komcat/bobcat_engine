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

    lua_sethook(m_L, DebugHook, LUA_MASKLINE | LUA_MASKCOUNT, 1000);

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
    m_debugCondition.notify_one();
}

void ScriptEngine::ClearStopFlag()
{
    m_stopRequested.store(false);
}

// ============================================================================
// Debugger
// ============================================================================

void ScriptEngine::RequestDebugContinue()
{
    m_debugContinueRequested.store(true);
    m_debugCondition.notify_one();
}

void ScriptEngine::RequestDebugRunToEnd()
{
    m_debugMode.store(false);
    m_debugContinueRequested.store(true);
    m_debugCondition.notify_one();
}

void ScriptEngine::SetBreakpoint(int line)
{
    std::lock_guard<std::mutex> lock(m_breakpointMutex);
    m_breakpoints.insert(line);
}

void ScriptEngine::ClearBreakpoint(int line)
{
    std::lock_guard<std::mutex> lock(m_breakpointMutex);
    m_breakpoints.erase(line);
}

void ScriptEngine::ToggleBreakpoint(int line)
{
    std::lock_guard<std::mutex> lock(m_breakpointMutex);
    if (m_breakpoints.count(line))
        m_breakpoints.erase(line);
    else
        m_breakpoints.insert(line);
}

void ScriptEngine::ClearAllBreakpoints()
{
    std::lock_guard<std::mutex> lock(m_breakpointMutex);
    m_breakpoints.clear();
}

std::set<int> ScriptEngine::GetBreakpoints() const
{
    std::lock_guard<std::mutex> lock(m_breakpointMutex);
    return m_breakpoints;
}

std::vector<ScriptEngine::LocalVar> ScriptEngine::CollectLocals(lua_State* L, lua_Debug* ar)
{
    std::vector<LocalVar> locals;
    const char* name = nullptr;
    for (int i = 1; (name = lua_getlocal(L, ar, i)) != nullptr; ++i) {
        std::string value;
        int t = lua_type(L, -1);
        switch (t) {
        case LUA_TNUMBER:  { std::ostringstream o; o << lua_tonumber(L, -1); value = o.str(); } break;
        case LUA_TSTRING:  value = std::string("\"") + lua_tostring(L, -1) + "\""; break;
        case LUA_TBOOLEAN: value = lua_toboolean(L, -1) ? "true" : "false"; break;
        case LUA_TNIL:     value = "nil"; break;
        case LUA_TTABLE:   value = "table"; break;
        case LUA_TFUNCTION: value = "function"; break;
        default:           value = lua_typename(L, t); break;
        }
        // Skip internal variables (names starting with '(')
        if (name[0] != '(') {
            locals.emplace_back(std::string(name), value);
        }
        lua_pop(L, 1);
    }
    return locals;
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
// Debug hook — fires on every line + every 1000 instructions
// ============================================================================

void ScriptEngine::DebugHook(lua_State* L, lua_Debug* ar)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "ScriptEngine");
    auto* engine = static_cast<ScriptEngine*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (!engine) return;

    if (engine->IsStopRequested()) {
        luaL_error(L, "Script stopped by user");
        return;
    }

    if (ar->event != LUA_HOOKLINE) return;

    // Get source info to filter out internal wrapper code
    lua_getinfo(L, "Sl", ar);
    std::string source = ar->source ? ar->source : "?";

    // Only pause on user script lines (source starts with '@' for files, or '=' for strings)
    // Skip internal convenience modules which have source "[string \"...\"]"
    if (source.empty() || source[0] == '[') return;

    int line = ar->currentline;
    if (line <= 0) return;

    bool shouldPause = engine->m_debugMode.load();

    if (!shouldPause) {
        std::lock_guard<std::mutex> lock(engine->m_breakpointMutex);
        shouldPause = engine->m_breakpoints.count(line) > 0;
    }

    if (!shouldPause) return;

    auto locals = engine->CollectLocals(L, ar);

    if (engine->m_debugPauseCallback) {
        engine->m_debugPauseCallback(line, source, locals);
    }

    engine->m_debugPaused.store(true);
    engine->m_debugContinueRequested.store(false);

    std::unique_lock<std::mutex> lock(engine->m_debugMutex);
    engine->m_debugCondition.wait(lock, [engine]() {
        return engine->m_debugContinueRequested.load() || engine->m_stopRequested.load();
    });

    engine->m_debugPaused.store(false);

    if (engine->IsStopRequested()) {
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
    const char* part1 = R"lua(
-- ================================================================
-- Motion control
-- ================================================================
motion = {
    moveToNamed = function(device, posName)
        return _dispatch(device, "MoveToNamedPosition", {positionName = posName})
    end,
    moveAbs = function(device, axes)
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
            x = ctx.getNumeric("_sp_x"), y = ctx.getNumeric("_sp_y"), z = ctx.getNumeric("_sp_z"),
            u = ctx.getNumeric("_sp_u"), v = ctx.getNumeric("_sp_v"), w = ctx.getNumeric("_sp_w")
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
    end,
    moveToGraph = function(device, graphName, nodeName)
        return _dispatch(device, "MoveToGraphNode", {graphName = graphName, endNodeName = nodeName})
    end,
    parallelMove = function(movesJsonStr)
        return _dispatch("parallel", "ParallelMove", {moves = movesJsonStr})
    end,
    parallelMoveRel = function(movesJsonStr)
        return _dispatch("parallel", "ParallelMoveRelative", {moves = movesJsonStr})
    end,
    emergencyStop = function(device)
        return _dispatch(device, "EmergencyStop", {})
    end,
    stopAxis = function(device, axis)
        return _dispatch(device, "StopAxis", {axis = axis})
    end,
    enableServo = function(device, axis, enable)
        return _dispatch(device, "EnableServo", {axis = axis, enable = tostring(enable)})
    end,
    savePosition = function(device, keyPrefix, saveUVW)
        local p = {key_prefix = keyPrefix}
        if saveUVW then p.save_uvw = "true" end
        return _dispatch(device, "SavePositionToContext", p)
    end,
    moveToContext = function(device, keyPrefix, keepZ)
        local p = {key_prefix = keyPrefix}
        if keepZ then p.keep_z = "true" end
        return _dispatch(device, "MoveToAbsFromContext", p)
    end,
    moveToContextXY = function(device, xKey, yKey)
        return _dispatch(device, "MoveToContextXY", {x_key = xKey, y_key = yKey})
    end,
    moveToContextPos = function(device, contextPrefix)
        return _dispatch(device, "MoveToContextPosition", {context_prefix = contextPrefix})
    end,
    updatePosition = function(device, posName, saveToFile)
        local p = {position_name = posName}
        if saveToFile then p.save_to_file = "true" end
        return _dispatch(device, "UpdatePositionAll", p)
    end,
    updatePositionAxis = function(device, posName, axis, contextKey)
        return _dispatch(device, "UpdatePositionAxis", {position_name = posName, axis = axis, context_key = contextKey})
    end,
    gridScan = function(device, params)
        return _dispatch(device, "GridScan", params)
    end,
    axisScan = function(device, params)
        return _dispatch(device, "AxisScan", params)
    end,
    xcan = function(device, params)
        return _dispatch(device, "XCAN", params)
    end,
    registerDeviceLink = function(device, primaryDevice, linkedDevice)
        return _dispatch(device, "RegisterDeviceLink", {primaryDevice = primaryDevice, linkedDevice = linkedDevice})
    end,
    compensateDeviceLink = function(device, primaryDevice, linkedDevice)
        return _dispatch(device, "CompensateDeviceLink", {primaryDevice = primaryDevice, linkedDevice = linkedDevice})
    end
}

-- ================================================================
-- Digital I/O
-- ================================================================
io = {
    setOutput = function(device, pinName, state)
        return _dispatch(device, "SetOutput", {pinName = pinName, state = tostring(state)})
    end,
    setOutputPin = function(device, pin, state)
        return _dispatch(device, "SetOutputPin", {pin = tostring(pin), state = tostring(state)})
    end,
    setOutputName = function(device, pinName)
        return _dispatch(device, "SetOutputName", {pinName = pinName})
    end,
    clearOutputName = function(device, pinName)
        return _dispatch(device, "ClearOutputName", {pinName = pinName})
    end,
    getInput = function(device, pinName)
        return _dispatch(device, "GetInput", {pinName = pinName})
    end,
    readInputs = function(device)
        return _dispatch(device, "ReadInputs", {})
    end,
    readOutputs = function(device)
        return _dispatch(device, "ReadOutputs", {})
    end,
    waitInput = function(device, pinName, state, timeoutMs)
        return _dispatch(device, "WaitForInput", {
            pinName = pinName, state = tostring(state), timeout = tostring(timeoutMs or 5000)
        })
    end,
    pulse = function(device, pinName, durationMs)
        return _dispatch(device, "Pulse", {pinName = pinName, milliseconds = tostring(durationMs)})
    end,
    clearLatch = function(device, latchMask)
        return _dispatch(device, "ClearLatch", {latchMask = tostring(latchMask)})
    end
}

-- ================================================================
-- Pneumatic slides
-- ================================================================
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
    stop = function(device)
        return _dispatch(device, "Stop", {})
    end,
    getPosition = function(device)
        return _dispatch(device, "GetPosition", {})
    end,
    getStatus = function(device)
        return _dispatch(device, "GetStatus", {})
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
)lua";

    const char* part2 = R"lua(
-- ================================================================
-- Camera
-- ================================================================
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
    captureToDUT = function(device, productName, serialNumber, filename)
        local p = {}
        if productName then p.product_name = productName end
        if serialNumber then p.serial_number = serialNumber end
        if filename then p.filename = filename end
        return _dispatch(device, "CaptureToDUT", p)
    end,
    captureAndConfirm = function(device, message)
        local p = {}
        if message then p.message = message end
        return _dispatch(device, "CaptureAndConfirm", p)
    end,
    checkPresent = function(device)
        return _dispatch(device, "CheckPresent", {})
    end,
    setExposure = function(device, exposureUs)
        return _dispatch(device, "SetExposure", {value = tostring(exposureUs)})
    end,
    setGain = function(device, gain)
        return _dispatch(device, "SetGain", {value = tostring(gain)})
    end,
    setAutoExposure = function(device, enable)
        return _dispatch(device, "SetAutoExposure", {value = tostring(enable)})
    end,
    setAutoGain = function(device, enable)
        return _dispatch(device, "SetAutoGain", {value = tostring(enable)})
    end
}

-- ================================================================
-- Vision (requires HAS_OPENCV)
-- ================================================================
vision = {
    runProfile = function(profilePath, cameraName)
        local p = {profile_path = profilePath, camera = cameraName or ""}
        _dispatch("vision", "execute_profile", p)
        local result = {}
        local keys = ctx.keys()
        for _, key in ipairs(keys) do
            local prefix = "vision.result."
            if key:sub(1, #prefix) == prefix then
                result[key:sub(#prefix + 1)] = ctx.get(key)
            end
        end
        return result
    end,
    autoFocus = function(device, camera, params)
        local p = {motion_device = device, camera = camera}
        if params then
            for k, v in pairs(params) do p[k] = tostring(v) end
        end
        return _dispatch("vision", "auto_focus", p)
    end,
    loadProfile = function(profilePath)
        return _dispatch("vision", "load_profile", {path = profilePath})
    end,
    enableCvInject = function()
        return _dispatch("vision", "enable_cv_inject", {})
    end,
    disableCvInject = function()
        return _dispatch("vision", "disable_cv_inject", {})
    end
}

-- ================================================================
-- TEC
-- ================================================================
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
    readLaserCurrent = function(device)
        _dispatch(device, "ReadLaserCurrent", {captureAs = "_slc"})
        return ctx.getNumeric("_slc")
    end,
    laserOn = function(device)
        return _dispatch(device, "LaserOn", {})
    end,
    laserOff = function(device)
        return _dispatch(device, "LaserOff", {})
    end,
    getStatus = function(device)
        return _dispatch(device, "GetStatus", {})
    end,
    connect = function(device)
        return _dispatch(device, "Connect", {})
    end,
    disconnect = function(device)
        return _dispatch(device, "Disconnect", {})
    end
}

-- ================================================================
-- Power supply
-- ================================================================
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
    end,
    readVoltageMultiple = function(device, count, delayMs)
        local p = {read_count = tostring(count or 10), delay_ms = tostring(delayMs or 100), captureAs = "_pvm"}
        _dispatch(device, "ReadVoltageMultiple", p)
        return ctx.getNumeric("_pvm")
    end,
    readCurrentMultiple = function(device, count, delayMs)
        local p = {read_count = tostring(count or 10), delay_ms = tostring(delayMs or 100), captureAs = "_pcm"}
        _dispatch(device, "ReadCurrentMultiple", p)
        return ctx.getNumeric("_pcm")
    end,
    connect = function(device)
        return _dispatch(device, "Connect", {})
    end,
    disconnect = function(device)
        return _dispatch(device, "Disconnect", {})
    end
}

-- ================================================================
-- Source measure unit
-- ================================================================
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
    end,
    setCompliance = function(device, limit, channel)
        local p = {limit = tostring(limit)}
        if channel then p.channel = tostring(channel) end
        return _dispatch(device, "SetCompliance", p)
    end,
    setMode = function(device, mode)
        return _dispatch(device, "SetMode", {mode = mode})
    end,
    startPolling = function(device, mode, intervalMs, channel)
        local p = {}
        if mode then p.mode = mode end
        if intervalMs then p.intervalMs = tostring(intervalMs) end
        if channel then p.channel = tostring(channel) end
        return _dispatch(device, "StartPolling", p)
    end,
    stopPolling = function(device)
        return _dispatch(device, "StopPolling", {})
    end,
    connect = function(device)
        return _dispatch(device, "Connect", {})
    end,
    disconnect = function(device)
        return _dispatch(device, "Disconnect", {})
    end
}
)lua";

    const char* part3 = R"lua(
-- ================================================================
-- Lighting
-- ================================================================
lighting = {
    setIntensity = function(device, channel, intensity)
        return _dispatch(device, "SetIntensity", {channel = tostring(channel), intensity = tostring(intensity)})
    end,
    turnOn = function(device, channel)
        return _dispatch(device, "TurnOn", {channel = tostring(channel)})
    end,
    turnOff = function(device, channel)
        return _dispatch(device, "TurnOff", {channel = tostring(channel)})
    end,
    allOff = function(device)
        return _dispatch(device, "AllOff", {})
    end,
    allOn = function(device, intensity)
        local p = {}
        if intensity then p.intensity = tostring(intensity) end
        return _dispatch(device, "AllOn", p)
    end,
    getStatus = function(device)
        return _dispatch(device, "GetStatus", {})
    end
}

-- ================================================================
-- DUT (Device Under Test)
-- ================================================================
dut = {
    create = function(productName, serialNumber)
        return _dispatch("dut", "Create", {productName = productName, serialNumber = serialNumber})
    end,
    load = function(productName, serialNumber)
        return _dispatch("dut", "Load", {productName = productName, serialNumber = serialNumber})
    end,
    loadOrCreate = function(productName, serialNumber)
        return _dispatch("dut", "LoadOrCreate", {productName = productName, serialNumber = serialNumber})
    end,
    close = function()
        return _dispatch("dut", "Close", {})
    end,
    insertString = function(key, value)
        return _dispatch("dut", "InsertString", {key = key, value = value})
    end,
    insertDouble = function(key, value)
        return _dispatch("dut", "InsertDouble", {key = key, value = tostring(value)})
    end,
    insertInteger = function(key, value)
        return _dispatch("dut", "InsertInteger", {key = key, value = tostring(value)})
    end,
    insertFromContext = function(entryKey, contextKey, dataType)
        return _dispatch("dut", "InsertFromContext", {entry_key = entryKey, context_key = contextKey, data_type = dataType or "double"})
    end,
    deleteEntry = function(key)
        return _dispatch("dut", "DeleteEntry", {key = key})
    end
}

-- ================================================================
-- System commands
-- ================================================================
bobcat = {
    wait = function(ms)
        return _dispatch("system", "Wait", {milliseconds = tostring(ms)})
    end,
    delay = function(ms)
        return _dispatch("system", "Delay", {milliseconds = tostring(ms)})
    end,
    prompt = function(title, message)
        return _dispatch("system", "UserPrompt", {title = title or "", message = message or ""})
    end,
    askInput = function(title, prompt, defaultVal, contextKey)
        return _dispatch("system", "AskForInput", {
            title = title or "", prompt = prompt or "",
            default_value = defaultVal or "", context_key = contextKey or "_input"
        })
    end,
    logMsg = function(message, level)
        return _dispatch("system", "Log", {message = message, log_level = level or "INFO"})
    end,
    stopSequence = function()
        return _dispatch("system", "StopSequence", {})
    end
}

-- ================================================================
-- Auxiliary window
-- ================================================================
auxwindow = {
    setTarget = function(params)
        return _dispatch("auxwindow", "SetTarget", params)
    end,
    clearTarget = function()
        return _dispatch("auxwindow", "ClearTarget", {})
    end,
    enableCrosshair = function(enable)
        return _dispatch("auxwindow", "EnableCrosshair", {enable = tostring(enable ~= false)})
    end
}

-- ================================================================
-- Camera offset (mag switching)
-- ================================================================
cameraoffset = {
    moveToHighMag = function(calibrationSet, velocity)
        local p = {}
        if calibrationSet then p.calibration_set_name = calibrationSet end
        if velocity then p.velocity = tostring(velocity) end
        return _dispatch("cameraoffset", "MoveToHighMag", p)
    end,
    moveToLowMag = function(calibrationSet, velocity)
        local p = {}
        if calibrationSet then p.calibration_set_name = calibrationSet end
        if velocity then p.velocity = tostring(velocity) end
        return _dispatch("cameraoffset", "MoveToLowMag", p)
    end
}

-- ================================================================
-- Coordinate alignment
-- ================================================================
coordalign = {
    transformPoint = function(liveUid, inputX, inputY, outputPrefix)
        return _dispatch("coordalign", "TransformPoint", {
            live_uid = liveUid, input_x = tostring(inputX),
            input_y = tostring(inputY), output_prefix = outputPrefix
        })
    end,
    moveToLocalXY = function(localX, localY, csUid, motionDevice)
        return _dispatch("coordalign", "MoveToLocalXY", {
            local_x = tostring(localX), local_y = tostring(localY),
            coord_system_uid = csUid, motion_device = motionDevice
        })
    end
}

-- ================================================================
-- Relative position tracking
-- ================================================================
relpos = {
    captureBaseline = function(uid, parent, child)
        return _dispatch("relpos", "CaptureBaseline", {uid = uid, parent = parent, child = child})
    end,
    getDelta = function(uid, outputPrefix)
        return _dispatch("relpos", "GetDelta", {uid = uid, outputPrefix = outputPrefix or "_delta"})
    end,
    clearBaseline = function(uid)
        return _dispatch("relpos", "ClearBaseline", {uid = uid})
    end
}

-- ================================================================
-- Trainers (headless execution)
-- ================================================================
trainer = {
    runAlignment = function(trainingSetName, opts)
        local p = {training_set_name = trainingSetName}
        if opts then
            if opts.registerHexLeft then p.register_hex_left = "true" end
            if opts.registerHexRight then p.register_hex_right = "true" end
            if opts.moveToBonding then p.move_to_bonding = "true" end
        end
        return _dispatch("accuracytrainer", "RunAlignment", p)
    end,
    runEdgeTransfer = function(params)
        return _dispatch("edgetransfer", "RunAlignment", params or {})
    end,
    runPlanarAdjustment = function(params)
        return _dispatch("planaradjustment", "RunPlanarAdjustment", params or {})
    end,
    runZOffset = function(params)
        return _dispatch("zoffset", "RunZOffset", params or {})
    end,
    runYawCorrection = function(params)
        return _dispatch("yawtrainer", "RunYawCorrection", params or {})
    end
}

-- ================================================================
-- Custom beam server
-- ================================================================
beamserver = {
    connect = function(host, port)
        local p = {}
        if host then p.host = host end
        if port then p.port = tostring(port) end
        return _dispatch("beamserver", "Connect", p)
    end,
    disconnect = function()
        return _dispatch("beamserver", "Disconnect", {})
    end,
    detect = function(camera, params)
        local p = {camera_device = camera}
        if params then
            for k, v in pairs(params) do p[k] = tostring(v) end
        end
        return _dispatch("beamserver", "Detect", p)
    end,
    fit = function(params)
        return _dispatch("beamserver", "Fit", params or {})
    end,
    reset = function()
        return _dispatch("beamserver", "Reset", {})
    end,
    save = function(params)
        return _dispatch("beamserver", "Save", params or {})
    end
}

-- ================================================================
-- DataStore
-- ================================================================
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

    const char* parts[] = { part1, part2, part3 };
    for (const char* part : parts) {
        if (luaL_dostring(m_L, part) != LUA_OK) {
            std::string err = lua_tostring(m_L, -1);
            lua_pop(m_L, 1);
            if (m_logger) {
                m_logger->Error("[ScriptEngine] Failed to register convenience modules: " + err, "ScriptEngine");
            }
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
