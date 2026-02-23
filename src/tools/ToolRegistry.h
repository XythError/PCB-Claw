#pragma once

#include "Tool.h"

static constexpr uint8_t TOOL_REGISTRY_MAX = 32;

// ─────────────────────────────────────────────────────────────────
// ToolRegistry — central registry for all agent skills/tools.
//
// Tools are registered once at startup (pre-installed skills).
// The agent can also register dynamically generated tools at
// runtime (self-building skills).
//
// Usage:
//   registry.add(&gpioTool);
//   registry.add(&i2cTool);
//   bool ok = registry.invoke("gpio_write", argsJson, result, sizeof(result));
// ─────────────────────────────────────────────────────────────────

class ToolRegistry {
public:
    ToolRegistry() = default;

    // Register a tool (pointer must remain valid for the lifetime of
    // the registry)
    bool add(ITool* tool);

    // Find a tool by name; returns nullptr if not found
    ITool* find(const char* name);

    // Invoke a tool by name, writing the JSON result into resultBuf.
    // Returns false if the tool is not found or execute() fails.
    bool invoke(const char* toolName,
                const char* argsJson,
                char*       resultBuf,
                size_t      resultLen);

    // Fill buf with a newline-separated list of "name: description"
    void listTools(char* buf, size_t len) const;

    // Build a JSON array of tool schemas suitable for LLM function-call
    // integration.  Writes into buf.
    void schemasJson(char* buf, size_t len) const;

    uint8_t count() const { return _count; }

private:
    ITool*  _tools[TOOL_REGISTRY_MAX] = {};
    uint8_t _count = 0;
};
