#include "sensors/TimeZone.h"
#include <cstring>

namespace aquamax {
namespace {
constexpr TimeZone zones[] = {
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Etc/UTC", "UTC0"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Asia/Tokyo", "JST-9"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
}

const TimeZone* findTimeZone(const char* name) {
    if (!name) return nullptr;
    for (const auto& zone : zones) {
        if (std::strcmp(zone.name, name) == 0) return &zone;
    }
    return nullptr;
}

} // namespace aquamax