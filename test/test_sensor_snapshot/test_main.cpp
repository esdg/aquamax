#include <unity.h>
#include <limits>
#include "sensors/SensorSnapshot.h"

using aquamax::ProbeState;
using aquamax::SensorSnapshotBuilder;
using aquamax::TemperatureReading;

void setUp() {}
void tearDown() {}

void test_temperature_filter_deduplicates_and_analog_channels_are_disabled() {
    SensorSnapshotBuilder builder;
    TemperatureReading input{ProbeState::connected, true, 24.0f, 100};
    for (unsigned repeat = 0; repeat < 10; ++repeat) {
        const auto snapshot = builder.updateTemperature(input);
        TEST_ASSERT_FALSE(snapshot.temperature.hasValue);
        TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::disabled), static_cast<int>(snapshot.ph.state));
        TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::disabled), static_cast<int>(snapshot.tds.state));
        TEST_ASSERT_FALSE(snapshot.ph.hasValue);
        TEST_ASSERT_FALSE(snapshot.tds.hasValue);
    }
    for (unsigned index = 1; index < 5; ++index) {
        input.lastSeenMs += 750;
        input.valueC = index == 2 ? 80.0f : 24.0f;
        const auto snapshot = builder.updateTemperature(input);
        if (index < 4) TEST_ASSERT_FALSE(snapshot.temperature.hasValue);
        else {
            TEST_ASSERT_TRUE(snapshot.temperature.hasValue);
            TEST_ASSERT_FLOAT_WITHIN(0.001f, 24.0f, snapshot.temperature.valueC);
            TEST_ASSERT_EQUAL_UINT32(input.lastSeenMs, snapshot.temperature.lastSeenMs);
        }
    }
}

void test_failure_and_recovery_discard_old_readings() {
    SensorSnapshotBuilder builder;
    TemperatureReading input{ProbeState::connected, true, 20.0f, 100};
    for (unsigned index = 0; index < 5; ++index) {
        builder.updateTemperature(input);
        input.lastSeenMs += 750;
    }
    input.hasValue = false;
    auto snapshot = builder.updateTemperature(input);
    TEST_ASSERT_FALSE(snapshot.temperature.hasValue);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, snapshot.temperature.valueC);
    input.hasValue = true;
    input.valueC = 30.0f;
    for (unsigned index = 0; index < 5; ++index) {
        snapshot = builder.updateTemperature(input);
        if (index < 4) TEST_ASSERT_FALSE(snapshot.temperature.hasValue);
        input.lastSeenMs += 750;
    }
    TEST_ASSERT_TRUE(snapshot.temperature.hasValue);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, snapshot.temperature.valueC);
    input.valueC = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(builder.updateTemperature(input).temperature.hasValue);
    input.state = ProbeState::disconnected;
    TEST_ASSERT_FALSE(builder.updateTemperature(input).temperature.hasValue);
}

void test_temperature_offset_applies_after_filter_and_never_creates_a_reading() {
    SensorSnapshotBuilder builder;
    TemperatureReading input{ProbeState::connected, true, 20.0f, 100};
    for (unsigned index = 0; index < 5; ++index) {
        const auto snapshot = builder.updateTemperature(input, 1.5f);
        if (index < 4) TEST_ASSERT_FALSE(snapshot.temperature.hasValue);
        input.lastSeenMs += 750;
    }
    input.lastSeenMs -= 750;
    auto snapshot = builder.updateTemperature(input, -0.5f);
    TEST_ASSERT_TRUE(snapshot.temperature.hasValue);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, snapshot.temperature.uncalibratedC);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 19.5f, snapshot.temperature.valueC);
    input.state = ProbeState::disconnected;
    TEST_ASSERT_FALSE(builder.updateTemperature(input, 1.5f).temperature.hasValue);
}

void test_temperature_stability_requires_fresh_consistent_samples() {
    SensorSnapshotBuilder builder;
    TemperatureReading input{ProbeState::connected, true, 20.0f, 100};
    for (unsigned index = 0; index < 8; ++index) {
        const auto snapshot = builder.updateTemperature(input);
        TEST_ASSERT_EQUAL(index == 7, snapshot.temperature.stable);
        input.lastSeenMs += 750;
    }
    input.valueC = 21.0f;
    TEST_ASSERT_FALSE(builder.updateTemperature(input).temperature.stable);
    input.state = ProbeState::disconnected;
    TEST_ASSERT_FALSE(builder.updateTemperature(input).temperature.stable);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temperature_filter_deduplicates_and_analog_channels_are_disabled);
    RUN_TEST(test_failure_and_recovery_discard_old_readings);
    RUN_TEST(test_temperature_offset_applies_after_filter_and_never_creates_a_reading);
    RUN_TEST(test_temperature_stability_requires_fresh_consistent_samples);
    return UNITY_END();
}