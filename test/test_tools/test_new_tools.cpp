// ─────────────────────────────────────────────────────────────────
// test_new_tools.cpp — Unit tests for FileTool, MessageTool, SpawnTool.
// Runs on native platform (no hardware required).
// ─────────────────────────────────────────────────────────────────

#include <unity.h>
#include "../../src/tools/FileTool.h"
#include "../../src/tools/MessageTool.h"
#include "../../src/tools/SpawnTool.h"
#include <string.h>

void setUp()    {}
void tearDown() {}

// ── FileTool tests ────────────────────────────────────────────────

static FileTool fileTool;

void test_file_tool_name() {
    TEST_ASSERT_EQUAL_STRING("file", fileTool.name());
}

void test_file_tool_has_description() {
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(fileTool.description()));
}

void test_file_tool_schema_is_object() {
    const char* s = fileTool.argSchema();
    TEST_ASSERT_NOT_NULL(strstr(s, "\"type\":\"object\""));
}

void test_file_tool_read_simulated() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"read\",\"path\":\"/workspace/test.txt\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(result, "simulated"));
}

void test_file_tool_write_simulated() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"write\",\"path\":\"/workspace/test.txt\","
        "\"content\":\"hello world\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
}

void test_file_tool_append_simulated() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"append\",\"path\":\"/workspace/test.txt\","
        "\"content\":\"more text\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
}

void test_file_tool_edit_simulated() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"edit\",\"path\":\"/workspace/test.txt\","
        "\"old\":\"hello\",\"new\":\"goodbye\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
}

void test_file_tool_list_simulated() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"list\",\"path\":\"/workspace/\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(result, "entries"));
}

void test_file_tool_missing_path_fails() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"read\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_file_tool_unknown_op_fails() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"delete\",\"path\":\"/workspace/x.txt\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_file_tool_edit_missing_old_fails() {
    char result[256] = {};
    bool ok = fileTool.execute(
        "{\"op\":\"edit\",\"path\":\"/workspace/x.txt\","
        "\"new\":\"replacement\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

// ── MessageTool tests ─────────────────────────────────────────────

static MessageTool messageTool;  // null gateway — simulated on native

void test_message_tool_name() {
    TEST_ASSERT_EQUAL_STRING("message", messageTool.name());
}

void test_message_tool_has_description() {
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(messageTool.description()));
}

void test_message_tool_schema_has_op() {
    TEST_ASSERT_NOT_NULL(strstr(messageTool.argSchema(), "\"op\""));
}

void test_message_tool_send_simulated() {
    char result[256] = {};
    bool ok = messageTool.execute(
        "{\"op\":\"send\",\"channel\":\"serial\",\"text\":\"Hello\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(result, "simulated"));
}

void test_message_tool_broadcast_simulated() {
    char result[256] = {};
    bool ok = messageTool.execute(
        "{\"op\":\"broadcast\",\"text\":\"Alert\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
}

void test_message_tool_missing_text_fails() {
    char result[256] = {};
    bool ok = messageTool.execute(
        "{\"op\":\"send\",\"channel\":\"serial\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_message_tool_unknown_op_simulated() {
    // On native, unknown ops still return simulated=true
    char result[256] = {};
    messageTool.execute(
        "{\"op\":\"unknown\",\"text\":\"test\"}",
        result, sizeof(result));
    // Just check no crash — result may be ok or error depending on platform
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(result));
}

// ── SpawnTool tests ───────────────────────────────────────────────

static SpawnTool spawnTool;  // null queue — simulated on native

void test_spawn_tool_name() {
    TEST_ASSERT_EQUAL_STRING("spawn", spawnTool.name());
}

void test_spawn_tool_has_description() {
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(spawnTool.description()));
}

void test_spawn_tool_schema_has_lane() {
    TEST_ASSERT_NOT_NULL(strstr(spawnTool.argSchema(), "\"lane\""));
}

void test_spawn_tool_spawn_simulated() {
    char result[256] = {};
    bool ok = spawnTool.execute(
        "{\"op\":\"spawn\",\"task\":\"tool:gpio\","
        "\"lane\":\"gpio\",\"payload\":\"{}\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(result, "simulated"));
}

void test_spawn_tool_spawn_with_priority() {
    char result[256] = {};
    bool ok = spawnTool.execute(
        "{\"op\":\"spawn\",\"task\":\"tool:i2c\","
        "\"lane\":\"hardware\",\"payload\":\"{\\\"op\\\":\\\"scan\\\"}\","
        "\"priority\":2}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
}

void test_spawn_tool_missing_task_fails() {
    char result[256] = {};
    bool ok = spawnTool.execute(
        "{\"op\":\"spawn\",\"lane\":\"gpio\",\"payload\":\"{}\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_spawn_tool_missing_lane_fails() {
    char result[256] = {};
    bool ok = spawnTool.execute(
        "{\"op\":\"spawn\",\"task\":\"tool:gpio\",\"payload\":\"{}\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_spawn_tool_unknown_op_fails() {
    char result[256] = {};
    bool ok = spawnTool.execute(
        "{\"op\":\"kill\",\"task\":\"tool:gpio\","
        "\"lane\":\"gpio\",\"payload\":\"{}\"}",
        result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

// ─────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // FileTool
    RUN_TEST(test_file_tool_name);
    RUN_TEST(test_file_tool_has_description);
    RUN_TEST(test_file_tool_schema_is_object);
    RUN_TEST(test_file_tool_read_simulated);
    RUN_TEST(test_file_tool_write_simulated);
    RUN_TEST(test_file_tool_append_simulated);
    RUN_TEST(test_file_tool_edit_simulated);
    RUN_TEST(test_file_tool_list_simulated);
    RUN_TEST(test_file_tool_missing_path_fails);
    RUN_TEST(test_file_tool_unknown_op_fails);
    RUN_TEST(test_file_tool_edit_missing_old_fails);

    // MessageTool
    RUN_TEST(test_message_tool_name);
    RUN_TEST(test_message_tool_has_description);
    RUN_TEST(test_message_tool_schema_has_op);
    RUN_TEST(test_message_tool_send_simulated);
    RUN_TEST(test_message_tool_broadcast_simulated);
    RUN_TEST(test_message_tool_missing_text_fails);
    RUN_TEST(test_message_tool_unknown_op_simulated);

    // SpawnTool
    RUN_TEST(test_spawn_tool_name);
    RUN_TEST(test_spawn_tool_has_description);
    RUN_TEST(test_spawn_tool_schema_has_lane);
    RUN_TEST(test_spawn_tool_spawn_simulated);
    RUN_TEST(test_spawn_tool_spawn_with_priority);
    RUN_TEST(test_spawn_tool_missing_task_fails);
    RUN_TEST(test_spawn_tool_missing_lane_fails);
    RUN_TEST(test_spawn_tool_unknown_op_fails);

    return UNITY_END();
}
