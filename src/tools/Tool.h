#pragma once

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// ─────────────────────────────────────────────────────────────────
// ITool — abstract interface for every agent capability/skill.
//
// Tools are the building blocks the agent uses to interact with
// the physical world (GPIO, I2C, SPI) and external services (HTTP).
//
// Each tool:
//   • Has a unique name (used by the agent to select it)
//   • Declares a JSON schema for its arguments (for LLM function calls)
//   • Executes with a JSON payload and writes a JSON result
//
// The ToolRegistry owns all tool instances and provides name-based
// lookup.  The agent calls ToolRegistry::invoke(name, args, result).
// ─────────────────────────────────────────────────────────────────

static constexpr size_t TOOL_NAME_LEN   = 32;
static constexpr size_t TOOL_DESC_LEN   = 128;
static constexpr size_t TOOL_ARG_LEN    = 1024;
static constexpr size_t TOOL_RESULT_LEN = 1024;

class ITool {
public:
    virtual ~ITool() = default;

    // Short identifier used to invoke the tool (e.g. "gpio_write")
    virtual const char* name() const = 0;

    // Human-readable description for LLM function-calling schema
    virtual const char* description() const = 0;

    // JSON schema string for arguments (OpenAI function-call format)
    virtual const char* argSchema() const = 0;

    // Execute the tool.
    //   argsJson   — JSON object with named arguments
    //   resultBuf  — output buffer to write JSON result into
    //   resultLen  — size of resultBuf
    // Returns true on success.
    virtual bool execute(const char* argsJson,
                         char*       resultBuf,
                         size_t      resultLen) = 0;

    // Optional: called once at system startup
    virtual bool begin() { return true; }
};
