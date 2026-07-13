// HIMS Scan R1 firmware outbox helpers.

#pragma once

#include <cstddef>

namespace hims_scan {

template <typename T, std::size_t Capacity>
class FixedRingBuffer {
 public:
  bool empty() const {
    return size_ == 0;
  }

  bool full() const {
    return size_ == Capacity;
  }

  std::size_t size() const {
    return size_;
  }

  const T* front() const {
    return empty() ? nullptr : &items_[head_];
  }

  bool push(const T& value) {
    if (Capacity == 0) {
      return false;
    }
    if (full()) {
      items_[head_] = value;
      head_ = (head_ + 1U) % Capacity;
      tail_ = head_;
      return true;
    }
    items_[tail_] = value;
    tail_ = (tail_ + 1U) % Capacity;
    ++size_;
    return true;
  }

  bool pop(T& value) {
    if (empty()) {
      return false;
    }
    value = items_[head_];
    head_ = (head_ + 1U) % Capacity;
    --size_;
    return true;
  }

  void clear() {
    head_ = 0;
    tail_ = 0;
    size_ = 0;
  }

 private:
  T items_[Capacity]{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::size_t size_ = 0;
};

}  // namespace hims_scan
