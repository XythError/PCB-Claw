#include "Agent.h"

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <LittleFS.h>
#  include <esp_heap_caps.h>
#endif

// ─────────────────────────────────────────────────────────────────
// Agent implementation
// ─────────────────────────────────────────────────────────────────

Agent::Agent(Gateway&       gw,
             ToolRegistry&  tools,
             LaneQueue&     queue,
             ConfigManager& config)
    : _gw(gw), _tools(tools), _queue(queue), _config(config),
      _llm(tools), _workflows(tools)
{}

bool Agent::configure() {
    // Load agent config
    _config.load("/config/agent.md");

    // Configure LLM
    LlmConfig llmCfg;
    strncpy(llmCfg.provider,      _config.get("llm_provider", "openai"),
            sizeof(llmCfg.provider) - 1);
    strncpy(llmCfg.model,         _config.get("llm_model", "gpt-4o-mini"),
            sizeof(llmCfg.model) - 1);
    strncpy(llmCfg.api_key,       _config.get("llm_api_key", ""),
            sizeof(llmCfg.api_key) - 1);
    strncpy(llmCfg.endpoint,      _config.get("llm_endpoint", ""),
            sizeof(llmCfg.endpoint) - 1);
    strncpy(llmCfg.system_prompt, _config.get("system_prompt",
            "You are PCB-Claw, an ultra-efficient AI assistant on ESP32."),
            sizeof(llmCfg.system_prompt) - 1);
    llmCfg.max_tokens  = (uint16_t)_config.getInt("max_tokens",  512);
    llmCfg.temperature = _config.getFloat("temperature", 0.3f);
    _llm.configure(llmCfg);

    // Load workflows
    _workflows.loadFromFile("/config/workflows.md");

    return true;
}

bool Agent::begin() {
    // Lane setup — each hardware resource gets its own lane
    _queue.addLane("llm",     [this](const Task& t) -> bool {
        char result[LLM_RESPONSE_BUF] = {};
        char ctx[512] = {};
        _loadWorkspaceContext(ctx, sizeof(ctx));
        bool ok = _llm.reason(t.payload, ctx, result, sizeof(result));
        Message resp = Message::make(t.reply_channel, "agent", result,
                                     MessageType::RESPONSE);
        _gw.send(resp);
        return ok;
    });

    _queue.addLane("gpio",    [this](const Task& t) -> bool {
        char result[TOOL_RESULT_LEN] = {};
        bool ok = _tools.invoke("gpio", t.payload, result, sizeof(result));
        Message resp = Message::make(t.reply_channel, "agent", result,
                                     MessageType::RESPONSE);
        _gw.send(resp);
        return ok;
    });

    _queue.addLane("i2c",     [this](const Task& t) -> bool {
        char result[TOOL_RESULT_LEN] = {};
        bool ok = _tools.invoke("i2c", t.payload, result, sizeof(result));
        Message resp = Message::make(t.reply_channel, "agent", result,
                                     MessageType::RESPONSE);
        _gw.send(resp);
        return ok;
    });

    _queue.addLane("spi",     [this](const Task& t) -> bool {
        char result[TOOL_RESULT_LEN] = {};
        bool ok = _tools.invoke("spi", t.payload, result, sizeof(result));
        Message resp = Message::make(t.reply_channel, "agent", result,
                                     MessageType::RESPONSE);
        _gw.send(resp);
        return ok;
    });

    _queue.addLane("http",    [this](const Task& t) -> bool {
        char result[TOOL_RESULT_LEN] = {};
        bool ok = _tools.invoke("http", t.payload, result, sizeof(result));
        Message resp = Message::make(t.reply_channel, "agent", result,
                                     MessageType::RESPONSE);
        _gw.send(resp);
        return ok;
    });

    _queue.addLane("default", [this](const Task& t) -> bool {
        // Handles: workflow:run, status:query, command:exec, config:set
        if (strncmp(t.name, "workflow:", 9) == 0) {
            char wfName[WF_NAME_LEN] = {};
            // Extract workflow name from payload {"workflow":"<name>",...}
            const char* p = strstr(t.payload, "\"workflow\":\"");
            if (p) {
                p += 12;
                size_t i = 0;
                while (*p && *p != '"' && i < WF_NAME_LEN - 1) wfName[i++] = *p++;
                wfName[i] = '\0';
            }
            WorkflowResult wr = _workflows.run(wfName);
            Message resp = Message::make(
                t.reply_channel, "agent",
                wr.ok ? wr.output : wr.error,
                MessageType::RESPONSE);
            _gw.send(resp);
            return wr.ok;
        }

        if (strncmp(t.name, "status:", 7) == 0) {
            char statusBuf[512] = {};
            statusJson(statusBuf, sizeof(statusBuf));
            Message resp = Message::make(t.reply_channel, "agent", statusBuf,
                                         MessageType::RESPONSE);
            _gw.send(resp);
            return true;
        }

        if (strncmp(t.name, "command:", 8) == 0) {
            // Extract command name
            char cmd[32] = {};
            const char* p = strstr(t.payload, "\"command\":\"");
            if (p) {
                p += 11;
                size_t i = 0;
                while (*p && *p != '"' && i < sizeof(cmd)-1) cmd[i++]=*p++;
                cmd[i] = '\0';
            }
            char replyBuf[256];
            if (strcmp(cmd, "status") == 0) {
                statusJson(replyBuf, sizeof(replyBuf));
            } else if (strcmp(cmd, "help") == 0) {
                char toolList[512] = {};
                _tools.listTools(toolList, sizeof(toolList));
                snprintf(replyBuf, sizeof(replyBuf),
                         "PCB-Claw Commands:\n"
                         "/status /help /workflows /reset\n"
                         "\nTools:\n%s", toolList);
            } else if (strcmp(cmd, "workflows") == 0) {
                snprintf(replyBuf, sizeof(replyBuf),
                         "%d workflow(s) loaded.", _workflows.count());
            } else if (strcmp(cmd, "reset") == 0) {
                snprintf(replyBuf, sizeof(replyBuf), "Restarting...");
                Message resp = Message::make(t.reply_channel, "agent",
                                             replyBuf, MessageType::RESPONSE);
                _gw.send(resp);
#ifndef NATIVE_BUILD
                delay(500); ESP.restart();
#endif
                return true;
            } else {
                snprintf(replyBuf, sizeof(replyBuf),
                         "Unknown command: /%s  (try /help)", cmd);
            }
            Message resp = Message::make(t.reply_channel, "agent", replyBuf,
                                         MessageType::RESPONSE);
            _gw.send(resp);
            return true;
        }

        return false;
    });

    _queue.begin();
    return true;
}

bool Agent::process() {
    Message msg;
    if (!_gw.receive(&msg, 10)) return false;

    Intent intent = IntentDetector::detect(msg);
    DecomposedTasks tasks = TaskDecomposer::decompose(msg, intent);

    for (uint8_t i = 0; i < tasks.count; ++i) {
        if (!_queue.enqueue(tasks.tasks[i])) {
            ++_errors;
            _reply(msg, "{\"error\":\"task queue full\"}");
        }
    }
    ++_processed;
    return true;
}

void Agent::inject(const Message& msg) {
    _gw.push(msg);
}

void Agent::statusJson(char* buf, size_t len) const {
#ifndef NATIVE_BUILD
    uint32_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    uint32_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    snprintf(buf, len,
             "{\"device\":\"PCB-Claw\","
             "\"uptime_ms\":%lu,"
             "\"processed\":%lu,"
             "\"errors\":%lu,"
             "\"free_heap\":%lu,"
             "\"free_psram\":%lu,"
             "\"tools\":%d,"
             "\"workflows\":%d}",
             (unsigned long)millis(),
             (unsigned long)_processed,
             (unsigned long)_errors,
             (unsigned long)freeHeap,
             (unsigned long)freePsram,
             _tools.count(),
             _workflows.count());
#else
    snprintf(buf, len,
             "{\"device\":\"PCB-Claw\","
             "\"processed\":%lu,"
             "\"errors\":%lu,"
             "\"tools\":%d,"
             "\"workflows\":%d}",
             (unsigned long)_processed,
             (unsigned long)_errors,
             _tools.count(),
             _workflows.count());
#endif
}

void Agent::_reply(const Message& req, const char* text) {
    Message resp = Message::makeResponse(req, text);
    _gw.send(resp);
}

void Agent::_loadWorkspaceContext(char* buf, size_t len) {
#ifndef NATIVE_BUILD
    const char* wsPath = "/workspace/context.json";
    if (!LittleFS.exists(wsPath)) { buf[0]='\0'; return; }
    File f = LittleFS.open(wsPath, "r");
    if (!f) { buf[0]='\0'; return; }
    size_t n = f.readBytes(buf, len - 1);
    f.close();
    buf[n] = '\0';
#else
    snprintf(buf, len, "{}");
#endif
}

void Agent::_saveToWorkspace(const char* key, const char* value) {
#ifndef NATIVE_BUILD
    // Simple append: { "key": "value" }
    const char* wsPath = "/workspace/context.json";
    File f = LittleFS.open(wsPath, "a");
    if (!f) return;
    char line[CONFIG_KEY_LEN + CONFIG_VAL_LEN + 16];
    snprintf(line, sizeof(line), "{\"%s\":\"%s\"}\n", key, value);
    f.print(line);
    f.close();
#else
    (void)key; (void)value;
#endif
}
