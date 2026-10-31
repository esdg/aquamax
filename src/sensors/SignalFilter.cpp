#include "sensors/SignalFilter.h"
#include <algorithm>
#include <cmath>

namespace aquamax {

bool SignalFilter::add(float sample, float& filtered) {
    if (!std::isfinite(sample)) return false;
    samples_[sampleNext_] = sample;
    sampleNext_ = (sampleNext_ + 1) % medianSize;
    if (sampleCount_ < medianSize) ++sampleCount_;
    if (sampleCount_ < medianSize) return false;
    float sorted[medianSize];
    std::copy(samples_, samples_ + medianSize, sorted);
    std::sort(sorted, sorted + medianSize);
    medians_[averageNext_] = sorted[medianSize / 2];
    averageNext_ = (averageNext_ + 1) % averageSize;
    if (averageCount_ < averageSize) ++averageCount_;
    float sum = 0.0f;
    for (std::size_t index = 0; index < averageCount_; ++index) sum += medians_[index];
    filtered = sum / static_cast<float>(averageCount_);
    return true;
}

void SignalFilter::reset() {
    sampleCount_ = 0;
    sampleNext_ = 0;
    averageCount_ = 0;
    averageNext_ = 0;
}

} // namespace aquamax