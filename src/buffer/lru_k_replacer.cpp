//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// lru_k_replacer.cpp
//
// Identification: src/buffer/lru_k_replacer.cpp
//
// Copyright (c) 2015-2022, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/lru_k_replacer.h"
#include <cstdint>
#include <list>
#include <mutex>
#include <stdexcept>
#include <string>
#include "common/config.h"
#include "common/exception.h"

namespace bustub {
LRUKNode::LRUKNode(frame_id_t fid, size_t k) : k_(k), fid_(fid) {}

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k) : replacer_size_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock<std::mutex> lock(latch_);
  // try to evict from secondary list first
  bool victim_found = false;
  if (!secondary_list_.empty()) {
    for (auto rit = secondary_list_.rbegin(); rit != secondary_list_.rend(); rit++) {
      auto it = node_store_.find(*rit);
      if (it == node_store_.end()) {
        throw std::invalid_argument(std::string("failed to find frame: ") + std::to_string(*rit));
      }
      auto node = it->second;
      if (!node.is_evictable_) {
        continue;
      }
      victim_found = true;
      *frame_id = node.fid_;
      secondary_list_.erase(std::next(rit).base());;
      node_store_.erase(it);
      break;
    }
  }
  // try primary list if we didn't find the victim from the secondary list
  if (!victim_found && !primary_list_.empty()) {
    for (auto rit = primary_list_.rbegin(); rit != primary_list_.rend(); rit++) {
      auto it = node_store_.find(*rit);
      if (it == node_store_.end()) {
        throw std::invalid_argument(std::string("failed to find frame: ") + std::to_string(*rit));
      }
      auto node = it->second;
      if (!node.is_evictable_) {
        continue;
      }
      victim_found = true;
      *frame_id = node.fid_;
      primary_list_.erase(std::next(rit).base());
      node_store_.erase(it);
      break;
    }
  }
  if (victim_found) {
    curr_size_--;
  }
  return victim_found;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id, [[maybe_unused]] AccessType access_type) {
  std::scoped_lock<std::mutex> lock(latch_);
  if (frame_id > static_cast<int>(replacer_size_)) {
    throw std::invalid_argument(std::string("Invalid frame id: ") + std::to_string(frame_id));
  }
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end()) {
    // cache doesn't have this frame id, put it to the secondary_list
    LRUKNode new_node{frame_id, k_};
    new_node.history_.emplace_back(current_timestamp_);
    secondary_list_.emplace_front(frame_id);
    new_node.position_ = secondary_list_.begin();
    node_store_.emplace(frame_id, new_node);
  } else {
    auto node = it->second;
    node.history_.emplace_back(current_timestamp_);
    auto access_count = node.history_.size();
    if (access_count < k_) {
      // hit_count < k-1, keep it in secondary_list, but move it to the list front
      secondary_list_.erase(node.position_);
      secondary_list_.emplace_front(frame_id);
      node.position_ = secondary_list_.begin();
    } else if (access_count == k_) {
      // hit_count == k-1, promote it from secondary_list to primary_list
      secondary_list_.erase(node.position_);
      primary_list_.emplace_front(frame_id);
      node.position_ = primary_list_.begin();
    } else {
      // hit_count >= k, keep it in primary_list, but move it to the list front
      node.history_.pop_front();
      primary_list_.erase(node.position_);
      primary_list_.emplace_front(frame_id);
      node.position_ = primary_list_.begin();
    }
    node_store_.erase(it);
    node_store_.emplace(frame_id, node);
  }
  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  auto it = node_store_.find(frame_id);
  if (it == node_store_.end()) {
    return;
  }
  auto node = it->second;
  if (!node.is_evictable_ && set_evictable) {
    curr_size_++;
  } else if (node.is_evictable_ && !set_evictable) {
    curr_size_--;
  }
  node.is_evictable_ = set_evictable;
  node_store_.erase(it);
  node_store_.emplace(frame_id, node);
}

void LRUKReplacer::Remove(frame_id_t frame_id) {}

auto LRUKReplacer::Size() -> size_t {
    return curr_size_;
}

}  // namespace bustub
