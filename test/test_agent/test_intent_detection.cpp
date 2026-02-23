// ─────────────────────────────────────────────────────────────────
// test_intent_detection.cpp — Unit tests for IntentDetector,
//   TaskDecomposer, WorkflowEngine, and ConfigManager.
// Runs on native platform (no hardware required).
// ─────────────────────────────────────────────────────────────────

#include <unity.h>
#include "../../src/agent/IntentDetector.h"
#include "../../src/agent/TaskDecomposer.h"
#include "../../src/agent/WorkflowEngine.h"
#include "../../src/config/ConfigManager.h"
#include "../../src/tools/ToolRegistry.h"
#include "../../src/tools/GpioTool.h"
#include <string.h>

void setUp()    {}
void tearDown() {}

// ── IntentDetector tests ──────────────────────────────────────────

void test_intent_chat() {
    TEST_ASSERT_EQUAL(Intent::CHAT,
                      IntentDetector::detect("Hello, how are you?"));
}

void test_intent_command() {
    TEST_ASSERT_EQUAL(Intent::COMMAND,
                      IntentDetector::detect("/help"));
}

void test_intent_gpio() {
    TEST_ASSERT_EQUAL(Intent::GPIO_CONTROL,
                      IntentDetector::detect("Turn on the LED"));
    TEST_ASSERT_EQUAL(Intent::GPIO_CONTROL,
                      IntentDetector::detect("Set GPIO pin 4 high"));
    TEST_ASSERT_EQUAL(Intent::GPIO_CONTROL,
                      IntentDetector::detect("configure PWM on pin 5"));
}

void test_intent_i2c() {
    TEST_ASSERT_EQUAL(Intent::I2C_OP,
                      IntentDetector::detect("read sensor via i2c"));
    TEST_ASSERT_EQUAL(Intent::I2C_OP,
                      IntentDetector::detect("scan the I2C bus"));
}

void test_intent_spi() {
    TEST_ASSERT_EQUAL(Intent::SPI_OP,
                      IntentDetector::detect("write SPI flash"));
    TEST_ASSERT_EQUAL(Intent::SPI_OP,
                      IntentDetector::detect("send data via SPI"));
}

void test_intent_http() {
    TEST_ASSERT_EQUAL(Intent::HTTP_REQUEST,
                      IntentDetector::detect("fetch this URL for me"));
    TEST_ASSERT_EQUAL(Intent::HTTP_REQUEST,
                      IntentDetector::detect("make an API call"));
}

void test_intent_workflow() {
    TEST_ASSERT_EQUAL(Intent::WORKFLOW_RUN,
                      IntentDetector::detect("run workflow blink_led"));
    TEST_ASSERT_EQUAL(Intent::WORKFLOW_RUN,
                      IntentDetector::detect("execute the scan_i2c workflow"));
}

void test_intent_status() {
    TEST_ASSERT_EQUAL(Intent::STATUS_QUERY,
                      IntentDetector::detect("what is the status?"));
    TEST_ASSERT_EQUAL(Intent::STATUS_QUERY,
                      IntentDetector::detect("show memory usage"));
    TEST_ASSERT_EQUAL(Intent::STATUS_QUERY,
                      IntentDetector::detect("how long uptime?"));
}

void test_intent_case_insensitive() {
    TEST_ASSERT_EQUAL(Intent::GPIO_CONTROL,
                      IntentDetector::detect("SET THE LED TO HIGH"));
    TEST_ASSERT_EQUAL(Intent::I2C_OP,
                      IntentDetector::detect("SCAN I2C"));
}

// ── Message-based intent detection ───────────────────────────────

void test_message_intent_command() {
    Message m = Message::make("serial", "user", "/status");
    TEST_ASSERT_EQUAL(Intent::COMMAND, IntentDetector::detect(m));
}

void test_message_intent_chat() {
    Message m = Message::make("serial", "user", "Tell me a joke");
    TEST_ASSERT_EQUAL(Intent::CHAT, IntentDetector::detect(m));
}

// ── TaskDecomposer tests ──────────────────────────────────────────

void test_decompose_chat_produces_llm_task() {
    Message m = Message::make("serial", "user", "Hello world");
    auto tasks = TaskDecomposer::decompose(m, Intent::CHAT);
    TEST_ASSERT_EQUAL(1, tasks.count);
    TEST_ASSERT_EQUAL_STRING("llm:reason", tasks.tasks[0].name);
    TEST_ASSERT_EQUAL_STRING("llm", tasks.tasks[0].lane);
}

void test_decompose_gpio_produces_gpio_task() {
    Message m = Message::make("serial", "user", "Turn on LED");
    auto tasks = TaskDecomposer::decompose(m, Intent::GPIO_CONTROL);
    TEST_ASSERT_EQUAL(1, tasks.count);
    TEST_ASSERT_EQUAL_STRING("tool:direct", tasks.tasks[0].name);
    TEST_ASSERT_EQUAL_STRING("gpio", tasks.tasks[0].lane);
}

void test_decompose_command_produces_command_task() {
    Message m = Message::make("serial", "user", "/help");
    auto tasks = TaskDecomposer::decompose(m, Intent::COMMAND);
    TEST_ASSERT_EQUAL(1, tasks.count);
    TEST_ASSERT_EQUAL_STRING("command:exec", tasks.tasks[0].name);
}

void test_decompose_workflow_produces_workflow_task() {
    Message m = Message::make("serial", "user", "run workflow blink_led");
    auto tasks = TaskDecomposer::decompose(m, Intent::WORKFLOW_RUN);
    TEST_ASSERT_EQUAL(1, tasks.count);
    TEST_ASSERT_EQUAL_STRING("workflow:run", tasks.tasks[0].name);
    TEST_ASSERT_NOT_NULL(strstr(tasks.tasks[0].payload, "blink_led"));
}

void test_decompose_origin_msg_set() {
    Message m = Message::make("serial", "user", "hello");
    auto tasks = TaskDecomposer::decompose(m, Intent::CHAT);
    TEST_ASSERT_EQUAL_STRING(m.id, tasks.tasks[0].origin_msg);
}

// ── WorkflowEngine tests ──────────────────────────────────────────

void test_workflow_parse_simple() {
    const char* md =
        "# blink\n"
        "## steps\n"
        "- tool: gpio\n"
        "  args: {\"op\":\"write\",\"pin\":2,\"value\":1}\n"
        "  lane: gpio\n";
    Workflow wf = WorkflowEngine::parse(md);
    TEST_ASSERT_EQUAL_STRING("blink", wf.name);
    TEST_ASSERT_TRUE(wf.valid);
    TEST_ASSERT_EQUAL(1, wf.stepCount);
    TEST_ASSERT_EQUAL_STRING("gpio", wf.steps[0].tool);
    TEST_ASSERT_EQUAL_STRING("gpio", wf.steps[0].lane);
}

void test_workflow_parse_multi_step() {
    const char* md =
        "# multi\n"
        "## steps\n"
        "- tool: gpio\n"
        "  args: {\"op\":\"pin_mode\",\"pin\":2,\"mode\":\"OUTPUT\"}\n"
        "- tool: gpio\n"
        "  args: {\"op\":\"write\",\"pin\":2,\"value\":1}\n"
        "- tool: gpio\n"
        "  args: {\"op\":\"write\",\"pin\":2,\"value\":0}\n";
    Workflow wf = WorkflowEngine::parse(md);
    TEST_ASSERT_EQUAL(3, wf.stepCount);
    TEST_ASSERT_TRUE(wf.valid);
}

void test_workflow_serialize_roundtrip() {
    const char* md =
        "# ping\n"
        "## steps\n"
        "- tool: http\n"
        "  args: {\"method\":\"GET\",\"url\":\"http://example.com\"}\n"
        "  lane: http\n";
    Workflow wf = WorkflowEngine::parse(md);
    char serialized[1024] = {};
    WorkflowEngine::serialize(wf, serialized, sizeof(serialized));
    // Re-parse serialized form
    Workflow wf2 = WorkflowEngine::parse(serialized);
    TEST_ASSERT_EQUAL_STRING("ping", wf2.name);
    TEST_ASSERT_EQUAL(1, wf2.stepCount);
}

void test_workflow_register_and_find() {
    GpioTool gt;
    ToolRegistry reg;
    reg.add(&gt);
    WorkflowEngine wfe(reg);

    Workflow wf = WorkflowEngine::parse(
        "# test_wf\n"
        "## steps\n"
        "- tool: gpio\n"
        "  args: {\"op\":\"read\",\"pin\":4}\n");
    TEST_ASSERT_TRUE(wfe.registerWorkflow(wf));
    TEST_ASSERT_NOT_NULL(wfe.find("test_wf"));
    TEST_ASSERT_NULL(wfe.find("nonexistent"));
}

void test_workflow_run_success() {
    GpioTool gt;
    ToolRegistry reg;
    reg.add(&gt);
    WorkflowEngine wfe(reg);

    Workflow wf = WorkflowEngine::parse(
        "# run_me\n"
        "## steps\n"
        "- tool: gpio\n"
        "  args: {\"op\":\"read\",\"pin\":4}\n");
    wfe.registerWorkflow(wf);

    WorkflowResult res = wfe.run("run_me");
    TEST_ASSERT_TRUE(res.ok);
}

void test_workflow_run_missing_workflow() {
    GpioTool gt;
    ToolRegistry reg;
    reg.add(&gt);
    WorkflowEngine wfe(reg);

    WorkflowResult res = wfe.run("does_not_exist");
    TEST_ASSERT_FALSE(res.ok);
    TEST_ASSERT_NOT_NULL(strstr(res.error, "not found"));
}

// ── ConfigManager tests ───────────────────────────────────────────

void test_config_set_get() {
    ConfigManager cfg;
    cfg.set("my_key", "my_value");
    TEST_ASSERT_EQUAL_STRING("my_value", cfg.get("my_key"));
}

void test_config_get_default() {
    ConfigManager cfg;
    TEST_ASSERT_EQUAL_STRING("fallback",
                             cfg.get("nonexistent", "fallback"));
}

void test_config_get_int() {
    ConfigManager cfg;
    cfg.set("port", "8080");
    TEST_ASSERT_EQUAL(8080, cfg.getInt("port"));
}

void test_config_get_float() {
    ConfigManager cfg;
    cfg.set("temp", "0.7");
    float v = cfg.getFloat("temp");
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.7f, v);
}

void test_config_update_existing() {
    ConfigManager cfg;
    cfg.set("key", "old");
    cfg.set("key", "new");
    TEST_ASSERT_EQUAL_STRING("new", cfg.get("key"));
    TEST_ASSERT_EQUAL(1, cfg.count());  // no duplicate
}

void test_config_parse_line() {
    // Test internal parsing via public load() from a simulated string
    // We use set() directly for the native test
    ConfigManager cfg;
    cfg.set("llm_provider", "openai");
    cfg.set("max_tokens", "512");
    TEST_ASSERT_EQUAL_STRING("openai", cfg.get("llm_provider"));
    TEST_ASSERT_EQUAL(512, cfg.getInt("max_tokens"));
}

// ── Message tests ─────────────────────────────────────────────────

void test_message_make() {
    Message m = Message::make("telegram", "user123", "hello");
    TEST_ASSERT_EQUAL_STRING("telegram", m.channel_id);
    TEST_ASSERT_EQUAL_STRING("user123",  m.sender_id);
    TEST_ASSERT_EQUAL_STRING("hello",    m.content);
    TEST_ASSERT_EQUAL(MessageType::TEXT, m.type);
    TEST_ASSERT_NOT_EQUAL('\0', m.id[0]);
}

void test_message_is_command() {
    Message m = Message::make("serial", "user", "/help");
    TEST_ASSERT_TRUE(m.isCommand());
}

void test_message_is_not_command() {
    Message m = Message::make("serial", "user", "hello");
    TEST_ASSERT_FALSE(m.isCommand());
}

void test_message_command_name() {
    Message m = Message::make("serial", "user", "/status now");
    char name[16] = {};
    m.commandName(name, sizeof(name));
    TEST_ASSERT_EQUAL_STRING("status", name);
}

void test_message_make_response() {
    Message req  = Message::make("telegram:123", "user", "hi");
    Message resp = Message::makeResponse(req, "response text");
    TEST_ASSERT_EQUAL(MessageType::RESPONSE, resp.type);
    TEST_ASSERT_EQUAL_STRING("response text", resp.content);
    TEST_ASSERT_FALSE(resp.requires_reply);
}

// ─────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();

    // IntentDetector
    RUN_TEST(test_intent_chat);
    RUN_TEST(test_intent_command);
    RUN_TEST(test_intent_gpio);
    RUN_TEST(test_intent_i2c);
    RUN_TEST(test_intent_spi);
    RUN_TEST(test_intent_http);
    RUN_TEST(test_intent_workflow);
    RUN_TEST(test_intent_status);
    RUN_TEST(test_intent_case_insensitive);
    RUN_TEST(test_message_intent_command);
    RUN_TEST(test_message_intent_chat);

    // TaskDecomposer
    RUN_TEST(test_decompose_chat_produces_llm_task);
    RUN_TEST(test_decompose_gpio_produces_gpio_task);
    RUN_TEST(test_decompose_command_produces_command_task);
    RUN_TEST(test_decompose_workflow_produces_workflow_task);
    RUN_TEST(test_decompose_origin_msg_set);

    // WorkflowEngine
    RUN_TEST(test_workflow_parse_simple);
    RUN_TEST(test_workflow_parse_multi_step);
    RUN_TEST(test_workflow_serialize_roundtrip);
    RUN_TEST(test_workflow_register_and_find);
    RUN_TEST(test_workflow_run_success);
    RUN_TEST(test_workflow_run_missing_workflow);

    // ConfigManager
    RUN_TEST(test_config_set_get);
    RUN_TEST(test_config_get_default);
    RUN_TEST(test_config_get_int);
    RUN_TEST(test_config_get_float);
    RUN_TEST(test_config_update_existing);
    RUN_TEST(test_config_parse_line);

    // Message
    RUN_TEST(test_message_make);
    RUN_TEST(test_message_is_command);
    RUN_TEST(test_message_is_not_command);
    RUN_TEST(test_message_command_name);
    RUN_TEST(test_message_make_response);

    return UNITY_END();
}
