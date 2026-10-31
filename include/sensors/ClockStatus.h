#pragma once

#include <cstdint>

namespace aquamax {

struct ClockStatus {
    bool synchronized = false;
    uint32_t unixSeconds = 0;
};

ClockStatus clockStatus(int64_t unixSeconds, bool receivedNetworkTime);

} // namespace aquamax