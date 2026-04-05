#ifndef ARDUINO_COMPAT_H
#define ARDUINO_COMPAT_H

#include <string>
#include <vector>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// millis() — milliseconds since boot
inline uint32_t millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// delay() — block for ms milliseconds
inline void delay(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// Arduino-compatible String class
class String : public std::string {
public:
    // Constructors
    String() : std::string() {}
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
    String(std::string&& s) : std::string(std::move(s)) {}
    explicit String(char c) : std::string(1, c) {}
    explicit String(int v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", v);
        assign(buf);
    }
    explicit String(unsigned int v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%u", v);
        assign(buf);
    }
    explicit String(long v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", v);
        assign(buf);
    }
    explicit String(unsigned long v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lu", v);
        assign(buf);
    }
    // Implicit conversion to const char* so LovyanGFX print() works
    operator const char*() const { return c_str(); }

    // Arduino methods
    bool isEmpty() const { return empty(); }

    String substring(size_t from) const {
        if (from >= size()) return String();
        return String(substr(from));
    }

    String substring(size_t from, size_t to) const {
        if (from >= size()) return String();
        if (to > size()) to = size();
        if (to <= from) return String();
        return String(substr(from, to - from));
    }

    int indexOf(char c, size_t from = 0) const {
        size_t pos = find(c, from);
        return (pos == npos) ? -1 : (int)pos;
    }

    int indexOf(const String& s, size_t from = 0) const {
        size_t pos = find(s, from);
        return (pos == npos) ? -1 : (int)pos;
    }

    int lastIndexOf(char c) const {
        size_t pos = rfind(c);
        return (pos == npos) ? -1 : (int)pos;
    }

    int lastIndexOf(const String& s) const {
        size_t pos = rfind(s);
        return (pos == npos) ? -1 : (int)pos;
    }

    bool startsWith(const char* prefix) const {
        if (!prefix) return false;
        return compare(0, strlen(prefix), prefix) == 0;
    }

    bool startsWith(const String& prefix) const {
        return compare(0, prefix.size(), prefix) == 0;
    }

    int toInt() const { return atoi(c_str()); }
    float toFloat() const { return (float)atof(c_str()); }

    // operator+ returning String
    String operator+(const String& rhs) const {
        return String(std::string(*this) + std::string(rhs));
    }
    String operator+(const char* rhs) const {
        return String(std::string(*this) + (rhs ? rhs : ""));
    }
    String operator+(char rhs) const {
        return String(std::string(*this) + rhs);
    }
    String operator+(int rhs) const {
        return *this + String(rhs);
    }
    String operator+(unsigned int rhs) const {
        return *this + String(rhs);
    }
    // operator+=
    String& operator+=(const String& rhs) {
        std::string::operator+=(rhs);
        return *this;
    }
    String& operator+=(const char* rhs) {
        if (rhs) std::string::operator+=(rhs);
        return *this;
    }
    String& operator+=(char rhs) {
        std::string::operator+=(rhs);
        return *this;
    }
    String& operator+=(int rhs) {
        return *this += String(rhs);
    }

    // operator== / operator!=
    bool operator==(const char* rhs) const {
        return std::string::compare(rhs ? rhs : "") == 0;
    }
    bool operator==(const String& rhs) const {
        return compare(rhs) == 0;
    }
    bool operator!=(const char* rhs) const {
        return !(*this == rhs);
    }
    bool operator!=(const String& rhs) const {
        return !(*this == rhs);
    }
};

// Non-member operator+ for (const char* + String)
inline String operator+(const char* a, const String& b) {
    return String(std::string(a ? a : "") + std::string(b));
}

#endif // ARDUINO_COMPAT_H
