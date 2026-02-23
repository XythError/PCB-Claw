#pragma once

#include "../gateway/Gateway.h"
#include "../queue/LaneQueue.h"
#include "../tools/ToolRegistry.h"
#include "IntentDetector.h"
#include "TaskDecomposer.h"
#include "ReasoningLoop.h"
#include "WorkflowEngine.h"
#include "PromptBuilder.h"
#include "../config/ConfigManager.h"

// ─────────────────────────────────────────────────────────────────
// Agent — the central AI reasoning unit.
//
// Lifecycle:
//   1. configure() — load agent.md config
//   2. begin()     — start lane workers, register tools
//   3. process()   — call from main loop; drains the gateway queue
//
// Per message the agent:
//   a. Detects intent (local, no LLM call)
//   b. Decomposes into tasks
//   c. Routes tasks into the LaneQueue
//   d. Lane workers invoke tools or LLM
//   e. Sends response back through the gateway
//
// The agent is intentionally stateless between messages to remain
// token-efficient.  Persistent state lives in the workspace files.
// ─────────────────────────────────────────────────────────────────

class Agent {
public:
    Agent(Gateway&      gateway,
          ToolRegistry& tools,
          LaneQueue&    queue,
          ConfigManager& config);

    // Load configuration from agent.md
    bool configure();

    // Register tools and start lane workers
    bool begin();

    // Process one message from the gateway queue.
    // Returns false if the queue was empty.
    bool process();

    // Direct injection (for testing / internal system messages)
    void inject(const Message& msg);

    // Returns a status JSON summary
    void statusJson(char* buf, size_t len) const;

    // Notify the agent whether the HTTP web server is running.
    // Used by PromptBuilder to include accurate server status in the prompt.
    void setWebServerRunning(bool running);

private:
    Gateway&       _gw;
    ToolRegistry&  _tools;
    LaneQueue&     _queue;
    ConfigManager& _config;
    ReasoningLoop  _llm;
    WorkflowEngine _workflows;
    PromptBuilder  _promptBuilder;

    uint32_t _processed = 0;
    uint32_t _errors    = 0;

    void _handleChat(const Message& msg);
    void _handleCommand(const Message& msg);
    void _handleHardware(const Message& msg, Intent intent);
    void _handleWorkflow(const Message& msg);
    void _handleStatus(const Message& msg);
    void _handleConfigCmd(const Message& msg);

    void _reply(const Message& req, const char* text);

    // Workspace helpers
    void _loadWorkspaceContext(char* buf, size_t len);
    void _saveToWorkspace(const char* key, const char* value);

    // Extract text that follows prefix inside a command task payload's
    // "args" JSON field (e.g. prefix="/remember " extracts the fact text).
    static void _extractCommandText(const char* payload,
                                     const char* prefix,
                                     char*       out,
                                     size_t      outLen);
};
