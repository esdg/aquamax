#include <unity.h>
#include <limits>
#include "alerts/AlertEngine.h"

using namespace aquamax;
void setUp() {}
void tearDown() {}

void test_threshold_debounce_hysteresis_repeat_and_recovery() {
    AlertEngine engine;
    AlertConfig config;
    config.thresholdsEnabled = true;
    config.debounceMs = 10000;
    config.repeatMs = 60000;
    TEST_ASSERT_EQUAL(static_cast<int>(AlertState::normal), static_cast<int>(engine.update(0, ProbeState::connected, true, 19, config).state));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::low), static_cast<int>(engine.update(10000, ProbeState::connected, true, 19, config).event));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertState::low), static_cast<int>(engine.update(20000, ProbeState::connected, true, 20.4f, config).state));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::none), static_cast<int>(engine.update(69999, ProbeState::connected, true, 20.4f, config).event));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::low), static_cast<int>(engine.update(70000, ProbeState::connected, true, 20.4f, config).event));
    engine.update(71000, ProbeState::connected, true, 20.5f, config);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::recovered), static_cast<int>(engine.update(81000, ProbeState::connected, true, 20.5f, config).event));
}

void test_fault_suppresses_threshold_and_requires_real_recovery() {
    AlertEngine engine;
    AlertConfig config;
    config.thresholdsEnabled = true;
    config.debounceMs = 10;
    engine.update(0, ProbeState::connected, true, 31, config);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::high), static_cast<int>(engine.update(10, ProbeState::connected, true, 31, config).event));
    const auto missing = engine.update(11, ProbeState::disconnected, false, 0, config);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertState::normal), static_cast<int>(missing.state));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::none), static_cast<int>(missing.event));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::probeFault), static_cast<int>(engine.update(21, ProbeState::disconnected, false, 0, config).event));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertState::probeFault), static_cast<int>(engine.update(40, ProbeState::connected, false, 0, config).state));
    engine.update(41, ProbeState::connected, true, 25, config);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::recovered), static_cast<int>(engine.update(51, ProbeState::connected, true, 25, config).event));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertState::normal), static_cast<int>(engine.update(60, ProbeState::disabled, false, 0, config).state));
}

void test_defaults_never_raise_thresholds_and_reject_invalid_settings() {
    AlertEngine engine;
    AlertConfig config;
    config.debounceMs = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::none), static_cast<int>(engine.update(0, ProbeState::connected, true, 100, config).event));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::none), static_cast<int>(engine.update(1, ProbeState::disabled, false, 0, config).event));
    config.minimum = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(validAlertConfig(config));
    config = {};
    config.hysteresis = 6;
    TEST_ASSERT_FALSE(validAlertConfig(config));
}

void test_transient_excursions_and_millis_rollover() {
    AlertEngine engine;
    AlertConfig config;
    config.thresholdsEnabled = true;
    config.debounceMs = 100;
    engine.update(0, ProbeState::connected, true, 31, config);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertState::normal), static_cast<int>(engine.update(99, ProbeState::connected, true, 25, config).state));
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::none), static_cast<int>(engine.update(100, ProbeState::connected, true, 31, config).event));
    engine.reset();
    engine.update(UINT32_MAX - 50, ProbeState::connected, true, 19, config);
    TEST_ASSERT_EQUAL(static_cast<int>(AlertEvent::low), static_cast<int>(engine.update(49, ProbeState::connected, true, 19, config).event));
}

void test_probe_warnings_debounce_pause_recovery_and_disable() {
    ProbeWarningMonitor monitor;
    TEST_ASSERT_FALSE(monitor.update(0, ProbeState::signal_fault, false, 100));
    TEST_ASSERT_FALSE(monitor.update(50, ProbeState::disabled, true, 100));
    TEST_ASSERT_FALSE(monitor.update(150, ProbeState::disabled, true, 100));
    TEST_ASSERT_FALSE(monitor.update(150, ProbeState::signal_fault, false, 100));
    TEST_ASSERT_FALSE(monitor.update(199, ProbeState::signal_fault, false, 100));
    TEST_ASSERT_TRUE(monitor.update(200, ProbeState::signal_fault, false, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeWarning::signalFault), static_cast<int>(monitor.state()));
    TEST_ASSERT_FALSE(monitor.update(201, ProbeState::out_of_range, false, 100));
    TEST_ASSERT_TRUE(monitor.update(301, ProbeState::out_of_range, false, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeWarning::outOfRange), static_cast<int>(monitor.state()));
    TEST_ASSERT_FALSE(monitor.update(301, ProbeState::waiting, false, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeWarning::outOfRange), static_cast<int>(monitor.state()));
    TEST_ASSERT_TRUE(monitor.update(302, ProbeState::connected, false, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeWarning::normal), static_cast<int>(monitor.state()));
    TEST_ASSERT_FALSE(monitor.update(UINT32_MAX - 50, ProbeState::unstable, false, 100));
    TEST_ASSERT_TRUE(monitor.update(49, ProbeState::unstable, false, 100));
    TEST_ASSERT_TRUE(monitor.update(50, ProbeState::disabled, false, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeWarning::normal), static_cast<int>(monitor.state()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_threshold_debounce_hysteresis_repeat_and_recovery);
    RUN_TEST(test_fault_suppresses_threshold_and_requires_real_recovery);
    RUN_TEST(test_defaults_never_raise_thresholds_and_reject_invalid_settings);
    RUN_TEST(test_transient_excursions_and_millis_rollover);
    RUN_TEST(test_probe_warnings_debounce_pause_recovery_and_disable);
    return UNITY_END();
}