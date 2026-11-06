#include "settings/TdsCalibrationStore.h"
#include <Preferences.h>
#include <cmath>
#include <cstring>

namespace aquamax {
namespace {
constexpr uint32_t schema = 1;
constexpr std::size_t recordWords = 5;

uint32_t checksum(const uint32_t* words) {
    uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < recordWords - 1; ++index) {
        for (unsigned shift = 0; shift < 32; shift += 8) hash = (hash ^ ((words[index] >> shift) & 0xffu)) * 16777619u;
    }
    return hash;
}
}

bool validTdsCalibration(const TdsCalibration& calibration) {
    return std::isfinite(calibration.factor) && calibration.factor >= 0.25f && calibration.factor <= 4.0f &&
           (calibration.calibrated || (calibration.factor == 1.0f && calibration.timestamp == 0));
}

TdsCalibrationLoad TdsCalibrationStore::load(TdsCalibration& calibration) {
    calibration = {};
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return TdsCalibrationLoad::error;
    if (!preferences.isKey("tdscal")) {
        preferences.end();
        return TdsCalibrationLoad::defaults;
    }
    uint32_t words[recordWords] = {};
    const std::size_t size = preferences.getBytes("tdscal", words, sizeof(words));
    preferences.end();
    if (size != sizeof(words) || words[0] != schema || words[3] > 1 || words[4] != checksum(words)) return TdsCalibrationLoad::error;
    TdsCalibration candidate;
    std::memcpy(&candidate.factor, &words[1], sizeof(float));
    candidate.timestamp = words[2];
    candidate.calibrated = words[3] != 0;
    if (!validTdsCalibration(candidate)) return TdsCalibrationLoad::error;
    calibration = candidate;
    return TdsCalibrationLoad::loaded;
}

bool TdsCalibrationStore::save(const TdsCalibration& calibration) {
    if (!validTdsCalibration(calibration)) return false;
    uint32_t words[recordWords] = {schema, 0, calibration.timestamp, calibration.calibrated ? 1u : 0u, 0};
    static_assert(sizeof(float) == sizeof(uint32_t), "Unsupported float representation");
    std::memcpy(&words[1], &calibration.factor, sizeof(float));
    words[4] = checksum(words);
    Preferences preferences;
    if (!preferences.begin("aquamax", false)) return false;
    const bool saved = preferences.putBytes("tdscal", words, sizeof(words)) == sizeof(words);
    preferences.end();
    return saved;
}

} // namespace aquamax