#include <unity.h>
#include "hardware/Ads1115.h"
#include "sensors/AdcMath.h"

void setUp() {}
void tearDown() {}

void test_single_shot_timing_and_signed_conversion() {
    TwoWire wire;
    aquamax::Ads1115 adc(wire);
    int16_t raw = 0;
    TEST_ASSERT_TRUE(adc.start(0, 100));
    TEST_ASSERT_EQUAL_HEX8(0x48, wire.address);
    TEST_ASSERT_EQUAL_HEX16(0xc383, wire.configuration);
    TEST_ASSERT_FALSE(adc.start(1, 100));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::pending), static_cast<int>(adc.poll(108, raw)));
    wire.configuration &= 0x7fffu;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::pending), static_cast<int>(adc.poll(109, raw)));
    wire.configuration |= 0x8000u;
    wire.conversion = 0xfc18u;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::ready), static_cast<int>(adc.poll(110, raw)));
    TEST_ASSERT_EQUAL_INT16(-1000, raw);
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, -0.125f, aquamax::adsRawToVolts(raw));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::idle), static_cast<int>(adc.poll(111, raw)));
}

void test_errors_timeout_and_recovery() {
    TwoWire wire;
    aquamax::Ads1115 adc(wire);
    int16_t raw = 42;
    TEST_ASSERT_FALSE(adc.start(4, 0));
    wire.present = false;
    TEST_ASSERT_FALSE(adc.start(0, 0));
    wire.present = true;
    TEST_ASSERT_TRUE(adc.start(0, 0));
    wire.shortRead = true;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::error), static_cast<int>(adc.poll(9, raw)));
    TEST_ASSERT_EQUAL_INT16(42, raw);
    wire.shortRead = false;
    TEST_ASSERT_TRUE(adc.start(0, 10));
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::error), static_cast<int>(adc.poll(110, raw)));
    TEST_ASSERT_TRUE(adc.start(0, 111));
    wire.conversion = 24000;
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::AdcResult::ready), static_cast<int>(adc.poll(120, raw)));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, aquamax::adsRawToVolts(raw));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_single_shot_timing_and_signed_conversion);
    RUN_TEST(test_errors_timeout_and_recovery);
    return UNITY_END();
}