#if defined(AQUAMAX_WEB_PREVIEW)
#include "webserver/HistoryFileStorage.h"
#include <cerrno>
#include <unistd.h>

namespace aquamax {
namespace {
bool pathFor(HistoryMetric metric, std::size_t tier, char* path, std::size_t size) {
    const unsigned metricIndex = static_cast<unsigned>(metric);
    if (metricIndex > 2 || tier >= historyTierCount) return false;
    return std::snprintf(path, size, "/littlefs/history-%u-%u.bin", metricIndex, static_cast<unsigned>(tier)) > 0;
}
}

HistoryFileStorage::~HistoryFileStorage() { closeReader(); finishWriter(); }

void HistoryFileStorage::closeReader() {
    if (reader_) std::fclose(reader_);
    reader_ = nullptr;
    readerTier_ = historyTierCount;
    readerSize_ = 0;
    readerMissing_ = false;
}

bool HistoryFileStorage::finishWriter() {
    if (!writer_) return true;
    const bool success = std::fflush(writer_) == 0 && fsync(fileno(writer_)) == 0;
    const bool closed = std::fclose(writer_) == 0;
    writer_ = nullptr;
    writerTier_ = historyTierCount;
    return success && closed;
}

bool HistoryFileStorage::beginBatch() {
    closeReader();
    batchError_ = false;
    return finishWriter();
}

bool HistoryFileStorage::endBatch() {
    return finishWriter() && !batchError_;
}

HistoryRead HistoryFileStorage::read(HistoryMetric metric, std::size_t tier, uint32_t slot, uint8_t* bytes) {
    if (!bytes || tier >= historyTierCount || slot >= historyTiers[tier].capacity) return HistoryRead::error;
    if (readerTier_ != tier || readerMetric_ != metric) {
        closeReader();
        char path[40];
        if (!pathFor(metric, tier, path, sizeof(path))) return HistoryRead::error;
        readerMetric_ = metric;
        readerTier_ = tier;
        reader_ = std::fopen(path, "rb");
        if (!reader_) {
            if (errno == ENOENT) { readerMissing_ = true; return HistoryRead::missing; }
            closeReader();
            return HistoryRead::error;
        }
        if (std::fseek(reader_, 0, SEEK_END) != 0 || (readerSize_ = std::ftell(reader_)) < 0) {
            closeReader();
            return HistoryRead::error;
        }
    }
    if (readerMissing_) return HistoryRead::missing;
    const long offset = static_cast<long>(slot * historyRecordBytes);
    if (offset + static_cast<long>(historyRecordBytes) > readerSize_) return HistoryRead::missing;
    if (std::fseek(reader_, offset, SEEK_SET) != 0 || std::fread(bytes, 1, historyRecordBytes, reader_) != historyRecordBytes) {
        closeReader();
        return HistoryRead::error;
    }
    return HistoryRead::found;
}

bool HistoryFileStorage::write(HistoryMetric metric, std::size_t tier, uint32_t slot, const uint8_t* bytes) {
    if (!bytes || tier >= historyTierCount || slot >= historyTiers[tier].capacity) return false;
    closeReader();
    if (!writer_ || writerMetric_ != metric || writerTier_ != tier) {
        if (!finishWriter()) { batchError_ = true; return false; }
        char path[40];
        if (!pathFor(metric, tier, path, sizeof(path))) return false;
        writer_ = std::fopen(path, "r+b");
        if (!writer_ && errno == ENOENT) writer_ = std::fopen(path, "w+b");
        if (!writer_) return false;
        writerMetric_ = metric;
        writerTier_ = tier;
    }
    return std::fseek(writer_, static_cast<long>(slot * historyRecordBytes), SEEK_SET) == 0 &&
           std::fwrite(bytes, 1, historyRecordBytes, writer_) == historyRecordBytes;
}

bool HistoryFileStorage::clear() {
    closeReader();
    bool success = finishWriter();
    for (unsigned metric = 0; metric < 3; ++metric) {
        for (std::size_t tier = 0; tier < historyTierCount; ++tier) {
            char path[40];
            if (!pathFor(static_cast<HistoryMetric>(metric), tier, path, sizeof(path))) return false;
            if (std::remove(path) != 0 && errno != ENOENT) success = false;
        }
    }
    return success;
}

} // namespace aquamax
#endif