#include <unity.h>
#include <limits>
#include <Preferences.h>
#include "calibration/PhCalibration.h"
#include "calibration/Compensation.h"
#include "settings/TdsCalibrationStore.h"
#include "calibration/PhCalibrationRecord.h"
#include "sensors/PhChannel.h"
#include "settings/PhCalibrationStore.h"

void setUp() { Preferences::reset(); }
void tearDown() {}

void test_negative_slope_and_baseline_health() {
    const aquamax::PhPoint points[] = {{4.0f, 3.1f}, {7.0f, 2.5f}, {10.0f, 1.9f}};
    aquamax::PhCalibration first;
    TEST_ASSERT_TRUE(aquamax::fitPhCalibration(points, 3, {}, {}, first));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.2f, first.voltsPerPh);
    float ph = 0.0f;
    TEST_ASSERT_TRUE(aquamax::phFromVoltage(1.5f, {}, first, ph));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.0f, ph);
    const aquamax::PhPoint later[] = {{4.0f, 3.01f}, {10.0f, 1.99f}};
    aquamax::PhCalibration next;
    TEST_ASSERT_TRUE(aquamax::fitPhCalibration(later, 2, {}, first, next));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.2f, next.baselineVoltsPerPh);
    float health = 0.0f;
    TEST_ASSERT_TRUE(aquamax::phSlopeHealth(next, health));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 85.0f, health);
}

void test_invalid_calibration_does_not_replace_baseline() {
    aquamax::PhCalibration previous {true, -0.2f, 3.9f, -0.2f};
    aquamax::PhCalibration output = previous;
    const aquamax::PhPoint wrongSlope[] = {{4.0f, 2.0f}, {7.0f, 3.0f}};
    TEST_ASSERT_FALSE(aquamax::fitPhCalibration(wrongSlope, 2, {}, previous, output));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.2f, output.baselineVoltsPerPh);
    const aquamax::PhPoint sameBuffer[] = {{7.0f, 2.5f}, {7.0f, 2.4f}};
    TEST_ASSERT_FALSE(aquamax::fitPhCalibration(sameBuffer, 2, {}, previous, output));
}

void test_rails_and_invalid_voltage() {
    float boardVolts = -1.0f;
    const aquamax::PhVoltageLimits limits;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhVoltageStatus::valid), static_cast<int>(aquamax::classifyPhVoltage(1.5f, limits, boardVolts)));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, boardVolts);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhVoltageStatus::railed), static_cast<int>(aquamax::classifyPhVoltage(0.0f, limits, boardVolts)));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhVoltageStatus::railed), static_cast<int>(aquamax::classifyPhVoltage(3.0f, limits, boardVolts)));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhVoltageStatus::outOfRange), static_cast<int>(aquamax::classifyPhVoltage(3.1f, limits, boardVolts)));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhVoltageStatus::outOfRange), static_cast<int>(aquamax::classifyPhVoltage(std::numeric_limits<float>::quiet_NaN(), limits, boardVolts)));
}

void test_calibration_record_round_trip_and_corruption() {
    const aquamax::PhCalibration original {true, -0.17f, 3.69f, -0.2f};
    aquamax::PhCalibrationRecord record = {};
    TEST_ASSERT_TRUE(aquamax::encodePhCalibration(original, record));
    aquamax::PhCalibration restored;
    TEST_ASSERT_TRUE(aquamax::decodePhCalibration(record, restored));
    float health = 0.0f;
    TEST_ASSERT_TRUE(aquamax::phSlopeHealth(restored, health));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 85.0f, health);
    record[3] ^= 1u;
    TEST_ASSERT_FALSE(aquamax::decodePhCalibration(record, restored));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.2f, restored.baselineVoltsPerPh);
}

void test_store_preserves_first_baseline_and_rejects_failures() {
    aquamax::PhCalibrationStore store;
    aquamax::PhCalibration current;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhCalibrationLoad::notFound), static_cast<int>(store.load(current)));
    const aquamax::PhPoint first[] = {{4.0f, 3.1f}, {10.0f, 1.9f}};
    TEST_ASSERT_TRUE(store.commit(first, 2, {}, current));
    const aquamax::PhPoint next[] = {{4.0f, 3.01f}, {10.0f, 1.99f}};
    Preferences::failWrite = true;
    TEST_ASSERT_FALSE(store.commit(next, 2, {}, current));
    Preferences::failWrite = false;
    aquamax::PhCalibration loaded;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhCalibrationLoad::loaded), static_cast<int>(store.load(loaded)));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.2f, loaded.voltsPerPh);
    TEST_ASSERT_TRUE(store.commit(next, 2, {}, current));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhCalibrationLoad::loaded), static_cast<int>(store.load(loaded)));
    float health = 0.0f;
    TEST_ASSERT_TRUE(aquamax::phSlopeHealth(loaded, health));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 85.0f, health);
    Preferences::bytes[0] ^= 1u;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::PhCalibrationLoad::error), static_cast<int>(store.load(loaded)));
    TEST_ASSERT_FALSE(store.commit(first, 2, {}, current));
}

void test_fault_hides_value_before_debounce_and_recovers() {
    aquamax::PhChannel channel;
    const aquamax::PhCalibration calibration {true, -0.2f, 3.9f, -0.2f};
    channel.update(1.5f, {}, calibration, true);
    channel.update(1.5f, {}, calibration, true);
    TEST_ASSERT_TRUE(channel.update(1.5f, {}, calibration, true).hasValue);
    const auto firstFault = channel.update(0.0f, {}, calibration, true);
    TEST_ASSERT_FALSE(firstFault.hasValue);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::connected), static_cast<int>(firstFault.state));
    channel.update(0.0f, {}, calibration, true);
    const auto fault = channel.update(0.0f, {}, calibration, true);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::out_of_range), static_cast<int>(fault.state));
    TEST_ASSERT_FALSE(fault.hasValue);
    TEST_ASSERT_FALSE(channel.update(1.5f, {}, calibration, true).hasValue);
    channel.update(1.5f, {}, calibration, true);
    TEST_ASSERT_TRUE(channel.update(1.5f, {}, calibration, true).hasValue);
}

void test_uncalibrated_and_disabled_channel_never_publish_ph() {
    aquamax::PhChannel channel;
    channel.update(1.5f, {}, {}, true);
    channel.update(1.5f, {}, {}, true);
    const auto uncalibrated = channel.update(1.5f, {}, {}, true);
    TEST_ASSERT_TRUE(uncalibrated.hasBoardVolts);
    TEST_ASSERT_TRUE(uncalibrated.calibrationRequired);
    TEST_ASSERT_FALSE(uncalibrated.hasValue);
    const auto disabled = channel.update(1.5f, {}, {}, false);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::disabled), static_cast<int>(disabled.state));
    TEST_ASSERT_FALSE(disabled.hasValue);
}

void test_equal_resistor_divider_and_stability_reset() {
    aquamax::PhVoltageLimits limits;
    limits.dividerRatio = 0.5f;
    aquamax::PhChannel channel;
    for (unsigned index = 0; index < 8; ++index) {
        const auto reading = channel.update(1.26f, limits, {}, true);
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.52f, reading.boardVolts);
        TEST_ASSERT_EQUAL(index == 7, reading.stable);
        TEST_ASSERT_FALSE(reading.hasValue);
    }
    TEST_ASSERT_FALSE(channel.update(1.30f, limits, {}, true).stable);
    TEST_ASSERT_FALSE(channel.update(0.0f, limits, {}, true).stable);
    TEST_ASSERT_FALSE(channel.update(1.26f, limits, {}, true).stable);
}

void test_calibration_session_requires_distinct_valid_points_and_can_cancel() {
    aquamax::PhVoltageLimits limits;
    limits.dividerRatio = 0.5f;
    aquamax::PhCalibrationSession session;
    aquamax::PhCalibration result;
    TEST_ASSERT_FALSE(session.capture(6.86f, 2.5f, limits));
    session.start();
    TEST_ASSERT_FALSE(session.fit(limits, {}, result));
    TEST_ASSERT_TRUE(session.capture(6.86f, 2.5f, limits));
    TEST_ASSERT_FALSE(session.capture(7.0f, 2.4f, limits));
    TEST_ASSERT_TRUE(session.capture(6.86f, 2.52f, limits));
    TEST_ASSERT_EQUAL_UINT32(1, session.count());
    TEST_ASSERT_TRUE(session.capture(4.01f, 3.1f, limits));
    TEST_ASSERT_TRUE(session.fit(limits, {}, result));
    TEST_ASSERT_TRUE(result.valid);
    session.cancel();
    TEST_ASSERT_FALSE(session.active());
    TEST_ASSERT_EQUAL_UINT32(0, session.count());
    TEST_ASSERT_FALSE(session.fit(limits, {}, result));
}

void test_temperature_compensation_and_explicit_fallback() {
    aquamax::CompensationTemperature temperature;
    TEST_ASSERT_TRUE(aquamax::resolveCompensationTemperature(false, 99.0f, 25.0f, temperature));
    TEST_ASSERT_TRUE(temperature.usedDefault);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, temperature.valueC);
    TEST_ASSERT_TRUE(aquamax::resolveCompensationTemperature(true, 20.0f, 25.0f, temperature));
    TEST_ASSERT_FALSE(temperature.usedDefault);
    TEST_ASSERT_FALSE(aquamax::resolveCompensationTemperature(true, std::numeric_limits<float>::quiet_NaN(), 25.0f, temperature));
    const aquamax::PhCalibration calibration {true, -0.2f, 3.9f, -0.2f};
    float ph = 0.0f;
    TEST_ASSERT_TRUE(aquamax::temperatureCompensatedPh(1.38f, {}, calibration, 25.0f, 25.0f, ph));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.0f, ph);
    TEST_ASSERT_TRUE(aquamax::temperatureCompensatedPh(1.38f, {}, calibration, 25.0f, 35.0f, ph));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.96755f, ph);
    TEST_ASSERT_FALSE(aquamax::temperatureCompensatedPh(0.0f, {}, calibration, 25.0f, 25.0f, ph));
    float ppm = 0.0f;
    TEST_ASSERT_TRUE(aquamax::temperatureCompensatedTds(1413.0f, 25.0f, 0.5f, ppm));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 706.5f, ppm);
    TEST_ASSERT_TRUE(aquamax::temperatureCompensatedTds(1695.6f, 35.0f, 0.5f, ppm));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 706.5f, ppm);
    TEST_ASSERT_FALSE(aquamax::temperatureCompensatedTds(-1.0f, 25.0f, 0.5f, ppm));
    TEST_ASSERT_FALSE(aquamax::temperatureCompensatedTds(1.0f, 25.0f, 0.0f, ppm));
}

void test_tds_voltage_and_reference_calibration() {
    float ppm = 0.0f;
    float factor = 0.0f;
    TEST_ASSERT_TRUE(aquamax::tdsEstimate(0.39f, 25.0f, 1.0f, ppm));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 151.6901f, ppm);
    TEST_ASSERT_TRUE(aquamax::tdsCalibrationFactor(0.39f, 25.0f, 200.0f, factor));
    TEST_ASSERT_TRUE(aquamax::tdsEstimate(0.39f, 25.0f, factor, ppm));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 200.0f, ppm);
    TEST_ASSERT_FALSE(aquamax::tdsCalibrationFactor(0.0f, 25.0f, 707.0f, factor));
    TEST_ASSERT_FALSE(aquamax::tdsCalibrationFactor(0.39f, 25.0f, 0.0f, factor));
    TEST_ASSERT_FALSE(aquamax::tdsEstimate(2.31f, 25.0f, 1.0f, ppm));
    TEST_ASSERT_FALSE(aquamax::tdsEstimate(-0.1f, 25.0f, 1.0f, ppm));
    TEST_ASSERT_FALSE(aquamax::tdsEstimate(0.39f, 25.0f, 4.1f, ppm));
}

void test_tds_calibration_storage() {
    aquamax::TdsCalibrationStore store;
    aquamax::TdsCalibration calibration;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TdsCalibrationLoad::defaults), static_cast<int>(store.load(calibration)));
    TEST_ASSERT_FALSE(calibration.calibrated);
    TEST_ASSERT_TRUE(store.save({1.25f, 1234, true}));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TdsCalibrationLoad::loaded), static_cast<int>(store.load(calibration)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, calibration.factor);
    TEST_ASSERT_TRUE(calibration.calibrated);
    TEST_ASSERT_FALSE(store.save({5.0f, 0, true}));
    Preferences::failWrite = true;
    TEST_ASSERT_FALSE(store.save({0.8f, 0, true}));
    Preferences::failWrite = false;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TdsCalibrationLoad::loaded), static_cast<int>(store.load(calibration)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, calibration.factor);
    Preferences::bytes[0] ^= 1;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TdsCalibrationLoad::error), static_cast<int>(store.load(calibration)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_negative_slope_and_baseline_health);
    RUN_TEST(test_invalid_calibration_does_not_replace_baseline);
    RUN_TEST(test_rails_and_invalid_voltage);
    RUN_TEST(test_calibration_record_round_trip_and_corruption);
    RUN_TEST(test_store_preserves_first_baseline_and_rejects_failures);
    RUN_TEST(test_fault_hides_value_before_debounce_and_recovers);
    RUN_TEST(test_uncalibrated_and_disabled_channel_never_publish_ph);
    RUN_TEST(test_equal_resistor_divider_and_stability_reset);
    RUN_TEST(test_calibration_session_requires_distinct_valid_points_and_can_cancel);
    RUN_TEST(test_temperature_compensation_and_explicit_fallback);
    RUN_TEST(test_tds_voltage_and_reference_calibration);
    RUN_TEST(test_tds_calibration_storage);
    return UNITY_END();
}