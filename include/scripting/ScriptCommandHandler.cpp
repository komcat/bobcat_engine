// ScriptCommandHandler.cpp - Handles RunFile, RunString, and RunRecipe methods
#include "ScriptCommandHandler.h"
#include "ScriptEngine.h"
#include "LogManager.h"
#include "ApplicationContext.h"
#include "RecipeWorker.h"
#include "ProcessSequenceFactory.h"
#include "product_position_manager.h"
#include "SequenceEngine.h"

#include <QEventLoop>
#include <QObject>
#include <QString>
#include <filesystem>

namespace {

// Resolve a recipe name/path to an absolute recipe file path.
// If "recipe" is an absolute or obviously-relative path (contains a separator or ends in .json),
// it is used as-is. Otherwise, it is resolved to {productDir}/recipes/{recipe}.json.
std::string ResolveRecipePath(const std::string& recipe, ApplicationContext* ctx, std::string& errorMessage)
{
    namespace fs = std::filesystem;

    fs::path input(recipe);
    const bool looksLikePath = input.has_extension() ||
        recipe.find('/') != std::string::npos ||
        recipe.find('\\') != std::string::npos;

    if (looksLikePath) {
        if (!fs::exists(input)) {
            errorMessage = "Recipe file not found: " + recipe;
            return {};
        }
        return fs::absolute(input).string();
    }

    if (!ctx || !ctx->productPositionManager) {
        errorMessage = "No product loaded — cannot resolve bare recipe name '" + recipe + "'";
        return {};
    }

    auto* ppm = ctx->productPositionManager;
    if (!ppm->HasLoadedProduct()) {
        errorMessage = "No product loaded — cannot resolve bare recipe name '" + recipe + "'";
        return {};
    }

    fs::path productFile(ppm->GetCurrentProductPath());
    fs::path productDir = productFile.has_parent_path()
        ? productFile.parent_path() / ppm->GetCurrentProductName()
        : fs::path("config") / ppm->GetCurrentProductName();

    // Product file is typically `config/{productName}/positions.{productName}.json`,
    // so its parent_path is already `config/{productName}`.
    fs::path productParent = productFile.parent_path();
    fs::path candidate = productParent / "recipes" / (recipe + ".json");

    if (!fs::exists(candidate)) {
        errorMessage = "Recipe file not found: " + candidate.string();
        return {};
    }
    return fs::absolute(candidate).string();
}

} // namespace

bool ScriptCommandHandler::ExecuteCommand(
    void* /*manager*/,
    const std::string& /*deviceName*/,
    const std::string& methodName,
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    if (!m_scriptEngine) {
        errorMessage = "Script engine not initialized";
        return false;
    }

    if (methodName == "RunRecipe") {
        return ExecuteRunRecipe(params, errorMessage, context);
    }

    // RunFile / RunString are only callable when the engine is idle
    // (they start a top-level script). RunRecipe runs synchronously inside
    // the currently-running script, so the busy check does not apply.
    if (m_scriptEngine->IsRunning()) {
        errorMessage = "Script engine is busy — another script is already running";
        return false;
    }

    if (methodName == "RunFile") {
        auto it = params.find("path");
        if (it == params.end() || it->second.empty()) {
            errorMessage = "RunFile requires 'path' parameter";
            return false;
        }

        if (m_logger) {
            m_logger->Info("[ScriptCmd] Running script: " + it->second, "ScriptCmd");
        }

        return m_scriptEngine->ExecuteFile(it->second, errorMessage);
    }

    if (methodName == "RunString") {
        auto it = params.find("code");
        if (it == params.end() || it->second.empty()) {
            errorMessage = "RunString requires 'code' parameter";
            return false;
        }

        if (m_logger) {
            m_logger->Debug("[ScriptCmd] Running inline script", "ScriptCmd");
        }

        return m_scriptEngine->ExecuteString(it->second, errorMessage);
    }

    errorMessage = "Unknown scripting method: " + methodName;
    return false;
}

bool ScriptCommandHandler::ExecuteRunRecipe(
    const std::map<std::string, std::string>& params,
    std::string& errorMessage,
    SequenceContext* context)
{
    auto it = params.find("recipe");
    if (it == params.end() || it->second.empty()) {
        errorMessage = "RunRecipe requires 'recipe' parameter";
        return false;
    }

    if (!context) {
        errorMessage = "RunRecipe requires a SequenceContext (Lua script context)";
        return false;
    }

    ApplicationContext* appCtx = m_scriptEngine->GetAppContext();
    if (!appCtx) {
        errorMessage = "ApplicationContext not available";
        return false;
    }
    if (!appCtx->sequenceEngine || !appCtx->productPositionManager || !appCtx->motionManager) {
        errorMessage = "Required managers not available (sequenceEngine/positionManager/motionManager)";
        return false;
    }

    std::string recipePath = ResolveRecipePath(it->second, appCtx, errorMessage);
    if (recipePath.empty()) {
        return false;
    }

    if (m_logger) {
        m_logger->Info("[ScriptCmd] RunRecipe: " + recipePath, "ScriptCmd");
    }

    ProcessSequenceFactory factory;

    // RecipeWorker's destructor clears SetUserPromptCallback / SetUserInputCallback
    // on the shared SequenceEngine — that kills bobcat.prompt()/askInput() from
    // the Lua script after the recipe returns. Save + restore them around the run.
    auto savedPrompt = appCtx->sequenceEngine->GetUserPromptCallback();
    auto savedInput  = appCtx->sequenceEngine->GetUserInputCallback();

    auto* worker = new RecipeWorker(
        appCtx->sequenceEngine,
        &factory,
        appCtx->productPositionManager,
        appCtx->motionManager,
        recipePath,
        nullptr
    );
    worker->setExternalContext(context);
    worker->setLoopMode(true); // reduces per-step logging; script owns the narrative
    // Forward the host's UserPrompt/UserInput callbacks into the RecipeWorker so
    // prompts inside the recipe also route to the Script Runner's dialogs.
    if (savedPrompt) worker->setUserPromptCallback(savedPrompt);
    if (savedInput)  worker->setUserInputCallback(savedInput);
    // Recipe log lines are already written to the main log viewer by RecipeWorker.
    // We do NOT forward them back through m_logger — RecipeWorker's LogManager
    // UICallback would re-emit each forwarded entry, causing an amplification loop.

    bool success = false;
    QString errMsg;
    QEventLoop loop;

    QObject::connect(worker, &RecipeWorker::executionCompleted, &loop,
        [&loop, &success, &errMsg](bool ok, QString msg) {
            success = ok;
            errMsg = msg;
            loop.quit();
        },
        Qt::QueuedConnection);

    QObject::connect(worker, &RecipeWorker::executionStopped, &loop,
        [&loop, &success]() {
            success = false;
            loop.quit();
        },
        Qt::QueuedConnection);

    worker->start();
    loop.exec();
    worker->wait();
    delete worker;

    // RecipeWorker's dtor wiped the engine's prompt/input callbacks — put them back.
    appCtx->sequenceEngine->SetUserPromptCallback(savedPrompt);
    appCtx->sequenceEngine->SetUserInputCallback(savedInput);

    if (!success) {
        errorMessage = errMsg.isEmpty()
            ? std::string("Recipe execution failed: ") + recipePath
            : errMsg.toStdString();

        auto failIt = params.find("fail_on_error");
        const bool failOnError = (failIt == params.end()) || (failIt->second != "false");
        if (!failOnError) {
            if (m_logger) {
                m_logger->Warning("[ScriptCmd] Recipe failed (suppressed): " + errorMessage, "ScriptCmd");
            }
            errorMessage.clear();
            return true;
        }
        return false;
    }
    return true;
}
