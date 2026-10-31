#include <unity.h>
#include "hardware/TemperatureProbe.h"
#include "sensors/SensorSnapshot.h"

void setUp() { OneWire::resetDevice(); }
void tearDown() {}

void test_temperature_probe_disconnect_and_recovery() {
    aquamax::TemperatureProbe probe(10);
    TEST_ASSERT_FALSE(probe.update(0).hasValue);
    TEST_ASSERT_FALSE(probe.update(750).hasValue);
    probe.update(751);
    TEST_ASSERT_FALSE(probe.update(1501).hasValue);
    probe.update(1502);
    auto connected = probe.update(2252);
    TEST_ASSERT_TRUE(connected.hasValue);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::connected), static_cast<int>(connected.state));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, connected.valueC);

    OneWire::present = false;
    TEST_ASSERT_FALSE(probe.update(2253).hasValue);
    TEST_ASSERT_FALSE(probe.update(5001).hasValue);
    auto disconnected = probe.update(10001);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::disconnected), static_cast<int>(disconnected.state));

    OneWire::present = true;
    probe.update(15001);
    TEST_ASSERT_FALSE(probe.update(15751).hasValue);
    probe.update(15752);
    TEST_ASSERT_FALSE(probe.update(16502).hasValue);
    probe.update(16503);
    TEST_ASSERT_TRUE(probe.update(17253).hasValue);
}

void test_bad_crc_never_exposes_temperature() {
    aquamax::TemperatureProbe probe(10);
    probe.update(0);
    OneWire::scratchpad[8] ^= 1u;
    const auto sample = probe.update(750);
    TEST_ASSERT_FALSE(sample.hasValue);
}

void test_filtered_snapshot_with_real_driver_disconnect_and_recovery() {
    aquamax::TemperatureProbe probe(10);
    aquamax::SensorSnapshotBuilder builder;
    auto update = [&](uint32_t now) {
        const auto sample = probe.update(now);
        return builder.updateTemperature({sample.state, sample.hasValue, sample.valueC, sample.lastSeenMs});
    };
    aquamax::SensorSnapshot snapshot;
    for (uint32_t now = 0; now <= 7000; now += 25) snapshot = update(now);
    TEST_ASSERT_TRUE(snapshot.temperature.hasValue);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, snapshot.temperature.valueC);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::disabled), static_cast<int>(snapshot.ph.state));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::disabled), static_cast<int>(snapshot.tds.state));
    OneWire::present = false;
    for (uint32_t now = 7025; now <= 8000; now += 25) snapshot = update(now);
    TEST_ASSERT_FALSE(snapshot.temperature.hasValue);
    OneWire::present = true;
    bool reconnected = false;
    for (uint32_t now = 8025; now <= 20000; now += 25) {
        snapshot = update(now);
        if (snapshot.temperature.hasValue) reconnected = true;
    }
    TEST_ASSERT_TRUE(reconnected);
    TEST_ASSERT_TRUE(snapshot.temperature.hasValue);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, snapshot.temperature.valueC);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temperature_probe_disconnect_and_recovery);
    RUN_TEST(test_bad_crc_never_exposes_temperature);
    RUN_TEST(test_filtered_snapshot_with_real_driver_disconnect_and_recovery);
    return UNITY_END();
}