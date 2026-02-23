#pragma once

#include "../queue/Task.h"
#include "../tools/ToolRegistry.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <LittleFS.h>
#  include <Arduino.h>
#  include <esp_heap_caps.h>
#endif

// ─────────────────────────────────────────────────────────────────
// WorkflowEngine — build and run structured multi-step workflows.
//
// A workflow is a sequence of steps defined in an .md file stored
// on LittleFS.  Each step specifies:
//   - tool:   which tool to invoke
//   - args:   JSON arguments (may reference previous step outputs
//             via ${step_N.result})
//   - lane:   which lane to use (default: "default")
//
// Workflow .md format (data/config/workflows.md or
//                      data/workflows/<name>.md):
//
//   # blink_led
//   ## steps
//   - tool: gpio
//     args: {"op":"pin_mode","pin":2,"mode":"OUTPUT"}
//   - tool: gpio
//     args: {"op":"write","pin":2,"value":1}
//   - tool: gpio
//     args: {"op":"write","pin":2,"value":0}
//
// Workflows can also be generated dynamically by the agent and
// written back to LittleFS for future reuse.
// ─────────────────────────────────────────────────────────────────

static constexpr uint8_t  WF_MAX_STEPS     = 16;
static constexpr uint8_t  WF_MAX_WORKFLOWS = 16;
static constexpr size_t   WF_NAME_LEN      = 32;
static constexpr size_t   WF_STEP_ARG_LEN  = 256;

struct WorkflowStep {
    char tool[TOOL_NAME_LEN]    = {};  // tool name
    char args[WF_STEP_ARG_LEN]  = {};  // JSON args
    char lane[TASK_LANE_LEN]    = "default";
    bool optional               = false;
};

struct Workflow {
    char         name[WF_NAME_LEN]     = {};
    WorkflowStep steps[WF_MAX_STEPS]   = {};
    uint8_t      stepCount             = 0;
    bool         valid                 = false;
};

// ── Execution result ──────────────────────────────────────────────
struct WorkflowResult {
    bool    ok        = false;
    uint8_t failStep  = 0;
    char    error[128] = {};
    char    output[TOOL_RESULT_LEN] = {};  // last step result
};

// ─────────────────────────────────────────────────────────────────

class WorkflowEngine {
public:
    explicit WorkflowEngine(ToolRegistry& tools) : _tools(tools) {}

    // Parse and register all workflows from data/config/workflows.md
    bool loadFromFile(const char* path = "/config/workflows.md");

    // Register a workflow object directly (e.g. agent-generated)
    bool registerWorkflow(const Workflow& wf);

    // Run a workflow by name synchronously (blocking).
    // Results are written into WorkflowResult.
    WorkflowResult run(const char* name);

    // Serialize workflow to .md text for saving to LittleFS
    static size_t serialize(const Workflow& wf, char* buf, size_t len);

    // Parse a workflow from .md text
    static Workflow parse(const char* md);

    uint8_t count() const { return _count; }

    const Workflow* find(const char* name) const;

private:
    ToolRegistry& _tools;
    Workflow      _workflows[WF_MAX_WORKFLOWS] = {};
    uint8_t       _count = 0;

    // Substitute ${step_N.result} placeholders in args
    static void _interpolate(char* args, size_t len,
                              const char stepResults[][TOOL_RESULT_LEN],
                              uint8_t numResults);
};

// ── Inline implementations ────────────────────────────────────────

inline bool WorkflowEngine::registerWorkflow(const Workflow& wf) {
    if (_count >= WF_MAX_WORKFLOWS) return false;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_workflows[i].name, wf.name) == 0) {
            _workflows[i] = wf;  // update in-place
            return true;
        }
    }
    _workflows[_count++] = wf;
    return true;
}

inline const Workflow* WorkflowEngine::find(const char* name) const {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_workflows[i].name, name) == 0) return &_workflows[i];
    }
    return nullptr;
}

inline WorkflowResult WorkflowEngine::run(const char* name) {
    WorkflowResult res;
    const Workflow* wf = find(name);
    if (!wf || !wf->valid) {
        res.ok = false;
        snprintf(res.error, sizeof(res.error),
                 "workflow '%s' not found", name ? name : "null");
        return res;
    }

    // Step result history for interpolation
    char stepResults[WF_MAX_STEPS][TOOL_RESULT_LEN] = {};

    for (uint8_t i = 0; i < wf->stepCount; ++i) {
        const WorkflowStep& step = wf->steps[i];
        char args[WF_STEP_ARG_LEN];
        strncpy(args, step.args, sizeof(args) - 1);

        // Interpolate previous results
        _interpolate(args, sizeof(args), stepResults, i);

        bool ok = _tools.invoke(step.tool, args,
                                stepResults[i], TOOL_RESULT_LEN);
        if (!ok && !step.optional) {
            res.ok       = false;
            res.failStep = i;
            snprintf(res.error, sizeof(res.error),
                     "step %d (tool '%s') failed",
                     i, step.tool);
            return res;
        }
    }

    res.ok = true;
    if (wf->stepCount > 0) {
        strncpy(res.output,
                stepResults[wf->stepCount - 1],
                sizeof(res.output) - 1);
    }
    return res;
}

inline bool WorkflowEngine::loadFromFile(const char* path) {
#ifndef NATIVE_BUILD
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    // Read the file in chunks into a heap-allocated buffer to avoid
    // a large stack allocation.
    const size_t BUF_SIZE = 4096;
    char* buf = (char*)heap_caps_malloc(BUF_SIZE, MALLOC_CAP_SPIRAM |
                                        MALLOC_CAP_8BIT);
    if (!buf) buf = (char*)malloc(BUF_SIZE);  // fallback to DRAM
    if (!buf) { f.close(); return false; }

    size_t n = f.readBytes(buf, BUF_SIZE - 1);
    f.close();
    buf[n] = '\0';

    // Parse multiple workflows separated by "# " headings
    // Each "# name" line starts a new workflow
    const char* p = buf;
    // Section buffer — sized for one workflow's steps (max WF_MAX_STEPS
    // steps × ~32 bytes each + name line ≈ 600 bytes; 1 KB is safe).
    static constexpr size_t SEC_BUF = 1024;
    while (*p) {
        if (*p == '#' && *(p+1) == ' ') {
            // Find end of this workflow section
            const char* sectionEnd = p + 2;
            while (*sectionEnd) {
                if (sectionEnd[0] == '\n' && sectionEnd[1] == '#' &&
                    sectionEnd[2] == ' ') break;
                ++sectionEnd;
            }
            // Copy section into a stack buffer (bounded to SEC_BUF)
            size_t secLen = (size_t)(sectionEnd - p);
            if (secLen < SEC_BUF) {
                char section[SEC_BUF];
                memcpy(section, p, secLen);
                section[secLen] = '\0';
                Workflow wf = parse(section);
                if (wf.valid) registerWorkflow(wf);
            }
            p = (*sectionEnd == '\0') ? sectionEnd : sectionEnd + 1;
        } else {
            while (*p && *p != '\n') ++p;
            if (*p) ++p;
        }
    }
    free(buf);
    return true;
#else
    (void)path;
    return false;
#endif
}

inline Workflow WorkflowEngine::parse(const char* md) {
    Workflow wf;
    if (!md) return wf;

    const char* p = md;

    // Extract name from "# <name>" line
    if (*p == '#' && *(p+1) == ' ') {
        p += 2;
        size_t i = 0;
        while (*p && *p != '\n' && i < WF_NAME_LEN - 1) {
            wf.name[i++] = *p++;
        }
        wf.name[i] = '\0';
        // Trim trailing whitespace
        while (i > 0 && (wf.name[i-1] == ' ' || wf.name[i-1] == '\r')) {
            wf.name[--i] = '\0';
        }
    }

    if (wf.name[0] == '\0') return wf;

    // Parse steps: lines matching "- tool: <name>" followed by
    //              "  args: <json>"
    while (*p) {
        if (wf.stepCount >= WF_MAX_STEPS) break;

        // Skip to next "- tool:" line
        while (*p && !(*p == '-' && *(p+1) == ' ')) {
            while (*p && *p != '\n') ++p;
            if (*p) ++p;
        }
        if (!*p) break;

        p += 2;  // skip "- "
        if (strncmp(p, "tool:", 5) != 0) { while (*p && *p != '\n') ++p; continue; }
        p += 5;
        while (*p == ' ') ++p;

        WorkflowStep& step = wf.steps[wf.stepCount];
        size_t ti = 0;
        while (*p && *p != '\n' && *p != '\r' && ti < TOOL_NAME_LEN - 1) {
            step.tool[ti++] = *p++;
        }
        step.tool[ti] = '\0';

        // Advance to next line — look for "args:" or "lane:" or "optional:"
        while (*p && *p != '\n') ++p;
        if (*p) ++p;

        // Parse optional sub-keys (indented)
        while (*p == ' ' || *p == '\t') {
            while (*p == ' ' || *p == '\t') ++p;
            if (strncmp(p, "args:", 5) == 0) {
                p += 5; while (*p == ' ') ++p;
                size_t ai = 0;
                while (*p && *p != '\n' && *p != '\r' &&
                       ai < WF_STEP_ARG_LEN - 1) {
                    step.args[ai++] = *p++;
                }
                step.args[ai] = '\0';
            } else if (strncmp(p, "lane:", 5) == 0) {
                p += 5; while (*p == ' ') ++p;
                size_t li = 0;
                while (*p && *p != '\n' && li < TASK_LANE_LEN - 1) {
                    step.lane[li++] = *p++;
                }
                step.lane[li] = '\0';
            } else if (strncmp(p, "optional:", 9) == 0) {
                p += 9; while (*p == ' ') ++p;
                step.optional = (*p == 't' || *p == 'T' || *p == '1');
            }
            while (*p && *p != '\n') ++p;
            if (*p) ++p;
        }

        // Default args if none provided
        if (step.args[0] == '\0') strncpy(step.args, "{}", 3);
        if (step.lane[0] == '\0') strncpy(step.lane, "default",
                                           TASK_LANE_LEN - 1);

        ++wf.stepCount;
    }

    wf.valid = (wf.stepCount > 0);
    return wf;
}

inline size_t WorkflowEngine::serialize(const Workflow& wf,
                                         char* buf, size_t len) {
    size_t pos = 0;
    auto w = [&](const char* s) {
        size_t sl = strlen(s);
        if (pos + sl < len) { memcpy(buf + pos, s, sl); pos += sl; }
    };
    w("# "); w(wf.name); w("\n## steps\n");
    for (uint8_t i = 0; i < wf.stepCount; ++i) {
        char line[TOOL_NAME_LEN + WF_STEP_ARG_LEN + TASK_LANE_LEN + 32];
        snprintf(line, sizeof(line),
                 "- tool: %s\n  args: %s\n  lane: %s\n",
                 wf.steps[i].tool,
                 wf.steps[i].args,
                 wf.steps[i].lane);
        w(line);
    }
    if (pos < len) buf[pos] = '\0';
    return pos;
}

inline void WorkflowEngine::_interpolate(
        char* args, size_t len,
        const char stepResults[][TOOL_RESULT_LEN],
        uint8_t numResults) {

    // Replace ${step_N.result} with the corresponding step result
    for (uint8_t i = 0; i < numResults; ++i) {
        char placeholder[32];
        snprintf(placeholder, sizeof(placeholder), "${step_%d.result}", i);
        const char* ph = strstr(args, placeholder);
        if (!ph) continue;

        size_t phLen   = strlen(placeholder);
        size_t resLen  = strlen(stepResults[i]);
        size_t before  = (size_t)(ph - args);
        size_t after   = strlen(ph + phLen);
        size_t newLen  = before + resLen + after;

        if (newLen + 1 > len) continue;  // won't fit

        char tmp[WF_STEP_ARG_LEN];
        memcpy(tmp, args, before);
        memcpy(tmp + before, stepResults[i], resLen);
        memcpy(tmp + before + resLen, ph + phLen, after + 1);
        strncpy(args, tmp, len - 1);
    }
}
