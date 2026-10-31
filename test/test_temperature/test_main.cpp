#include <unity.h>
#include "sensors/TemperatureDecode.h"

void setUp() {}
void tearDown() {}

void test_temperature_crc_and_sentinels() {
    uint8_t data[9] = {0x90, 0x01, 0x4b, 0x46, 0x7f, 0xff, 0x0c, 0x10, 0};
    data[8] = aquamax::oneWireCrc8(data, 8);
    float value = 0;
    TEST_ASSERT_TRUE(aquamax::decodeTemperature(data, value));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, value);
    data[0] ^= 1;
    TEST_ASSERT_FALSE(aquamax::decodeTemperature(data, value));
    data[0] = 0x50;
    data[1] = 0x05;
    data[8] = aquamax::oneWireCrc8(data, 8);
    TEST_ASSERT_FALSE(aquamax::decodeTemperature(data, value));
    data[0] = 0x60;
    data[1] = 0xff;
    data[8] = aquamax::oneWireCrc8(data, 8);
    TEST_ASSERT_TRUE(aquamax::decodeTemperature(data, value));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -10.0f, value);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temperature_crc_and_sentinels);
    return UNITY_END();
}