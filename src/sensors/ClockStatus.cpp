#include "sensors/ClockStatus.h"
#include <limits>

namespace aquamax {

ClockStatus clockStatus(int64_t unixSeconds, bool receivedNetworkTime) {
    constexpr int64_t earliestValidTime = 1704067200;
    if (!receivedNetworkTime || unixSeconds < earliestValidTime ||
        unixSeconds > std::numeric_limits<uint32_t>::max()) return {};
    return {true, static_cast<uint32_t>(unixSeconds)};
}

} // namespace aquamax