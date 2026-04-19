// ScriptEngine.h - Lua 5.4 scripting engine for bobcat automation
#pragma once

#include <string>
#include <map>
#include <atomic>
#include <functional>
#include <memory>

struct lua_State;
struct lua_Debug;
class ApplicationContext;
class SequenceEngine;
class SequenceContext;
class LogManager;

class ScriptEngine {
public:
    explicit ScriptEngine(ApplicationContext* appContext);
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    [[nodiscard]] bool Initialize();

    [[nodiscard]] bool ExecuteFile(const std::string& scriptPath, std::string& errorMessage);
    [[nodiscard]] bool ExecuteString(const std::string& code, std::string& errorMessage);

    void RequestStop();
    void ClearStopFlag();
    [[nodiscard]] bool IsStopRequested() const { return m_stopRequested.load(); }
    [[nodiscard]] bool IsRunning() const { return m_running.load(); }

    SequenceContext* GetContext() const { return m_context.get(); }
    SequenceEngine* GetSequenceEngine() const { return m_seqEngine; }
    ApplicationContext* GetAppContext() const { return m_appContext; }
    LogManager* GetLogger() const { return m_logger; }

    using OutputCallback = std::function<void(const std::string& message)>;
    void SetOutputCallback(OutputCallback callback) { m_outputCallback = callback; }

private:
    lua_State* m_L = nullptr;
    ApplicationContext* m_appContext = nullptr;
    SequenceEngine* m_seqEngine = nullptr;
    std::unique_ptr<SequenceContext> m_context;
    LogManager* m_logger = nullptr;

    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_running{false};

    OutputCallback m_outputCallback;

    void RegisterBindings();
    void RegisterConvenienceModules();
    void Sandbox();

    static void StopCheckHook(lua_State* L, lua_Debug* ar);
    static int LuaDispatch(lua_State* L);
    static int LuaPrint(lua_State* L);

    static int LuaCtxSet(lua_State* L);
    static int LuaCtxSetString(lua_State* L);
    static int LuaCtxGet(lua_State* L);
    static int LuaCtxGetNumeric(lua_State* L);
    static int LuaCtxGetString(lua_State* L);
    static int LuaCtxGetBoolean(lua_State* L);
    static int LuaCtxHas(lua_State* L);
    static int LuaCtxClear(lua_State* L);
    static int LuaCtxGetAllKeys(lua_State* L);

    static int LuaLogInfo(lua_State* L);
    static int LuaLogWarn(lua_State* L);
    static int LuaLogError(lua_State* L);
    static int LuaLogDebug(lua_State* L);

    static int LuaSystemWait(lua_State* L);

    static ScriptEngine* GetEngine(lua_State* L);
    static std::map<std::string, std::string> TableToParams(lua_State* L, int index);
    static std::string FormatLuaString(lua_State* L, int startArg);
};
