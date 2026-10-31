#include <unity.h>
#include "sensors/HistoryAggregate.h"

void setUp() {}
void tearDown() {}

void test_weighted_aggregation_preserves_extrema() {
    aquamax::HistoryAggregate aggregate;
    aquamax::HistoryPoint point;
    TEST_ASSERT_FALSE(aggregate.result(point));
    aggregate.add({100, 1000, 2000, 3000, 1});
    aggregate.add({110, 1500, 2500, 4000, 3});
    TEST_ASSERT_TRUE(aggregate.result(point));
    TEST_ASSERT_EQUAL_INT16(1000, point.minimum);
    TEST_ASSERT_EQUAL_INT16(2375, point.average);
    TEST_ASSERT_EQUAL_INT16(4000, point.maximum);
    TEST_ASSERT_EQUAL_UINT32(4, point.count);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_weighted_aggregation_preserves_extrema);
    return UNITY_END();
}