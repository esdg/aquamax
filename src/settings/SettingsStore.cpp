#include "settings/SettingsStore.h"
#include <Preferences.h>
#include <cstring>

namespace aquamax {
namespace {

constexpr uint32_t schema = 1;
constexpr std::size_t recordWords = 8;

uint32_t checksum(const uint32_t* words) {
    uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < recordWords - 1; ++index) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash = (hash ^ ((words[index] >> shift) & 0xffu)) * 16777619u;
        }
    }
    return hash;
}

} // namespace

SettingsLoad SettingsStore::load(Settings& settings) {
    settings = Settings{};
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return SettingsLoad::error;
    if (!preferences.isKey("settings")) {
        preferences.end();
        return SettingsLoad::defaults;
    }
    uint32_t words[recordWords] = {};
    const std::size_t size = preferences.getBytes("settings", words, sizeof(words));
    preferences.end();
    if (size != sizeof(words) || words[0] != schema || words[7] != checksum(words) || words[6] > 1) {
        return SettingsLoad::error;
    }
    Settings candidate;
    candidate.tdsIntervalMs = words[1];
    candidate.tdsWarmupMs = words[2];
    candidate.tdsSampleMs = words[3];
    candidate.phRecoveryMs = words[4];
    std::memcpy(&candidate.defaultTemperatureC, &words[5], sizeof(float));
    candidate.cableDetectEnabled = words[6] != 0;
    if (!validSettings(candidate)) return SettingsLoad::error;
    settings = candidate;
    return SettingsLoad::loaded;
}

bool SettingsStore::save(const Settings& settings) {
    if (!validSettings(settings)) return false;
    uint32_t words[recordWords] = {schema, settings.tdsIntervalMs, settings.tdsWarmupMs,
                                   settings.tdsSampleMs, settings.phRecoveryMs, 0,
                                   settings.cableDetectEnabled ? 1u : 0u, 0};
    static_assert(sizeof(float) == sizeof(uint32_t), "Unsupported float representation");
    std::memcpy(&words[5], &settings.defaultTemperatureC, sizeof(float));
    words[7] = checksum(words);
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return false;
    const bool saved = preferences.putBytes("settings", words, sizeof(words)) == sizeof(words);
    preferences.end();
    return saved;
}

} // namespace aquamax