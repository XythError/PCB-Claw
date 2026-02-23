// ─────────────────────────────────────────────────────────────────
// test_tool_registry.cpp — Unit tests for the ToolRegistry
// Runs on native platform (no hardware required).
// ─────────────────────────────────────────────────────────────────

#include <unity.h>
#include "../../src/tools/ToolRegistry.h"
#include "../../src/tools/Tool.h"
#include "../../src/tools/GpioTool.h"
#include "../../src/tools/HttpTool.h"
#include <string.h>

void setUp()    {}
void tearDown() {}

// ── Stub tool for testing ─────────────────────────────────────────

class EchoTool : public ITool {
public:
    explicit EchoTool(const char* n) { strncpy(_name, n, sizeof(_name)-1); }
    const char* name()        const override { return _name; }
    const char* description() const override { return "echo args back"; }
    const char* argSchema()   const override {
        return "{\"type\":\"object\",\"properties\":{}}";
    }
    bool execute(const char* argsJson, char* buf, size_t len) override {
        snprintf(buf, len, "{\"echo\":\"%s\"}", argsJson ? argsJson : "");
        return true;
    }
private:
    char _name[TOOL_NAME_LEN] = {};
};

class FailTool : public ITool {
public:
    const char* name()        const override { return "fail_tool"; }
    const char* description() const override { return "always fails"; }
    const char* argSchema()   const override { return "{}"; }
    bool execute(const char*, char* buf, size_t len) override {
        snprintf(buf, len, "{\"error\":\"intentional failure\"}");
        return false;
    }
};

// ── Fixtures ──────────────────────────────────────────────────────

static EchoTool echoA("echo_a");
static EchoTool echoB("echo_b");
static FailTool failTool;
static GpioTool gpioTool;
static HttpTool httpTool;

// ── Tests ─────────────────────────────────────────────────────────

void test_registry_add_and_find() {
    ToolRegistry reg;
    TEST_ASSERT_TRUE(reg.add(&echoA));
    ITool* found = reg.find("echo_a");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("echo_a", found->name());
}

void test_registry_find_missing() {
    ToolRegistry reg;
    reg.add(&echoA);
    TEST_ASSERT_NULL(reg.find("nonexistent"));
}

void test_registry_no_duplicates() {
    ToolRegistry reg;
    TEST_ASSERT_TRUE(reg.add(&echoA));
    TEST_ASSERT_FALSE(reg.add(&echoA));  // duplicate rejected
    TEST_ASSERT_EQUAL(1, reg.count());
}

void test_registry_count() {
    ToolRegistry reg;
    reg.add(&echoA);
    reg.add(&echoB);
    TEST_ASSERT_EQUAL(2, reg.count());
}

void test_invoke_success() {
    ToolRegistry reg;
    reg.add(&echoA);
    char result[256] = {};
    bool ok = reg.invoke("echo_a", "{}", result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "echo"));
}

void test_invoke_fail_tool() {
    ToolRegistry reg;
    reg.add(&failTool);
    char result[256] = {};
    bool ok = reg.invoke("fail_tool", "{}", result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
}

void test_invoke_missing_tool_returns_false() {
    ToolRegistry reg;
    char result[256] = {};
    bool ok = reg.invoke("ghost_tool", "{}", result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_list_tools_format() {
    ToolRegistry reg;
    reg.add(&echoA);
    reg.add(&echoB);
    char buf[512] = {};
    reg.listTools(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "echo_a"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "echo_b"));
}

void test_schemas_json_is_valid_array() {
    ToolRegistry reg;
    reg.add(&echoA);
    char buf[1024] = {};
    reg.schemasJson(buf, sizeof(buf));
    TEST_ASSERT_EQUAL('[', buf[0]);
    TEST_ASSERT_EQUAL(']', buf[strlen(buf)-1]);
    TEST_ASSERT_NOT_NULL(strstr(buf, "echo_a"));
}

void test_gpio_tool_name() {
    TEST_ASSERT_EQUAL_STRING("gpio", gpioTool.name());
}

void test_gpio_tool_execute_simulated() {
    char result[256] = {};
    bool ok = gpioTool.execute("{\"op\":\"read\",\"pin\":4}",
                                result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "simulated"));
}

void test_http_tool_name() {
    TEST_ASSERT_EQUAL_STRING("http", httpTool.name());
}

void test_http_tool_missing_url() {
    char result[256] = {};
    bool ok = httpTool.execute("{\"method\":\"GET\"}", result, sizeof(result));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "error"));
}

void test_http_tool_simulated_get() {
    char result[256] = {};
    bool ok = httpTool.execute(
        "{\"method\":\"GET\",\"url\":\"http://example.com\"}",
        result, sizeof(result));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_NOT_NULL(strstr(result, "simulated"));
}

// ─────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_registry_add_and_find);
    RUN_TEST(test_registry_find_missing);
    RUN_TEST(test_registry_no_duplicates);
    RUN_TEST(test_registry_count);
    RUN_TEST(test_invoke_success);
    RUN_TEST(test_invoke_fail_tool);
    RUN_TEST(test_invoke_missing_tool_returns_false);
    RUN_TEST(test_list_tools_format);
    RUN_TEST(test_schemas_json_is_valid_array);
    RUN_TEST(test_gpio_tool_name);
    RUN_TEST(test_gpio_tool_execute_simulated);
    RUN_TEST(test_http_tool_name);
    RUN_TEST(test_http_tool_missing_url);
    RUN_TEST(test_http_tool_simulated_get);

    return UNITY_END();
}
