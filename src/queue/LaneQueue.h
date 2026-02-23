#pragma once

#include "Task.h"
#include "../gateway/Message.h"

#ifndef NATIVE_BUILD
#  include <freertos/FreeRTOS.h>
#  include <freertos/queue.h>
#  include <freertos/semphr.h>
#  include <freertos/task.h>
#else
#  include <mutex>
#  include <condition_variable>
#  include <queue>
#  include <functional>
#endif

// ─────────────────────────────────────────────────────────────────
// LaneQueue — sequential, multi-lane task scheduling system.
//
// A "lane" maps to a logical resource (e.g. "llm", "gpio", "i2c").
// Tasks within the same lane are executed STRICTLY in FIFO order,
// preventing race conditions on shared hardware or API endpoints.
// Tasks in different lanes may run concurrently.
//
// On ESP32 each lane maps to a dedicated FreeRTOS task that blocks
// on its own queue.  Workers call the registered TaskHandler when
// a task becomes available.
//
// Design goals:
//   • Tasks never overlap within the same lane
//   • No dynamic allocation in the critical path
//   • Simple to add new lanes at runtime
// ─────────────────────────────────────────────────────────────────

static constexpr uint8_t LANE_QUEUE_DEPTH   = 8;
static constexpr uint8_t LANE_QUEUE_MAX_LANES = 8;
static constexpr uint32_t LANE_WORKER_STACK  = 4096;

// Default lane priorities (FreeRTOS task priority).
// Higher number = higher priority.  Keep below system tasks (~23).
static constexpr uint8_t LANE_PRIORITY_LOW  = 1;  // LLM inference
static constexpr uint8_t LANE_PRIORITY_MID  = 2;  // general / workflow
static constexpr uint8_t LANE_PRIORITY_HIGH = 3;  // hardware (GPIO/I2C/SPI)

// Callback type: executed by the lane worker when a task is dequeued.
// Returns true on success, false on failure.
#ifndef NATIVE_BUILD
typedef bool (*TaskHandler)(const Task& task);
#else
using TaskHandler = std::function<bool(const Task&)>;
#endif

// ── Per-lane descriptor ───────────────────────────────────────────
struct Lane {
    char        name[TASK_LANE_LEN] = {};
    TaskHandler handler             = nullptr;
    uint8_t     priority            = LANE_PRIORITY_MID;  // FreeRTOS priority
    bool        active              = false;
    volatile bool* stopped          = nullptr;  // points to LaneQueue::_stopped

#ifndef NATIVE_BUILD
    QueueHandle_t     queue   = nullptr;
    TaskHandle_t      worker  = nullptr;
    SemaphoreHandle_t mutex   = nullptr;  // guards is_busy
    volatile bool     is_busy = false;
#else
    std::queue<Task>         nativeQueue;
    std::mutex               mtx;
    std::condition_variable  cv;
    volatile bool            is_busy = false;
#endif
};

// ─────────────────────────────────────────────────────────────────

class LaneQueue {
public:
    LaneQueue() = default;
    ~LaneQueue();

    // Register a named lane with its handler function.
    // priority controls the FreeRTOS worker task priority:
    //   LANE_PRIORITY_LOW  (1) — LLM lane (can be pre-empted by hardware)
    //   LANE_PRIORITY_MID  (2) — default / workflow tasks
    //   LANE_PRIORITY_HIGH (3) — hardware control (GPIO, I2C, SPI)
    // Must be called before begin().
    bool addLane(const char* laneName, TaskHandler handler,
                 uint8_t priority = LANE_PRIORITY_MID);

    // Start all lane workers (FreeRTOS tasks on ESP32)
    void begin();

    // Enqueue a task.  The lane is selected from task.lane.
    // If no matching lane exists, uses the "default" lane.
    // Returns false if the target lane queue is full or emergency stop
    // is active.
    bool enqueue(const Task& task);

    // Returns true if the named lane is currently processing a task.
    bool isBusy(const char* laneName) const;

    // Returns true if ALL lanes are idle.
    bool allIdle() const;

    // Number of tasks waiting in a lane's queue
    uint8_t pending(const char* laneName) const;

    // Emergency stop: prevents new tasks from starting in all lanes.
    // Tasks currently executing will complete naturally.
    // Call clearStop() to resume normal operation.
    void emergencyStop()  { _stopped = true; }
    void clearStop()      { _stopped = false; }
    bool isStopped() const { return _stopped; }

private:
    Lane    _lanes[LANE_QUEUE_MAX_LANES] = {};
    uint8_t _count = 0;
    volatile bool _stopped = false;

    Lane*   _findLane(const char* name);
    const Lane* _findLane(const char* name) const;

#ifndef NATIVE_BUILD
    // FreeRTOS task entry point (static trampoline)
    static void _workerTask(void* pvParams);
#endif
};

// ── Inline / implementation ───────────────────────────────────────

inline LaneQueue::~LaneQueue() {
#ifndef NATIVE_BUILD
    for (uint8_t i = 0; i < _count; ++i) {
        if (_lanes[i].worker) vTaskDelete(_lanes[i].worker);
        if (_lanes[i].queue)  vQueueDelete(_lanes[i].queue);
        if (_lanes[i].mutex)  vSemaphoreDelete(_lanes[i].mutex);
    }
#endif
}

inline bool LaneQueue::addLane(const char* laneName, TaskHandler handler,
                                uint8_t priority) {
    if (_count >= LANE_QUEUE_MAX_LANES || !laneName || !handler) return false;
    Lane& l = _lanes[_count++];
    strncpy(l.name, laneName, TASK_LANE_LEN - 1);
    l.handler  = handler;
    l.priority = priority;
    l.stopped  = &_stopped;
#ifndef NATIVE_BUILD
    l.queue = xQueueCreate(LANE_QUEUE_DEPTH, sizeof(Task));
    l.mutex = xSemaphoreCreateMutex();
    l.active = (l.queue != nullptr && l.mutex != nullptr);
#else
    l.active = true;
#endif
    return l.active;
}

inline void LaneQueue::begin() {
#ifndef NATIVE_BUILD
    for (uint8_t i = 0; i < _count; ++i) {
        if (!_lanes[i].active) continue;
        // Pass a pointer to the Lane struct as task parameter
        xTaskCreate(_workerTask,
                    _lanes[i].name,
                    LANE_WORKER_STACK,
                    &_lanes[i],
                    _lanes[i].priority,    // per-lane FreeRTOS priority
                    &_lanes[i].worker);
    }
#endif
}

#ifndef NATIVE_BUILD
inline void LaneQueue::_workerTask(void* pvParams) {
    Lane* lane = static_cast<Lane*>(pvParams);
    Task  t;
    for (;;) {
        if (xQueueReceive(lane->queue, &t, portMAX_DELAY) == pdTRUE) {
            // Emergency stop: intentionally discard the dequeued task
            // so the queue drains without executing new operations.
            // The task is lost by design — stop is a safety measure.
            // Call clearStop() to resume normal operation.
            if (lane->stopped && *lane->stopped) continue;

            xSemaphoreTake(lane->mutex, portMAX_DELAY);
            lane->is_busy = true;
            xSemaphoreGive(lane->mutex);

            t.status = TaskStatus::RUNNING;
            bool ok  = lane->handler(t);
            t.status = ok ? TaskStatus::DONE : TaskStatus::FAILED;

            xSemaphoreTake(lane->mutex, portMAX_DELAY);
            lane->is_busy = false;
            xSemaphoreGive(lane->mutex);
        }
    }
}
#endif

inline bool LaneQueue::enqueue(const Task& task) {
    if (_stopped) return false;  // reject new tasks during emergency stop
    Lane* l = _findLane(task.lane);
    if (!l) l = _findLane("default");
    if (!l || !l->active) return false;

#ifndef NATIVE_BUILD
    return xQueueSendToBack(l->queue, &task, 0) == pdTRUE;
#else
    std::lock_guard<std::mutex> lock(l->mtx);
    if (l->nativeQueue.size() >= LANE_QUEUE_DEPTH) return false;
    l->nativeQueue.push(task);
    l->cv.notify_one();
    return true;
#endif
}

inline bool LaneQueue::isBusy(const char* laneName) const {
    const Lane* l = _findLane(laneName);
    return l ? l->is_busy : false;
}

inline bool LaneQueue::allIdle() const {
    for (uint8_t i = 0; i < _count; ++i) {
        if (_lanes[i].is_busy) return false;
#ifndef NATIVE_BUILD
        if (uxQueueMessagesWaiting(_lanes[i].queue) > 0) return false;
#else
        if (!_lanes[i].nativeQueue.empty()) return false;
#endif
    }
    return true;
}

inline uint8_t LaneQueue::pending(const char* laneName) const {
    const Lane* l = _findLane(laneName);
    if (!l) return 0;
#ifndef NATIVE_BUILD
    return (uint8_t)uxQueueMessagesWaiting(l->queue);
#else
    return (uint8_t)l->nativeQueue.size();
#endif
}

inline Lane* LaneQueue::_findLane(const char* name) {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_lanes[i].name, name) == 0) return &_lanes[i];
    }
    return nullptr;
}

inline const Lane* LaneQueue::_findLane(const char* name) const {
    if (!name) return nullptr;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_lanes[i].name, name) == 0) return &_lanes[i];
    }
    return nullptr;
}
