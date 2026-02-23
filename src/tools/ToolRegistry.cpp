#include "ToolRegistry.h"

#include <string.h>
#include <stdio.h>

bool ToolRegistry::add(ITool* tool) {
    if (!tool || _count >= TOOL_REGISTRY_MAX) return false;
    // Prevent duplicate registrations
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_tools[i]->name(), tool->name()) == 0) return false;
    }
    _tools[_count++] = tool;
    return true;
}

ITool* ToolRegistry::find(const char* name) {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_tools[i]->name(), name) == 0) return _tools[i];
    }
    return nullptr;
}

bool ToolRegistry::invoke(const char* toolName,
                          const char* argsJson,
                          char*       resultBuf,
                          size_t      resultLen) {
    ITool* t = find(toolName);
    if (!t) {
        snprintf(resultBuf, resultLen,
                 "{\"error\":\"tool '%s' not found\"}", toolName);
        return false;
    }
    return t->execute(argsJson, resultBuf, resultLen);
}

void ToolRegistry::listTools(char* buf, size_t len) const {
    size_t pos = 0;
    for (uint8_t i = 0; i < _count && pos < len - 1; ++i) {
        int written = snprintf(buf + pos, len - pos,
                               "%s: %s\n",
                               _tools[i]->name(),
                               _tools[i]->description());
        if (written > 0) pos += (size_t)written;
    }
    if (pos < len) buf[pos] = '\0';
}

void ToolRegistry::schemasJson(char* buf, size_t len) const {
    // Build: [{"name":"...","description":"...","parameters":<schema>}, ...]
    size_t pos = 0;
    auto append = [&](const char* s) {
        size_t slen = strlen(s);
        if (pos + slen < len) {
            memcpy(buf + pos, s, slen);
            pos += slen;
        }
    };
    append("[");
    for (uint8_t i = 0; i < _count; ++i) {
        if (i > 0) append(",");
        append("{\"name\":\"");
        append(_tools[i]->name());
        append("\",\"description\":\"");
        append(_tools[i]->description());
        append("\",\"parameters\":");
        append(_tools[i]->argSchema());
        append("}");
    }
    append("]");
    buf[pos < len ? pos : len - 1] = '\0';
}
