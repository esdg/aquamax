#include "settings/AlertSettingsStore.h"
#include <Preferences.h>
#include <cstring>

namespace aquamax {
namespace {
constexpr uint32_t schema = 1;
constexpr std::size_t recordWords = 8;

uint32_t checksum(const uint32_t* words) {
    uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < recordWords - 1; ++index) {
        for (unsigned shift = 0; shift < 32; shift += 8) hash = (hash ^ ((words[index] >> shift) & 0xffu)) * 16777619u;
    }
    return hash;
}
}

AlertSettingsLoad AlertSettingsStore::load(AlertConfig& config) {
    config = {};
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return AlertSettingsLoad::error;
    if (!preferences.isKey("alerts")) { preferences.end(); return AlertSettingsLoad::defaults; }
    uint32_t words[recordWords] = {};
    const std::size_t size = preferences.getBytes("alerts", words, sizeof(words));
    preferences.end();
    if (size != sizeof(words) || words[0] != schema || words[1] > 3 || words[7] != checksum(words)) return AlertSettingsLoad::error;
    AlertConfig candidate;
    candidate.thresholdsEnabled = (words[1] & 1u) != 0;
    candidate.probeFaultEnabled = (words[1] & 2u) != 0;
    std::memcpy(&candidate.minimum, &words[2], sizeof(float));
    std::memcpy(&candidate.maximum, &words[3], sizeof(float));
    std::memcpy(&candidate.hysteresis, &words[4], sizeof(float));
    candidate.debounceMs = words[5];
    candidate.repeatMs = words[6];
    if (!validAlertConfig(candidate)) return AlertSettingsLoad::error;
    config = candidate;
    return AlertSettingsLoad::loaded;
}

bool AlertSettingsStore::save(const AlertConfig& config) {
    if (!validAlertConfig(config)) return false;
    uint32_t words[recordWords] = {schema, (config.thresholdsEnabled ? 1u : 0u) | (config.probeFaultEnabled ? 2u : 0u), 0, 0, 0, config.debounceMs, config.repeatMs, 0};
    static_assert(sizeof(float) == sizeof(uint32_t), "Unsupported float representation");
    std::memcpy(&words[2], &config.minimum, sizeof(float));
    std::memcpy(&words[3], &config.maximum, sizeof(float));
    std::memcpy(&words[4], &config.hysteresis, sizeof(float));
    words[7] = checksum(words);
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return false;
    const bool saved = preferences.putBytes("alerts", words, sizeof(words)) == sizeof(words);
    preferences.end();
    return saved;
}

} // namespace aquamax