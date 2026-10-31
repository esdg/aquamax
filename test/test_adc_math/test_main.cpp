#include <unity.h>
#include "sensors/AdcMath.h"

void setUp() {}
void tearDown() {}

void test_single_shot_configuration_and_scaling() {
    uint16_t config = 0;
    TEST_ASSERT_TRUE(aquamax::adsSingleShotConfig(0, config));
    TEST_ASSERT_EQUAL_HEX16(0xc383, config);
    TEST_ASSERT_TRUE(aquamax::adsSingleShotConfig(1, config));
    TEST_ASSERT_EQUAL_HEX16(0xd383, config);
    TEST_ASSERT_FALSE(aquamax::adsSingleShotConfig(4, config));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, aquamax::adsRawToVolts(24000));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -0.125f, aquamax::adsRawToVolts(-1000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_single_shot_configuration_and_scaling);
    return UNITY_END();
}