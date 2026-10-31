#include "display/GaugeModel.h"
#include <algorithm>
#include <cmath>

namespace aquamax {

GaugeModel::GaugeModel(GaugeRange range, float easingSeconds)
    : easingSeconds_(std::isfinite(easingSeconds) && easingSeconds > 0.0f ? easingSeconds : 0.5f) {
    setRange(range);
}

bool GaugeModel::setRange(GaugeRange range) {
    if (!std::isfinite(range.minimum) || !std::isfinite(range.maximum) || range.minimum >= range.maximum ||
        !std::isfinite(range.maximum - range.minimum)) {
        return false;
    }
    range_ = range;
    rangeValid_ = true;
    cursorValid_ = false;
    return true;
}

GaugeFrame GaugeModel::update(ProbeState state, bool hasValue, float value, bool alert, uint32_t nowMs) {
    GaugeFrame frame;
    frame.state = state;
    if (!rangeValid_ || state != ProbeState::connected || !hasValue || !std::isfinite(value)) {
        cursorValid_ = false;
        return frame;
    }
    const float target = std::clamp((value - range_.minimum) / (range_.maximum - range_.minimum), 0.0f, 1.0f);
    if (!cursorValid_) {
        cursor_ = target;
        cursorValid_ = true;
    } else {
        const float elapsedSeconds = static_cast<float>(nowMs - lastUpdateMs_) / 1000.0f;
        const float blend = 1.0f - std::exp(-elapsedSeconds / easingSeconds_);
        cursor_ += (target - cursor_) * blend;
    }
    lastUpdateMs_ = nowMs;
    frame.hasValue = true;
    frame.showCursor = true;
    frame.alert = alert;
    frame.value = value;
    frame.cursorPosition = cursor_;
    return frame;
}

} // namespace aquamax