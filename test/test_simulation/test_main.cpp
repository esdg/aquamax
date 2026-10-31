#include <unity.h>
#include "sensors/Simulation.h"

void setUp() {}
void tearDown() {}

void test_disconnect_never_returns_stale_value() {
    aquamax::Simulation simulation;
    simulation.sample(0);
    simulation.sample(1000);
    TEST_ASSERT_TRUE(simulation.sample(2000).temperature.hasValue);
    simulation.setPresent(false, true, true);
    const auto disconnected = simulation.sample(3000);
    TEST_ASSERT_FALSE(disconnected.temperature.hasValue);
    TEST_ASSERT_TRUE(disconnected.defaultCompensation);
    TEST_ASSERT_TRUE(disconnected.ph.hasValue);
    simulation.sample(4000);
    TEST_ASSERT_EQUAL(static_cast<int>(aquamax::ProbeState::disconnected), static_cast<int>(simulation.sample(5000).temperature.state));
    simulation.setPresent(true, true, true);
    TEST_ASSERT_FALSE(simulation.sample(6000).temperature.hasValue);
    simulation.sample(7000);
    TEST_ASSERT_TRUE(simulation.sample(8000).temperature.hasValue);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_disconnect_never_returns_stale_value);
    return UNITY_END();
}