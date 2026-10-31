#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class Preferences {
public:
    inline static bool failBegin = false;
    inline static bool failWrite = false;
    inline static bool present = false;
    inline static uint8_t bytes[64] = {};
    inline static std::size_t length = 0;

    static void reset() {
        failBegin = false;
        failWrite = false;
        present = false;
        length = 0;
    }

    bool begin(const char*, bool) { return !failBegin; }
    void end() {}
    bool isKey(const char*) { return present; }
    std::size_t getBytes(const char*, void* destination, std::size_t capacity) {
        if (!present || capacity < length) return 0;
        std::memcpy(destination, bytes, length);
        return length;
    }
    std::size_t putBytes(const char*, const void* source, std::size_t size) {
        if (failWrite || size > sizeof(bytes)) return 0;
        std::memcpy(bytes, source, size);
        length = size;
        present = true;
        return size;
    }
};