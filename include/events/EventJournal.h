#pragma once

#include <cstddef>
#include <cstdint>

namespace aquamax {

constexpr std::size_t journalCapacity = 100;
constexpr std::size_t journalTextLimit = 120;

enum class JournalKind : uint8_t { note = 1, alert = 2, recovery = 3 };

struct JournalEntry {
    uint32_t id = 0;
    uint32_t timestamp = 0;
    JournalKind kind = JournalKind::note;
    char text[journalTextLimit + 1] = {};
};

bool validJournalText(const char* text);

class EventJournal {
public:
    explicit EventJournal(const char* path) : path_(path) {}
    bool begin();
    bool add(uint32_t timestamp, JournalKind kind, const char* text);
    bool remove(uint32_t id);
    const JournalEntry* recent(std::size_t index) const;
    std::size_t size() const;

private:
    bool write(std::size_t slot, const uint8_t* bytes);
    const char* path_;
    JournalEntry entries_[journalCapacity] = {};
    uint32_t lastId_ = 0;
};

} // namespace aquamax