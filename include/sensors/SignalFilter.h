#pragma once

#include <cstddef>

namespace aquamax {

class SignalFilter {
public:
    bool add(float sample, float& filtered);
    void reset();

private:
    static constexpr std::size_t medianSize = 5;
    static constexpr std::size_t averageSize = 5;
    float samples_[medianSize] = {};
    float medians_[averageSize] = {};
    std::size_t sampleCount_ = 0;
    std::size_t sampleNext_ = 0;
    std::size_t averageCount_ = 0;
    std::size_t averageNext_ = 0;
};

} // namespace aquamax