// ScriptWorker.cpp - Runs Lua scripts on a background thread
#include "ScriptWorker.h"
#include "ScriptEngine.h"

ScriptWorker::ScriptWorker(ScriptEngine* engine, const std::string& scriptPathOrCode,
                           Mode mode, QObject* parent)
    : QThread(parent)
    , m_engine(engine)
    , m_scriptPathOrCode(scriptPathOrCode)
    , m_mode(mode)
{
    connect(this, &QThread::finished, this, &QObject::deleteLater);
}

void ScriptWorker::requestStop()
{
    m_stopRequested.store(true);
    if (m_engine) {
        m_engine->RequestStop();
    }
}

void ScriptWorker::run()
{
    emit executionStarted();

    if (!m_engine) {
        emit executionCompleted(false, "Script engine is null");
        return;
    }

    if (m_engine->IsRunning()) {
        emit executionCompleted(false, "Script engine is busy");
        return;
    }

    m_engine->SetOutputCallback([this](const std::string& msg) {
        emit scriptOutput(QString::fromStdString(msg));
    });

    std::string errorMessage;
    bool success = false;

    if (m_mode == Mode::FILE) {
        success = m_engine->ExecuteFile(m_scriptPathOrCode, errorMessage);
    } else {
        success = m_engine->ExecuteString(m_scriptPathOrCode, errorMessage);
    }

    m_engine->SetOutputCallback(nullptr);

    emit executionCompleted(success, QString::fromStdString(errorMessage));
}
