/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Based on the Quic implementation in Chromium.

#ifndef MODULES_CONGESTION_CONTROLLER_BBR_PACKET_NUMBER_INDEXED_QUEUE_H_
#define MODULES_CONGESTION_CONTROLLER_BBR_PACKET_NUMBER_INDEXED_QUEUE_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <type_traits>
#include <utility>

#include "rtc_base/checks.h"

namespace webrtc {
namespace bbr {

// PacketNumberIndexedQueue 是一个主要由连续编号的条目组成的队列，
// 支持以下操作：
// - 向队列末尾添加元素，或在末尾之后的某个位置添加
// - 以任意顺序移除元素
// - 检索元素
// 如果所有元素都按顺序插入，上述所有操作的时间复杂度都是摊销 O(1)。
//
// 在内部，数据结构是一个双端队列，其中每个元素都标记为存在或不存在。
// 双端队列从最低的存在索引开始。每当移除一个元素时，它会被标记为不存在，
// 并且双端队列的前端会清除所有不存在的元素。
//
// 由于假设条目是按顺序插入的，队列的尾部不会被清除，尽管移除队列中的所有元素
// 会将其恢复到初始状态。
//
// 注意，这个数据结构本质上是危险的，因为仅添加两个条目就会导致它消耗所有可用的内存。
// 因此，它不是一个通用容器，不应该被当作一个通用容器使用。
template <typename T>
class PacketNumberIndexedQueue {
 public:
  PacketNumberIndexedQueue()
      : number_of_present_entries_(0), first_packet_(0) {}

  // 检索与包号关联的条目。成功时返回条目的指针，如果条目不存在则返回 nullptr。
  T* GetEntry(int64_t packet_number);
  const T* GetEntry(int64_t packet_number) const;

  // 将与 |packet_number| 关联的数据插入到队列的末尾（或末尾之后），
  // 必要时填充缺失的中间条目。如果元素已成功插入，则返回 true；
  // 如果元素已在队列中或插入顺序错误，则返回 false。
  template <typename... Args>
  bool Emplace(int64_t packet_number, Args&&... args);

  // 移除与 |packet_number| 关联的数据，并根据需要释放队列中的槽位。
  bool Remove(int64_t packet_number);

  bool IsEmpty() const { return number_of_present_entries_ == 0; }

  // 返回队列中的条目数量。
  size_t number_of_present_entries() const {
    return number_of_present_entries_;
  }

  // 返回底层双端队列中分配的条目数量。这与队列的内存使用量成正比。
  size_t entry_slots_used() const { return entries_.size(); }

  // 队列中第一个条目的包号。如果队列为空，则为零。
  int64_t first_packet() const { return first_packet_; }

  // 队列中最后一个插入的条目的包号。注意，该条目可能已经被移除。如果队列为空，则为零。
  int64_t last_packet() const {
    if (IsEmpty()) {
      return 0;
    }
    return first_packet_ + entries_.size() - 1;
  }

 private:
  // 用于标记条目是否实际在映射中的 T 的包装器。
  struct EntryWrapper {
    T data;
    bool present;

    EntryWrapper() : data(), present(false) {}

    template <typename... Args>
    explicit EntryWrapper(Args&&... args)
        : data(std::forward<Args>(args)...), present(true) {}
  };

  // 在移除元素后清理前端未使用的槽位。
  void Cleanup();

  const EntryWrapper* GetEntryWrapper(int64_t offset) const;
  EntryWrapper* GetEntryWrapper(int64_t offset) {
    const auto* const_this = this;
    return const_cast<EntryWrapper*>(const_this->GetEntryWrapper(offset));
  }

  std::deque<EntryWrapper> entries_;
  size_t number_of_present_entries_;
  int64_t first_packet_;
};

template <typename T>
T* PacketNumberIndexedQueue<T>::GetEntry(int64_t packet_number) {
  EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return nullptr;
  }
  return &entry->data;
}

template <typename T>
const T* PacketNumberIndexedQueue<T>::GetEntry(int64_t packet_number) const {
  const EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return nullptr;
  }
  return &entry->data;
}

template <typename T>
template <typename... Args>
bool PacketNumberIndexedQueue<T>::Emplace(int64_t packet_number,
                                          Args&&... args) {
  if (IsEmpty()) {
    RTC_DCHECK(entries_.empty());
    RTC_DCHECK_EQ(0u, first_packet_);

    entries_.emplace_back(std::forward<Args>(args)...);
    number_of_present_entries_ = 1;
    first_packet_ = packet_number;
    return true;
  }

  // 不允许乱序插入。
  if (packet_number <= last_packet()) {
    return false;
  }

  // 处理可能缺失的元素。
  int64_t offset = packet_number - first_packet_;
  if (offset > static_cast<int64_t>(entries_.size())) {
    entries_.resize(offset);
  }

  number_of_present_entries_++;
  entries_.emplace_back(std::forward<Args>(args)...);
  RTC_DCHECK_EQ(packet_number, last_packet());
  return true;
}

template <typename T>
bool PacketNumberIndexedQueue<T>::Remove(int64_t packet_number) {
  EntryWrapper* entry = GetEntryWrapper(packet_number);
  if (entry == nullptr) {
    return false;
  }
  entry->present = false;
  number_of_present_entries_--;

  if (packet_number == first_packet()) {
    Cleanup();
  }
  return true;
}

template <typename T>
void PacketNumberIndexedQueue<T>::Cleanup() {
  while (!entries_.empty() && !entries_.front().present) {
    entries_.pop_front();
    first_packet_++;
  }
  if (entries_.empty()) {
    first_packet_ = 0;
  }
}

template <typename T>
auto PacketNumberIndexedQueue<T>::GetEntryWrapper(int64_t offset) const
    -> const EntryWrapper* {
  if (offset < first_packet_) {
    return nullptr;
  }

  offset -= first_packet_;
  if (offset >= static_cast<int64_t>(entries_.size())) {
    return nullptr;
  }

  const EntryWrapper* entry = &entries_[offset];
  if (!entry->present) {
    return nullptr;
  }

  return entry;
}

}  // namespace bbr
}  // namespace webrtc

#endif  // MODULES_CONGESTION_CONTROLLER_BBR_PACKET_NUMBER_INDEXED_QUEUE_H_
