//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// buffer_pool_manager.cpp
//
// Identification: src/buffer/buffer_pool_manager.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "buffer/buffer_pool_manager.h"
#include <iostream>
#include <memory>
#include <vector>

#include "common/exception.h"
#include "common/macros.h"
#include "storage/disk/disk_scheduler.h"
#include "storage/page/page.h"
#include "storage/page/page_guard.h"

namespace bustub {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k,
                                     LogManager *log_manager)
    : pool_size_(pool_size), disk_scheduler_(std::make_unique<DiskScheduler>(disk_manager)), log_manager_(log_manager) {
  // TODO(students): remove this line after you have implemented the buffer pool manager
  // throw NotImplementedException(
  //     "BufferPoolManager is not implemented yet. If you have finished implementing BPM, please remove the throw "
  //     "exception line in `buffer_pool_manager.cpp`.");

  // we allocate a consecutive memory space for the buffer pool
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);

  // Initially, every page is in the free list.
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<int>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::scoped_lock lock(latch_);

  if (free_list_.empty() && replacer_->Size() == 0) {
    *page_id = INVALID_PAGE_ID;
    return nullptr;
  }

  frame_id_t replacement_frame_id;
  if (!free_list_.empty()) {
    // find the replacement frame from the free list first
    // in this case the frame cannot be dirty
    replacement_frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    // find a replacement frame by evicting a victim from the LRU-K replacer
    bool evicted = replacer_->Evict(&replacement_frame_id);
    if (!evicted) {
      throw std::invalid_argument(std::string("failed to evict a frame from replacer."));
    }
    // if the frame is dirty, we need to write it back to disk
    if (pages_[replacement_frame_id].IsDirty()) {
      auto promise = disk_scheduler_->CreatePromise();
      auto future = promise.get_future();
      auto request = DiskRequest{true, pages_[replacement_frame_id].GetData(), pages_[replacement_frame_id].GetPageId(),
                                 std::move(promise)};
      disk_scheduler_->Schedule(std::move(request));
      future.get();
      pages_[replacement_frame_id].is_dirty_ = true;
    }
    // remove the mapping since this page is no longer in memory
    page_table_.erase(pages_[replacement_frame_id].GetPageId());
  }
  // get the new page id and update the mapping
  // between the page_id(on disk) and the frame_id(in memory)
  *page_id = AllocatePage();
  page_table_[*page_id] = replacement_frame_id;
  replacer_->RecordAccess(replacement_frame_id);

  // reset memory and the metadata
  pages_[replacement_frame_id].ResetMemory();
  pages_[replacement_frame_id].page_id_ = *page_id;
  pages_[replacement_frame_id].is_dirty_ = false;
  pages_[replacement_frame_id].pin_count_ = 0;

  // pin this frame by default for potential write
  replacer_->SetEvictable(replacement_frame_id, false);
  pages_[replacement_frame_id].pin_count_++;

  return &(pages_[replacement_frame_id]);
}

auto BufferPoolManager::FetchPage(page_id_t page_id, [[maybe_unused]] AccessType access_type) -> Page * {
  std::scoped_lock lock(latch_);

  auto it = page_table_.find(page_id);
  if (it != page_table_.end()) {
    // this page is already in memory
    // so we simply pin it
    frame_id_t frame_id = it->second;
    Page *page = &pages_[frame_id];
    replacer_->RecordAccess(frame_id);
    replacer_->SetEvictable(frame_id, false);
    pages_[frame_id].pin_count_++;
    return page;
  }

  // this page is not in memory so we need to find an available frame
  // and read the data from disk to the frame
  if (free_list_.empty() && replacer_->Size() == 0) {
    // if there is no replacement from free list or replacer
    return nullptr;
  }
  frame_id_t replacement_frame_id;
  if (!free_list_.empty()) {
    replacement_frame_id = free_list_.front();
    free_list_.pop_front();
  } else {
    bool evicted = replacer_->Evict(&replacement_frame_id);
    if (!evicted) {
      throw std::invalid_argument(std::string("failed to evict a frame from replacer."));
    }
    if (pages_[replacement_frame_id].IsDirty()) {
      auto promise = disk_scheduler_->CreatePromise();
      auto future = promise.get_future();
      auto request = DiskRequest{true, pages_[replacement_frame_id].GetData(), pages_[replacement_frame_id].GetPageId(),
                                 std::move(promise)};
      disk_scheduler_->Schedule(std::move(request));
      future.get();
      pages_[replacement_frame_id].is_dirty_ = false;
    }
    // remove the mapping since this page is no longer in memory
    page_table_.erase(pages_[replacement_frame_id].GetPageId());
  }

  // read the data from disk to memory, fill the frame with the new page
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  // DiskRequest r{false, pages_[replacement_frame_id].GetData(), page_id, std::move(promise)};
  auto request = DiskRequest{false, pages_[replacement_frame_id].GetData(), page_id, std::move(promise)};
  disk_scheduler_->Schedule(std::move(request));
  future.get();
  pages_[replacement_frame_id].is_dirty_ = false;
  pages_[replacement_frame_id].page_id_ = page_id;
  page_table_[page_id] = replacement_frame_id;

  // track this frame in the LRU-K replacer and pin it
  replacer_->RecordAccess(replacement_frame_id);
  replacer_->SetEvictable(replacement_frame_id, false);
  pages_[replacement_frame_id].pin_count_++;
  return &pages_[replacement_frame_id];
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty, [[maybe_unused]] AccessType access_type) -> bool {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  // return false if we cannot find this page in memory
  if (it == page_table_.end()) {
    return false;
  }
  auto frame_id = it->second;
  if (!pages_[frame_id].IsDirty() && is_dirty) {
    pages_[frame_id].is_dirty_ = true;
  }
  pages_[frame_id].pin_count_--;
  if (pages_[frame_id].GetPinCount() == 0) {
    replacer_->SetEvictable(frame_id, true);
  }
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  // return false if we cannot find this page in memory
  if (it == page_table_.end()) {
    return false;
  }
  auto frame_id = it->second;
  auto promise = disk_scheduler_->CreatePromise();
  auto future = promise.get_future();
  // DiskRequest r{true, pages_[frame_id].GetData(), pages_[frame_id].page_id_, std::move(promise)};
  auto request = DiskRequest{true, pages_[frame_id].GetData(), pages_[frame_id].GetPageId(), std::move(promise)};
  disk_scheduler_->Schedule(std::move(request));
  future.get();
  pages_[frame_id].is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::scoped_lock lock(latch_);
  std::vector<std::future<bool>> futures;
  for (auto & it : page_table_) {
    auto frame_id = it.second;
    auto page = &pages_[frame_id];
    auto promise = disk_scheduler_->CreatePromise();
    auto future = promise.get_future();
    auto request = DiskRequest{true, page->GetData(), page->GetPageId(), std::move(promise)};
    disk_scheduler_->Schedule(std::move(request));
    futures.push_back(std::move(future));
  }
  for (auto &future : futures) {
    future.get();
  }
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    // return true if this page is not in memory
    return true;
  }
  auto frame_id = it->second;
  if (pages_[frame_id].GetPinCount() > 0) {
    // return false if this page is pinned and cannot be deleted
    return false;
  }
  // stop tracking the frame in the replacer
  replacer_->Remove(frame_id);

  // clear this page from memory
  page_table_.erase(page_id);
  free_list_.push_back(frame_id);

  // reset the frame in the buffer
  pages_[frame_id].ResetMemory();
  pages_[frame_id].page_id_ = INVALID_PAGE_ID;
  pages_[frame_id].is_dirty_ = false;
  pages_[frame_id].pin_count_ = 0;
  DeallocatePage(page_id);
  return true;
}

auto BufferPoolManager::AllocatePage() -> page_id_t { return next_page_id_++; }

auto BufferPoolManager::FetchPageBasic(page_id_t page_id) -> BasicPageGuard {
  auto fetched_page = FetchPage(page_id);
  return BasicPageGuard(this, fetched_page);
}

auto BufferPoolManager::FetchPageRead(page_id_t page_id) -> ReadPageGuard {
  auto fetched_page = FetchPage(page_id);
  if (fetched_page != nullptr) {
    fetched_page->RLatch();
  }
  return ReadPageGuard(this, fetched_page);
}

auto BufferPoolManager::FetchPageWrite(page_id_t page_id) -> WritePageGuard {
  auto fetched_page = FetchPage(page_id);
  if (fetched_page != nullptr) {
    fetched_page->WLatch();
  }
  return WritePageGuard(this, fetched_page);
}

auto BufferPoolManager::NewPageGuarded(page_id_t *page_id) -> BasicPageGuard {
  auto new_page = NewPage(page_id);
  return BasicPageGuard{this, new_page};
}

}  // namespace bustub
