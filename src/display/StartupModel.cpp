#include "display/StartupModel.h"

namespace aquamax {

void StartupModel::begin(uint32_t nowMs, bool hasSavedWifi) {
    startedMs_ = nowMs;
    hasSavedWifi_ = hasSavedWifi;
    finished_ = false;
}

StartupScreen StartupModel::update(uint32_t nowMs, bool wifiConnected, bool wifiTimedOut) {
    if (finished_) return StartupScreen::gauges;
    const uint32_t elapsed = nowMs - startedMs_;
    if (elapsed < animationCycleMs) return StartupScreen::splash;
    if (!hasSavedWifi_ && !wifiConnected) return StartupScreen::setup;
    if (wifiConnected || wifiTimedOut) {
        finished_ = true;
        return StartupScreen::gauges;
    }
    return StartupScreen::splash;
}

} // namespace aquamax