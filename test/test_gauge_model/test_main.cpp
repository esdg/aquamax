#include <unity.h>
#include <cmath>
#include <initializer_list>
#include <limits>
#include "display/GaugeModel.h"
#include "../../src/generated/icons.h"

using aquamax::GaugeModel;
using aquamax::ProbeState;

void setUp() {}
void tearDown() {}

void test_cursor_tracks_configured_range_smoothly() {
    GaugeModel gauge({10.0f, 30.0f}, 1.0f);
    auto frame = gauge.update(ProbeState::connected, true, 10.0f, false, 1000);
    TEST_ASSERT_TRUE(frame.showCursor);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, frame.cursorPosition);
    frame = gauge.update(ProbeState::connected, true, 30.0f, true, 2000);
    TEST_ASSERT_TRUE(frame.alert);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f - std::exp(-1.0f), frame.cursorPosition);
    TEST_ASSERT_TRUE(gauge.setRange({0.0f, 100.0f}));
    frame = gauge.update(ProbeState::connected, true, 200.0f, false, 2001);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, frame.cursorPosition);
}

void test_faults_hide_values_and_recovery_discards_old_cursor() {
    GaugeModel gauge({0.0f, 40.0f});
    gauge.update(ProbeState::connected, true, 20.0f, false, 0);
    for (ProbeState state : {ProbeState::disconnected, ProbeState::unstable, ProbeState::out_of_range, ProbeState::disabled}) {
        const auto frame = gauge.update(state, true, 25.0f, true, 1);
        TEST_ASSERT_FALSE(frame.hasValue);
        TEST_ASSERT_FALSE(frame.showCursor);
        TEST_ASSERT_FALSE(frame.alert);
    }
    TEST_ASSERT_FALSE(gauge.update(ProbeState::connected, false, 25.0f, true, 2).hasValue);
    TEST_ASSERT_FALSE(gauge.update(ProbeState::connected, true, std::numeric_limits<float>::quiet_NaN(), true, 3).hasValue);
    const auto recovered = gauge.update(ProbeState::connected, true, 30.0f, false, 4);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, recovered.cursorPosition);
}

void test_invalid_ranges_preserve_previous_range_and_clock_wrap() {
    GaugeModel gauge({0.0f, 10.0f}, 1.0f);
    TEST_ASSERT_FALSE(gauge.setRange({1.0f, 1.0f}));
    TEST_ASSERT_FALSE(gauge.setRange({0.0f, std::numeric_limits<float>::infinity()}));
    gauge.update(ProbeState::connected, true, 0.0f, false, UINT32_MAX - 499);
    const auto frame = gauge.update(ProbeState::connected, true, 10.0f, false, 500);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f - std::exp(-1.0f), frame.cursorPosition);
    GaugeModel invalid({10.0f, 0.0f});
    TEST_ASSERT_FALSE(invalid.update(ProbeState::connected, true, 5.0f, false, 0).hasValue);
}

void test_generated_icons_are_cpp_arrays() {
    TEST_ASSERT_EQUAL_UINT32(32 * 32, sizeof(aquamax::icons::thermometer_32));
    TEST_ASSERT_EQUAL_UINT32(48 * 48, sizeof(aquamax::icons::thermometer_off_48));
    TEST_ASSERT_EQUAL_UINT32(32 * 32, sizeof(aquamax::icons::drop_off_32));
    TEST_ASSERT_EQUAL_UINT32(48 * 48, sizeof(aquamax::icons::three_drops_off_48));
    TEST_ASSERT_EQUAL_UINT32(48 * 48, sizeof(aquamax::icons::warning_48));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_cursor_tracks_configured_range_smoothly);
    RUN_TEST(test_faults_hide_values_and_recovery_discards_old_cursor);
    RUN_TEST(test_invalid_ranges_preserve_previous_range_and_clock_wrap);
    RUN_TEST(test_generated_icons_are_cpp_arrays);
    return UNITY_END();
}