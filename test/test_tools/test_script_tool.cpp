// ─────────────────────────────────────────────────────────────────
// test_script_tool.cpp — Unit tests for ScriptEngine, ScriptTool,
//   RamVfs, and PsramAllocator.
// Runs on native platform (no hardware required).
// ─────────────────────────────────────────────────────────────────

#include <unity.h>
#include "../../src/scripting/ScriptEngine.h"
#include "../../src/tools/ScriptTool.h"
#include "../../src/fs/RamVfs.h"
#include "../../src/memory/PsramAllocator.h"
#include <string.h>

void setUp()    {}
void tearDown() {}

// ── ScriptEngine tests ────────────────────────────────────────────

void test_script_engine_execute_stub_succeeds() {
    ScriptEngine eng;
    ScriptResult res = eng.execute("print('hello')");
    TEST_ASSERT_TRUE(res.ok);
    TEST_ASSERT_NOT_NULL(strstr(res.output, "stub"));
}

void test_script_engine_execute_empty_fails() {
    ScriptEngine eng;
    ScriptResult res = eng.execute("");
    TEST_ASSERT_FALSE(res.ok);
    TEST_ASSERT_NOT_NULL(strstr(res.error, "empty"));
}

void test_script_engine_execute_null_fails() {
    ScriptEngine eng;
    ScriptResult res = eng.execute(nullptr);
    TEST_ASSERT_FALSE(res.ok);
    TEST_ASSERT_NOT_NULL(strstr(res.error, "empty"));
}

void test_script_engine_execute_invalid_character_fails() {
    ScriptEngine eng;
    // Backtick is flagged as unsupported in stub mode
    ScriptResult res = eng.execute("local x = `echo`");
    TEST_ASSERT_FALSE(res.ok);
    TEST_ASSERT_NOT_NULL(strstr(res.error, "stub"));
}

void test_script_engine_lua_available_false_without_define() {
    // Without PCBCLAW_LUA_ENABLED, luaAvailable() must return false.
    TEST_ASSERT_FALSE(ScriptEngine::luaAvailable());
}

void test_script_engine_execute_file_stub() {
    ScriptEngine eng;
    ScriptResult res = eng.executeFile("/scripts/test.lua");
    TEST_ASSERT_TRUE(res.ok);
    TEST_ASSERT_NOT_NULL(strstr(res.output, "stub"));
}

// ── ScriptTool tests ──────────────────────────────────────────────

void test_script_tool_name() {
    ScriptTool st;
    TEST_ASSERT_EQUAL_STRING("script", st.name());
}

void test_script_tool_has_description() {
    ScriptTool st;
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(st.description()));
}

void test_script_tool_exec_succeeds() {
    ScriptTool st;
    char result[256] = {};
    bool ok = st.execute("{\"op\":\"exec\",\"code\":\"print(42)\"}",
                          result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "\"ok\":true"));
}

void test_script_tool_exec_empty_code() {
    ScriptTool st;
    char result[256] = {};
    bool ok = st.execute("{\"op\":\"exec\",\"code\":\"\"}",
                          result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_script_tool_unknown_op_fails() {
    ScriptTool st;
    char result[256] = {};
    bool ok = st.execute("{\"op\":\"compile\"}", result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_script_tool_load_op_stub() {
    ScriptTool st;
    char result[256] = {};
    bool ok = st.execute("{\"op\":\"load\",\"path\":\"/scripts/x.lua\"}",
                          result, sizeof(result));
    // On native stub, executeFile always returns ok=true
    TEST_ASSERT_TRUE(ok);
}

// ── RamVfs tests ──────────────────────────────────────────────────

void test_ramvfs_write_and_read() {
    RamVfs vfs;
    TEST_ASSERT_TRUE(vfs.write("/tmp/test.md", "hello world"));
    char buf[64] = {};
    size_t n = vfs.read("/tmp/test.md", buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_EQUAL_STRING("hello world", buf);
}

void test_ramvfs_append() {
    RamVfs vfs;
    vfs.write("/tmp/a.md", "line1");
    vfs.append("/tmp/a.md", " line2");
    char buf[64] = {};
    vfs.read("/tmp/a.md", buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "line1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "line2"));
}

void test_ramvfs_exists() {
    RamVfs vfs;
    TEST_ASSERT_FALSE(vfs.exists("/tmp/nonexistent.md"));
    vfs.write("/tmp/exists.md", "data");
    TEST_ASSERT_TRUE(vfs.exists("/tmp/exists.md"));
}

void test_ramvfs_discard() {
    RamVfs vfs;
    vfs.write("/tmp/discard.md", "data");
    vfs.discard("/tmp/discard.md");
    TEST_ASSERT_FALSE(vfs.exists("/tmp/discard.md"));
}

void test_ramvfs_file_count() {
    RamVfs vfs;
    TEST_ASSERT_EQUAL(0, vfs.fileCount());
    vfs.write("/tmp/f1.md", "a");
    vfs.write("/tmp/f2.md", "b");
    TEST_ASSERT_EQUAL(2, vfs.fileCount());
}

void test_ramvfs_overwrite() {
    RamVfs vfs;
    vfs.write("/tmp/ow.md", "old");
    vfs.write("/tmp/ow.md", "new");
    char buf[16] = {};
    vfs.read("/tmp/ow.md", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("new", buf);
    TEST_ASSERT_EQUAL(1, vfs.fileCount());  // no duplicate slot
}

void test_ramvfs_commit_clears_dirty() {
    RamVfs vfs;
    vfs.write("/tmp/commit.md", "persistent");
    // On native, commit() skips the LittleFS write but must return true
    TEST_ASSERT_TRUE(vfs.commit("/tmp/commit.md"));
}

void test_ramvfs_read_missing_returns_zero() {
    RamVfs vfs;
    char buf[16] = {};
    size_t n = vfs.read("/tmp/missing.md", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(0, (int)n);
}

void test_ramvfs_max_slots() {
    RamVfs vfs;
    // Fill all slots
    for (int i = 0; i < RAM_VFS_MAX_FILES; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/tmp/%d.md", i);
        TEST_ASSERT_TRUE(vfs.write(path, "x"));
    }
    TEST_ASSERT_EQUAL(RAM_VFS_MAX_FILES, vfs.fileCount());
    // One more slot should fail (returns false)
    TEST_ASSERT_FALSE(vfs.write("/tmp/overflow.md", "x"));
}

// ── PsramAllocator tests ──────────────────────────────────────────

void test_psram_alloc_and_free() {
    TEST_ASSERT_FALSE(PsramAllocator::isBusy(PsramPool::WORKSPACE));
    void* p = PsramAllocator::alloc(PsramPool::WORKSPACE);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(PsramAllocator::isBusy(PsramPool::WORKSPACE));
    PsramAllocator::free(PsramPool::WORKSPACE, p);
    TEST_ASSERT_FALSE(PsramAllocator::isBusy(PsramPool::WORKSPACE));
}

void test_psram_double_alloc_fails() {
    void* p1 = PsramAllocator::alloc(PsramPool::LLM_CTX);
    TEST_ASSERT_NOT_NULL(p1);
    void* p2 = PsramAllocator::alloc(PsramPool::LLM_CTX);
    TEST_ASSERT_NULL(p2);  // pool already held
    PsramAllocator::free(PsramPool::LLM_CTX, p1);
}

void test_psram_wrong_pointer_does_not_corrupt() {
    void* p = PsramAllocator::alloc(PsramPool::SCRIPT);
    TEST_ASSERT_NOT_NULL(p);
    // Freeing with a wrong pointer must not corrupt state
    PsramAllocator::free(PsramPool::SCRIPT, (void*)0x1234);
    TEST_ASSERT_TRUE(PsramAllocator::isBusy(PsramPool::SCRIPT));
    PsramAllocator::free(PsramPool::SCRIPT, p);
    TEST_ASSERT_FALSE(PsramAllocator::isBusy(PsramPool::SCRIPT));
}

void test_psram_stats_json() {
    char buf[256] = {};
    PsramAllocator::statsJson(buf, sizeof(buf));
    TEST_ASSERT_EQUAL('{', buf[0]);
    TEST_ASSERT_NOT_NULL(strstr(buf, "llm_ctx"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "workspace"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "script"));
}

// ─────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // ScriptEngine
    RUN_TEST(test_script_engine_execute_stub_succeeds);
    RUN_TEST(test_script_engine_execute_empty_fails);
    RUN_TEST(test_script_engine_execute_null_fails);
    RUN_TEST(test_script_engine_execute_invalid_character_fails);
    RUN_TEST(test_script_engine_lua_available_false_without_define);
    RUN_TEST(test_script_engine_execute_file_stub);

    // ScriptTool
    RUN_TEST(test_script_tool_name);
    RUN_TEST(test_script_tool_has_description);
    RUN_TEST(test_script_tool_exec_succeeds);
    RUN_TEST(test_script_tool_exec_empty_code);
    RUN_TEST(test_script_tool_unknown_op_fails);
    RUN_TEST(test_script_tool_load_op_stub);

    // RamVfs
    RUN_TEST(test_ramvfs_write_and_read);
    RUN_TEST(test_ramvfs_append);
    RUN_TEST(test_ramvfs_exists);
    RUN_TEST(test_ramvfs_discard);
    RUN_TEST(test_ramvfs_file_count);
    RUN_TEST(test_ramvfs_overwrite);
    RUN_TEST(test_ramvfs_commit_clears_dirty);
    RUN_TEST(test_ramvfs_read_missing_returns_zero);
    RUN_TEST(test_ramvfs_max_slots);

    // PsramAllocator
    RUN_TEST(test_psram_alloc_and_free);
    RUN_TEST(test_psram_double_alloc_fails);
    RUN_TEST(test_psram_wrong_pointer_does_not_corrupt);
    RUN_TEST(test_psram_stats_json);

    return UNITY_END();
}
