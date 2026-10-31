#pragma once

#if defined(AQUAMAX_WEB_PREVIEW)
#include <cstdio>
#include "sensors/HistoryEngine.h"

namespace aquamax {

class HistoryFileStorage : public HistoryStorage {
public:
    ~HistoryFileStorage() override;
    bool beginBatch() override;
    bool endBatch() override;
    HistoryRead read(HistoryMetric metric, std::size_t tier, uint32_t slot, uint8_t* bytes) override;
    bool write(HistoryMetric metric, std::size_t tier, uint32_t slot, const uint8_t* bytes) override;
    bool clear() override;

private:
    void closeReader();
    bool finishWriter();
    std::FILE* reader_ = nullptr;
    std::FILE* writer_ = nullptr;
    HistoryMetric readerMetric_ = HistoryMetric::temperature;
    HistoryMetric writerMetric_ = HistoryMetric::temperature;
    std::size_t readerTier_ = historyTierCount;
    long readerSize_ = 0;
    bool readerMissing_ = false;
    std::size_t writerTier_ = historyTierCount;
    bool batchError_ = false;
};

} // namespace aquamax
#endif