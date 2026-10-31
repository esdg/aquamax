#include <unity.h>
#include "sensors/ProbeState.h"

using aquamax::ProbeState;
using aquamax::ProbeStateTracker;

void setUp() {}
void tearDown() {}

void test_debounced_connection_and_fault() {
    ProbeStateTracker tracker;
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::disconnected), static_cast<int>(tracker.update(ProbeState::connected)));
    tracker.update(ProbeState::connected);
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::connected), static_cast<int>(tracker.update(ProbeState::connected)));
    tracker.update(ProbeState::unstable);
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::connected), static_cast<int>(tracker.update(ProbeState::connected)));
    tracker.update(ProbeState::disconnected);
    tracker.update(ProbeState::disconnected);
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::disconnected), static_cast<int>(tracker.update(ProbeState::disconnected)));
}

void test_disabled_and_reenabled() {
    ProbeStateTracker tracker(2, 2);
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::disabled), static_cast<int>(tracker.update(ProbeState::disabled)));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::disconnected), static_cast<int>(tracker.update(ProbeState::connected)));
    TEST_ASSERT_EQUAL(static_cast<int>(ProbeState::connected), static_cast<int>(tracker.update(ProbeState::connected)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_debounced_connection_and_fault);
    RUN_TEST(test_disabled_and_reenabled);
    return UNITY_END();
}