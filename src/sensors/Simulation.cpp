#include "sensors/Simulation.h"
#include <cmath>

namespace aquamax {

void Simulation::setPresent(bool temperature, bool ph, bool tds) {
    temperaturePresent_ = temperature;
    phPresent_ = ph;
    tdsPresent_ = tds;
}

SimulatedSnapshot Simulation::sample(uint32_t elapsedMs) {
    const float seconds = static_cast<float>(elapsedMs) / 1000.0f;
    SimulatedSnapshot result;
    result.temperature.state = temperatureState_.update(temperaturePresent_ ? ProbeState::connected : ProbeState::disconnected);
    result.ph.state = phState_.update(phPresent_ ? ProbeState::connected : ProbeState::disconnected);
    result.tds.state = tdsState_.update(tdsPresent_ ? ProbeState::connected : ProbeState::disconnected);
    result.temperature.hasValue = temperaturePresent_ && result.temperature.state == ProbeState::connected;
    result.ph.hasValue = phPresent_ && result.ph.state == ProbeState::connected;
    result.tds.hasValue = tdsPresent_ && result.tds.state == ProbeState::connected;
    if (result.temperature.hasValue) result.temperature.value = 25.5f + 0.3f * std::sin(seconds / 90.0f);
    if (result.ph.hasValue) result.ph.value = 7.0f + 0.1f * std::sin(seconds / 120.0f);
    if (result.tds.hasValue) result.tds.value = 180.0f + 5.0f * std::sin(seconds / 180.0f);
    result.defaultCompensation = !result.temperature.hasValue;
    return result;
}

} // namespace aquamax