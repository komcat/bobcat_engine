// ScriptWorker.h - QThread wrapper for async Lua script execution
#pragma once

#include <QThread>
#include <string>
#include <atomic>

class ScriptEngine;

class ScriptWorker : public QThread
{
    Q_OBJECT

public:
    enum class Mode { FILE, STRING };

    ScriptWorker(ScriptEngine* engine, const std::string& scriptPathOrCode,
                 Mode mode = Mode::FILE, QObject* parent = nullptr);

    void requestStop();

signals:
    void executionStarted();
    void executionCompleted(bool success, QString errorMessage);
    void scriptOutput(QString message);

protected:
    void run() override;

private:
    ScriptEngine* m_engine;
    std::string m_scriptPathOrCode;
    Mode m_mode;
    std::atomic<bool> m_stopRequested{false};
};
