// SPDX-FileCopyrightText: 2026 Paweł Kwiatkowski
// SPDX-License-Identifier: GPL-3.0-only

// Host stand-in for the Arduino core types used by the firmware's shared
// headers. It implements only the String members those headers call, with the
// results the Arduino core gives for them, so the host tests exercise the same
// logic that runs on the device.
#pragma once

#include <algorithm>
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
