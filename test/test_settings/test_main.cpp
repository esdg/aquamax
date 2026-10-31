#include <unity.h>
#include <limits>
#include <Preferences.h>
#include "settings/Settings.h"
#include "settings/SettingsStore.h"
#include "settings/TemperatureCalibrationStore.h"

void setUp() { Preferences::reset(); }
void tearDown() {}

void test_settings_validation() {
    aquamax::Settings settings;
    TEST_ASSERT_TRUE(aquamax::validSettings(settings));
    settings.defaultTemperatureC = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(aquamax::validSettings(settings));
    settings = {};
    settings.tdsWarmupMs = settings.tdsIntervalMs;
    TEST_ASSERT_FALSE(aquamax::validSettings(settings));
    settings = {};
    settings.tdsIntervalMs = 1;
    TEST_ASSERT_FALSE(aquamax::validSettings(settings));
}

void test_settings_store_defaults_roundtrip_and_corruption() {
    aquamax::SettingsStore store;
    aquamax::Settings settings;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::SettingsLoad::defaults), static_cast<int>(store.load(settings)));
    settings.defaultTemperatureC = 28.5f;
    settings.cableDetectEnabled = true;
    TEST_ASSERT_TRUE(store.save(settings));
    aquamax::Settings loaded;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::SettingsLoad::loaded), static_cast<int>(store.load(loaded)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 28.5f, loaded.defaultTemperatureC);
    TEST_ASSERT_TRUE(loaded.cableDetectEnabled);
    Preferences::bytes[4] ^= 1u;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::SettingsLoad::error), static_cast<int>(store.load(loaded)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, aquamax::Settings{}.defaultTemperatureC, loaded.defaultTemperatureC);
    TEST_ASSERT_FALSE(loaded.cableDetectEnabled);
}

void test_settings_store_rejects_bad_data_and_storage_errors() {
    aquamax::SettingsStore store;
    aquamax::Settings settings;
    settings.defaultTemperatureC = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(store.save(settings));
    TEST_ASSERT_FALSE(Preferences::present);
    settings = {};
    Preferences::failWrite = true;
    TEST_ASSERT_FALSE(store.save(settings));
    Preferences::failWrite = false;
    TEST_ASSERT_TRUE(store.save(settings));
    --Preferences::length;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::SettingsLoad::error), static_cast<int>(store.load(settings)));
    Preferences::failBegin = true;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::SettingsLoad::error), static_cast<int>(store.load(settings)));
    TEST_ASSERT_FALSE(store.save(settings));
}

void test_temperature_calibration_persists_and_rejects_corruption() {
    aquamax::TemperatureCalibrationStore store;
    aquamax::TemperatureCalibration calibration;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TemperatureCalibrationLoad::defaults), static_cast<int>(store.load(calibration)));
    calibration = {1.25f, 1700000000u, true};
    TEST_ASSERT_TRUE(store.save(calibration));
    aquamax::TemperatureCalibration loaded;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TemperatureCalibrationLoad::loaded), static_cast<int>(store.load(loaded)));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, loaded.offsetC);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, loaded.timestamp);
    TEST_ASSERT_TRUE(loaded.calibrated);
    Preferences::bytes[4] ^= 1u;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::TemperatureCalibrationLoad::error), static_cast<int>(store.load(loaded)));
    TEST_ASSERT_FALSE(loaded.calibrated);
    TEST_ASSERT_FALSE(store.save({5.01f, 0, true}));
    TEST_ASSERT_FALSE(store.save({std::numeric_limits<float>::quiet_NaN(), 0, true}));
    TEST_ASSERT_FALSE(store.save({1.0f, 0, false}));
    Preferences::failWrite = true;
    TEST_ASSERT_FALSE(store.save({0, 0, false}));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_settings_validation);
    RUN_TEST(test_settings_store_defaults_roundtrip_and_corruption);
    RUN_TEST(test_settings_store_rejects_bad_data_and_storage_errors);
    RUN_TEST(test_temperature_calibration_persists_and_rejects_corruption);
    return UNITY_END();
}