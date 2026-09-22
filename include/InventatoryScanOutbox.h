// Inventatory Scan R1 firmware outbox helpers.

#pragma once

#include <cstddef>
#include <cstdint>

namespace inventatory_scan {

// The persistent outbox header stores these values as one state transition.
// Callers can prepare a successor on a copy and publish it only after the
// corresponding NVS write succeeds.
struct OutboxQueueState {
  std::size_t head = 0;
  std::size_t count = 0;
  std::size_t tail = 0;
  uint32_t nextSequence = 1;
};

inline OutboxQueueState makeOutboxQueueState(std::size_t head, std::size_t count, std::size_t tail,
                                             uint32_t nextSequence) {
  OutboxQueueState state;
  state.head = head;
  state.count = count;
  state.tail = tail;
  state.nextSequence = nextSequence;
  return state;
}

inline bool prepareOutboxPop(const OutboxQueueState& current, std::size_t capacity,
                             OutboxQueueState& next) {
  if (capacity == 0 || current.count == 0 || current.count > capacity || current.head >= capacity ||
      current.tail >= capacity) {
    return false;
  }

  next = current;
  next.head = (current.head + 1U) % capacity;
  --next.count;
  if (next.count == 0) next.head = next.tail = 0;
  return true;
}

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

}  // namespace inventatory_scan
