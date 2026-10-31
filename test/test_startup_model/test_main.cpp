#include <unity.h>
#include <cstdint>
#include "display/StartupModel.h"

using namespace aquamax;

void setUp() {}
void tearDown() {}

void test_saved_wifi_starts_after_connection_and_logo_cycle() {
    StartupModel startup;
    startup.begin(100, true);
    TEST_ASSERT_TRUE(startup.update(2099, true, false) == StartupScreen::splash);
    TEST_ASSERT_TRUE(startup.update(2100, true, false) == StartupScreen::gauges);
    TEST_ASSERT_TRUE(startup.update(2101, false, false) == StartupScreen::gauges);
}

void test_saved_wifi_waits_until_it_connects() {
    StartupModel startup;
    startup.begin(0, true);
    TEST_ASSERT_TRUE(startup.update(2000, false, false) == StartupScreen::splash);
    TEST_ASSERT_TRUE(startup.update(5000, false, false) == StartupScreen::splash);
    TEST_ASSERT_TRUE(startup.update(5001, true, false) == StartupScreen::gauges);
}

void test_saved_network_starts_offline_after_timeout() {
    StartupModel startup;
    startup.begin(0, true);
    TEST_ASSERT_TRUE(startup.update(20000, false, false) == StartupScreen::splash);
    TEST_ASSERT_TRUE(startup.update(20001, false, true) == StartupScreen::gauges);
}

void test_first_setup_waits_for_connection_even_after_timeout() {
    StartupModel startup;
    startup.begin(0, false);
    TEST_ASSERT_TRUE(startup.update(1999, false, false) == StartupScreen::splash);
    TEST_ASSERT_TRUE(startup.update(2000, false, false) == StartupScreen::setup);
    TEST_ASSERT_TRUE(startup.update(30000, false, true) == StartupScreen::setup);
    TEST_ASSERT_TRUE(startup.update(31000, true, true) == StartupScreen::gauges);
    TEST_ASSERT_TRUE(startup.update(32000, false, true) == StartupScreen::gauges);
}

void test_first_setup_can_finish_early_and_time_wraps() {
    StartupModel startup;
    startup.begin(UINT32_MAX - 999, false);
    TEST_ASSERT_TRUE(startup.update(999, true, false) == StartupScreen::splash);
    TEST_ASSERT_TRUE(startup.update(1000, false, false) == StartupScreen::setup);
    TEST_ASSERT_TRUE(startup.update(1001, true, false) == StartupScreen::gauges);
    startup.begin(UINT32_MAX - 999, true);
    TEST_ASSERT_TRUE(startup.update(19000, false, true) == StartupScreen::gauges);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_saved_wifi_starts_after_connection_and_logo_cycle);
    RUN_TEST(test_saved_wifi_waits_until_it_connects);
    RUN_TEST(test_saved_network_starts_offline_after_timeout);
    RUN_TEST(test_first_setup_waits_for_connection_even_after_timeout);
    RUN_TEST(test_first_setup_can_finish_early_and_time_wraps);
    return UNITY_END();
}