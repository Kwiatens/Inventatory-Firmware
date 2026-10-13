// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the Arduino core types used by the firmware's shared
// headers. It implements only the String members those headers call, with the
// results the Arduino core gives for them, so the host tests exercise the same
// logic that runs on the device.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

// Arduino defines min() and max() as macros that accept mixed operand types.
// A template keeps the same call sites compiling without macro side effects.
template <typename A, typename B>
constexpr auto min(A a, B b) -> decltype(a < b ? a : b) {
  return a < b ? a : b;
}

template <typename A, typename B>
constexpr auto max(A a, B b) -> decltype(a > b ? a : b) {
  return a > b ? a : b;
}

class String {
 public:
  String() = default;
  String(const char* text) : data_(text != nullptr ? text : "") {}
  String(char value) : data_(1, value) {}
  String(int value) : data_(std::to_string(value)) {}
  String(unsigned int value) : data_(std::to_string(value)) {}
  String(long value) : data_(std::to_string(value)) {}
  String(unsigned long value) : data_(std::to_string(value)) {}
  String(long long value) : data_(std::to_string(value)) {}
  String(unsigned long long value) : data_(std::to_string(value)) {}

  String& operator+=(const String& other) {
    data_ += other.data_;
    return *this;
  }
  String& operator+=(const char* text) {
    data_ += text != nullptr ? text : "";
    return *this;
  }
  String& operator+=(char value) {
    data_ += value;
    return *this;
  }

  friend String operator+(const String& left, const String& right) {
    String result(left);
    result += right;
    return result;
  }
  friend String operator+(const String& left, const char* right) {
    String result(left);
    result += right;
    return result;
  }
  friend String operator+(const char* left, const String& right) {
    String result(left);
    result += right;
    return result;
  }
  friend String operator+(const String& left, char right) {
    String result(left);
    result += right;
    return result;
  }
  friend String operator+(char left, const String& right) {
    String result(left);
    result += right;
    return result;
  }

  friend bool operator==(const String& left, const String& right) { return left.data_ == right.data_; }
  friend bool operator!=(const String& left, const String& right) { return !(left == right); }
  friend bool operator<(const String& left, const String& right) { return left.data_ < right.data_; }
  friend bool operator>(const String& left, const String& right) { return right < left; }
  friend bool operator<=(const String& left, const String& right) { return !(right < left); }
  friend bool operator>=(const String& left, const String& right) { return !(left < right); }

  unsigned int length() const { return static_cast<unsigned int>(data_.size()); }
  const char* c_str() const { return data_.c_str(); }

  // Arduino does not bounds-check operator[]. The host build does, so an
  // out-of-range index fails loudly instead of reading past the buffer.
  char& operator[](unsigned int index) { return data_.at(index); }
  char operator[](unsigned int index) const { return data_.at(index); }
  void setCharAt(unsigned int index, char value) {
    if (index < length()) data_[index] = value;
  }

  void reserve(unsigned int size) { data_.reserve(size); }

  // Arduino semantics: remove(index) truncates at index; remove(index, count)
  // deletes up to count characters starting at index.
  void remove(unsigned int index) {
    if (index < length()) data_.resize(index);
  }
  void remove(unsigned int index, unsigned int count) {
    if (index >= length()) return;
    data_.erase(index, count);
  }

  void clear() { data_.clear(); }

  // Arduino semantics: trim() removes leading and trailing whitespace in place.
  void trim() {
    std::size_t begin = 0;
    while (begin < data_.size() && std::isspace(static_cast<unsigned char>(data_[begin])) != 0) ++begin;
    std::size_t end = data_.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(data_[end - 1])) != 0) --end;
    data_ = data_.substr(begin, end - begin);
  }

  bool equalsIgnoreCase(const String& other) const {
    if (length() != other.length()) return false;
    for (std::size_t index = 0; index < data_.size(); ++index) {
      if (std::tolower(static_cast<unsigned char>(data_[index])) !=
          std::tolower(static_cast<unsigned char>(other.data_[index]))) {
        return false;
      }
    }
    return true;
  }

  int indexOf(char value, unsigned int fromIndex = 0) const {
    if (fromIndex >= length()) return -1;
    const auto found = data_.find(value, fromIndex);
    return found == std::string::npos ? -1 : static_cast<int>(found);
  }

  int indexOf(const String& needle, unsigned int fromIndex = 0) const {
    if (fromIndex >= length()) return -1;
    const auto found = data_.find(needle.data_, fromIndex);
    return found == std::string::npos ? -1 : static_cast<int>(found);
  }

  bool startsWith(const String& prefix) const {
    return prefix.length() <= length() && data_.compare(0, prefix.length(), prefix.data_) == 0;
  }

  // Arduino clamps the end index to the string length and returns an empty
  // String when the start index is at or past the end.
  String substring(unsigned int beginIndex) const { return substring(beginIndex, length()); }
  String substring(unsigned int beginIndex, unsigned int endIndex) const {
    endIndex = std::min(endIndex, length());
    if (beginIndex >= endIndex) return String();
    String result;
    result.data_ = data_.substr(beginIndex, endIndex - beginIndex);
    return result;
  }

  long toInt() const { return std::atol(data_.c_str()); }

  // Copies at most bufferSize - 1 characters and always terminates the buffer.
  unsigned int getBytes(unsigned char* buffer, unsigned int bufferSize) const {
    if (buffer == nullptr || bufferSize == 0) return 0;
    const auto count = std::min<std::size_t>(data_.size(), bufferSize - 1U);
    std::memcpy(buffer, data_.data(), count);
    buffer[count] = '\0';
    return static_cast<unsigned int>(count);
  }

 private:
  std::string data_;
};

// ---- Host stand-ins for the timing, serial and IP helpers the network code uses. ----
// The clock is fake: it only moves when delay() is called, so timeouts are deterministic.
inline unsigned long& hostClockMs() {
  static unsigned long now = 10000UL;
  return now;
}
inline unsigned long millis() { return hostClockMs(); }
inline void delay(unsigned long ms) { hostClockMs() += ms; }
inline void delayMicroseconds(unsigned int) {}

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}
  String toString() const {
    return String(static_cast<int>(bytes_[0])) + "." + String(static_cast<int>(bytes_[1])) + "." +
           String(static_cast<int>(bytes_[2])) + "." + String(static_cast<int>(bytes_[3]));
  }
  bool operator==(const IPAddress& other) const { return bytes_[0] == other.bytes_[0] && bytes_[1] == other.bytes_[1] && bytes_[2] == other.bytes_[2] && bytes_[3] == other.bytes_[3]; }

 private:
  uint8_t bytes_[4]{};
};

// Serial output is discarded on the host. The firmware never logs secrets (AGENTS.md
// invariant 1), and the host tests check behaviour, not log text.
class HostSerial {
 public:
  void begin(unsigned long) {}
  void print(const char*) {}
  void print(char) {}
  void print(int) {}
  void print(unsigned int) {}
  void print(unsigned long) {}
  void print(const String&) {}
  void print(const IPAddress&) {}
  void println() {}
  void println(const char*) {}
  void println(char) {}
  void println(int) {}
  void println(unsigned int) {}
  void println(unsigned long) {}
  void println(const String&) {}
  void println(const IPAddress&) {}
  void printf(const char*, ...) {}
};
inline HostSerial Serial;
