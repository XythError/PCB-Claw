#pragma once

#include <stdint.h>
#include <string.h>
#include "../gateway/Message.h"

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────
// Task — a unit of work to be executed by the agent.
//
// Tasks are placed into a LaneQueue.  Each task has:
//   - A name/type string (e.g. "tool:gpio", "llm:chat")
//   - A payload (serialised JSON or plain text)
//   - A priority (higher = processed first within the same lane)
//   - A lane id (tasks in the same lane execute strictly in order)
//   - A reply channel (where to send the result)
//   - An origin message ID (for tracing)
// ─────────────────────────────────────────────────────────────────

static constexpr size_t TASK_NAME_LEN    = 32;
static constexpr size_t TASK_PAYLOAD_LEN = 2048;
static constexpr size_t TASK_LANE_LEN    = 16;

enum class TaskStatus : uint8_t {
    PENDING   = 0,
    RUNNING   = 1,
    DONE      = 2,
    FAILED    = 3,
    CANCELLED = 4,
};

struct Task {
    uint32_t   id       = 0;
    char       name[TASK_NAME_LEN]       = {};  // e.g. "tool:gpio_write"
    char       lane[TASK_LANE_LEN]       = {};  // e.g. "default", "gpio", "llm"
    char       payload[TASK_PAYLOAD_LEN] = {};  // JSON arguments
    char       origin_msg[MSG_ID_LEN]    = {};  // originating Message::id
    char       reply_channel[MSG_CHANNEL_LEN] = {};
    uint8_t    priority  = 1;   // 0=low, 1=normal, 2=high, 3=urgent
    TaskStatus status    = TaskStatus::PENDING;
    uint32_t   created_at = 0;

    static Task make(const char* name_,
                     const char* lane_,
                     const char* payload_,
                     const char* reply_ch = "serial",
                     uint8_t     prio = 1) {
        static uint32_t _counter = 0;
        Task t;
        t.id = ++_counter;
        strncpy(t.name,          name_,    TASK_NAME_LEN - 1);
        strncpy(t.lane,          lane_,    TASK_LANE_LEN - 1);
        strncpy(t.payload,       payload_, TASK_PAYLOAD_LEN - 1);
        strncpy(t.reply_channel, reply_ch, MSG_CHANNEL_LEN - 1);
        t.priority = prio;
#ifndef NATIVE_BUILD
        t.created_at = millis();
#endif
        return t;
    }
};
