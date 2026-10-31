#pragma once

namespace aquamax {

struct TimeZone {
    const char* name;
    const char* rule;
};

const TimeZone* findTimeZone(const char* name);

} // namespace aquamax