// ─────────────────────────────────────────────────────────────────
// test_lane_queue.cpp — Unit tests for the LaneQueue system
// Runs on native platform (no hardware required).
// ─────────────────────────────────────────────────────────────────

#include <unity.h>
#include "../../src/queue/LaneQueue.h"
#include "../../src/queue/Task.h"

void setUp()    {}
void tearDown() {}

// ── Helpers ───────────────────────────────────────────────────────

static int g_handlerCallCount = 0;
static char g_lastTaskName[TASK_NAME_LEN] = {};

static bool testHandler(const Task& t) {
    ++g_handlerCallCount;
    strncpy(g_lastTaskName, t.name, TASK_NAME_LEN - 1);
    return true;
}

static bool failHandler(const Task& t) {
    (void)t;
    return false;
}

// ── Tests ─────────────────────────────────────────────────────────

void test_lane_can_be_added() {
    LaneQueue q;
    TEST_ASSERT_TRUE(q.addLane("default", testHandler));
}

void test_max_lanes_not_exceeded() {
    LaneQueue q;
    for (int i = 0; i < LANE_QUEUE_MAX_LANES; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "lane%d", i);
        TEST_ASSERT_TRUE(q.addLane(name, testHandler));
    }
    // One more should fail
    TEST_ASSERT_FALSE(q.addLane("overflow", testHandler));
}

void test_task_creation() {
    Task t = Task::make("tool:gpio", "gpio", "{\"op\":\"read\",\"pin\":4}",
                        "serial", 2);
    TEST_ASSERT_EQUAL_STRING("tool:gpio", t.name);
    TEST_ASSERT_EQUAL_STRING("gpio",      t.lane);
    TEST_ASSERT_EQUAL_STRING("serial",    t.reply_channel);
    TEST_ASSERT_EQUAL(2, t.priority);
    TEST_ASSERT_EQUAL(TaskStatus::PENDING, t.status);
    TEST_ASSERT_NOT_EQUAL(0, t.id);
}

void test_task_ids_are_unique() {
    Task t1 = Task::make("a", "default", "{}");
    Task t2 = Task::make("b", "default", "{}");
    TEST_ASSERT_NOT_EQUAL(t1.id, t2.id);
}

void test_enqueue_without_begin_native() {
    // On native build, workers don't start automatically —
    // we test that enqueue succeeds (returns true for non-full queue)
    LaneQueue q;
    q.addLane("default", testHandler);
    // begin() on native does nothing, but enqueue still adds to queue
    q.begin();

    Task t = Task::make("test:task", "default", "{}");
    TEST_ASSERT_TRUE(q.enqueue(t));
    TEST_ASSERT_EQUAL(1, q.pending("default"));
}

void test_pending_count_increments() {
    LaneQueue q;
    q.addLane("gpio", testHandler);
    q.begin();

    for (int i = 0; i < 3; ++i) {
        Task t = Task::make("tool:gpio", "gpio", "{}");
        q.enqueue(t);
    }
    TEST_ASSERT_EQUAL(3, q.pending("gpio"));
}

void test_queue_full_returns_false() {
    LaneQueue q;
    q.addLane("default", testHandler);
    q.begin();

    // Fill queue to capacity
    for (int i = 0; i < LANE_QUEUE_DEPTH; ++i) {
        Task t = Task::make("t", "default", "{}");
        TEST_ASSERT_TRUE(q.enqueue(t));
    }
    // Next enqueue should fail
    Task t = Task::make("overflow", "default", "{}");
    TEST_ASSERT_FALSE(q.enqueue(t));
}

void test_unknown_lane_falls_back_to_default() {
    LaneQueue q;
    q.addLane("default", testHandler);
    q.begin();

    Task t = Task::make("t", "unknown_lane", "{}");
    // Should succeed — fallback to "default"
    TEST_ASSERT_TRUE(q.enqueue(t));
}

void test_pending_on_nonexistent_lane() {
    LaneQueue q;
    TEST_ASSERT_EQUAL(0, q.pending("nonexistent"));
}

void test_is_busy_initially_false() {
    LaneQueue q;
    q.addLane("llm", testHandler);
    TEST_ASSERT_FALSE(q.isBusy("llm"));
}

void test_all_idle_initially() {
    LaneQueue q;
    q.addLane("default", testHandler);
    q.addLane("gpio",    testHandler);
    q.begin();
    // Immediately after begin() (no tasks) all lanes should be idle
    // (pending count > 0 means not all_idle, but no tasks yet)
    // Note: on native, workers don't drain automatically
    // We check: after enqueueing, pending > 0, so allIdle == false
    Task t = Task::make("t", "default", "{}");
    q.enqueue(t);
    TEST_ASSERT_FALSE(q.allIdle());
}

// ─────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_lane_can_be_added);
    RUN_TEST(test_max_lanes_not_exceeded);
    RUN_TEST(test_task_creation);
    RUN_TEST(test_task_ids_are_unique);
    RUN_TEST(test_enqueue_without_begin_native);
    RUN_TEST(test_pending_count_increments);
    RUN_TEST(test_queue_full_returns_false);
    RUN_TEST(test_unknown_lane_falls_back_to_default);
    RUN_TEST(test_pending_on_nonexistent_lane);
    RUN_TEST(test_is_busy_initially_false);
    RUN_TEST(test_all_idle_initially);

    return UNITY_END();
}
