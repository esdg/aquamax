#include "events/EventJournal.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace aquamax {
namespace {
constexpr std::size_t recordBytes = 134;
constexpr std::size_t checksumOffset = 130;

uint32_t read32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}
void write32(uint8_t* bytes, uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) bytes[index] = static_cast<uint8_t>(value >> (index * 8));
}
uint32_t checksum(const uint8_t* bytes) {
    uint32_t hash = 2166136261u;
    for (std::size_t index = 0; index < checksumOffset; ++index) hash = (hash ^ bytes[index]) * 16777619u;
    return hash;
}
bool decode(const uint8_t* bytes, std::size_t slot, JournalEntry& entry) {
    const uint32_t id = read32(bytes);
    if (!id || (id - 1) % journalCapacity != slot || bytes[8] < 1 || bytes[8] > 3 ||
        !bytes[9] || bytes[9] > journalTextLimit || read32(bytes + checksumOffset) != checksum(bytes)) return false;
    JournalEntry candidate;
    candidate.id = id;
    candidate.timestamp = read32(bytes + 4);
    candidate.kind = static_cast<JournalKind>(bytes[8]);
    std::memcpy(candidate.text, bytes + 10, bytes[9]);
    if (std::strlen(candidate.text) != bytes[9] || !validJournalText(candidate.text)) return false;
    entry = candidate;
    return true;
}
bool syncFile(std::FILE* file) {
    if (std::fflush(file) != 0) return false;
#if defined(_WIN32)
    return _commit(_fileno(file)) == 0;
#else
    return fsync(fileno(file)) == 0;
#endif
}
}

bool validJournalText(const char* text) {
    if (!text) return false;
    const std::size_t length = std::strlen(text);
    if (!length || length > journalTextLimit) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    for (std::size_t index = 0; index < length;) {
        const unsigned char first = bytes[index];
        if ((first < 0x20 && first != '\n' && first != '\r' && first != '\t') || first == 0x7f) return false;
        if (first < 0x80) { ++index; continue; }
        const std::size_t count = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!count || index + count > length) return false;
        for (std::size_t part = 1; part < count; ++part) if ((bytes[index + part] & 0xc0) != 0x80) return false;
        if ((first == 0xe0 && bytes[index + 1] < 0xa0) || (first == 0xed && bytes[index + 1] >= 0xa0) ||
            (first == 0xf0 && bytes[index + 1] < 0x90) || (first == 0xf4 && bytes[index + 1] >= 0x90)) return false;
        index += count;
    }
    return true;
}

bool EventJournal::begin() {
    for (auto& entry : entries_) entry = {};
    lastId_ = 0;
    std::FILE* file = std::fopen(path_, "rb");
    if (!file) return errno == ENOENT;
    uint8_t bytes[recordBytes];
    for (std::size_t slot = 0; slot < journalCapacity; ++slot) {
        const std::size_t count = std::fread(bytes, 1, sizeof(bytes), file);
        if (count != sizeof(bytes)) { if (std::ferror(file)) { std::fclose(file); return false; } break; }
        if (decode(bytes, slot, entries_[slot]) && entries_[slot].id > lastId_) lastId_ = entries_[slot].id;
    }
    return std::fclose(file) == 0;
}

bool EventJournal::write(std::size_t slot, const uint8_t* bytes) {
    std::FILE* file = std::fopen(path_, "r+b");
    if (!file && errno == ENOENT) file = std::fopen(path_, "w+b");
    if (!file) return false;
    const bool saved = std::fseek(file, static_cast<long>(slot * recordBytes), SEEK_SET) == 0 &&
                       std::fwrite(bytes, 1, recordBytes, file) == recordBytes && syncFile(file);
    return std::fclose(file) == 0 && saved;
}

bool EventJournal::add(uint32_t timestamp, JournalKind kind, const char* text) {
    if (!validJournalText(text) || kind < JournalKind::note || kind > JournalKind::recovery || lastId_ == UINT32_MAX) return false;
    const uint32_t id = lastId_ + 1;
    const std::size_t slot = (id - 1) % journalCapacity;
    uint8_t bytes[recordBytes] = {};
    write32(bytes, id);
    write32(bytes + 4, timestamp);
    bytes[8] = static_cast<uint8_t>(kind);
    bytes[9] = static_cast<uint8_t>(std::strlen(text));
    std::memcpy(bytes + 10, text, bytes[9]);
    write32(bytes + checksumOffset, checksum(bytes));
    if (!write(slot, bytes)) return false;
    JournalEntry entry;
    if (!decode(bytes, slot, entry)) return false;
    entries_[slot] = entry;
    lastId_ = id;
    return true;
}

bool EventJournal::remove(uint32_t id) {
    if (!id) return false;
    const std::size_t slot = (id - 1) % journalCapacity;
    if (entries_[slot].id != id) return false;
    const uint8_t bytes[recordBytes] = {};
    if (!write(slot, bytes)) return false;
    entries_[slot] = {};
    return true;
}

const JournalEntry* EventJournal::recent(std::size_t index) const {
    for (std::size_t offset = 0; offset < journalCapacity && offset < lastId_; ++offset) {
        const uint32_t id = lastId_ - static_cast<uint32_t>(offset);
        const JournalEntry& entry = entries_[(id - 1) % journalCapacity];
        if (entry.id == id) { if (!index) return &entry; --index; }
    }
    return nullptr;
}

std::size_t EventJournal::size() const {
    std::size_t count = 0;
    for (const auto& entry : entries_) if (entry.id) ++count;
    return count;
}

} // namespace aquamax