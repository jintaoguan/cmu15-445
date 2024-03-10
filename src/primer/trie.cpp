#include "primer/trie.h"
#include <string_view>
#include <vector>
#include "common/exception.h"

namespace bustub {

template <class T>
auto Trie::Get(std::string_view key) const -> const T * {
  // You should walk through the trie to find the node corresponding to the key. If the node doesn't exist, return
  // nullptr. After you find the node, you should use `dynamic_cast` to cast it to `const TrieNodeWithValue<T> *`. If
  // dynamic_cast returns `nullptr`, it means the type of the value is mismatched, and you should return nullptr.
  // Otherwise, return the value.

  if (this->root_ == nullptr) {
    return nullptr;
  }
  auto cur = this->root_;
  for (char ch : key) {
    auto children = cur->children_;
    if (children.find(ch) != children.end()) {
      cur = children.find(ch)->second;
    } else {
      return nullptr;
    }
  }
  if (cur->is_value_node_) {
    auto node = std::dynamic_pointer_cast<const TrieNodeWithValue<T>>(cur);
    if (node == nullptr) {
      return nullptr;
    }
    return node->value_.get();
  }
  return nullptr;
}

template <class T>
auto Trie::Put(std::string_view key, T value) const -> Trie {
  // Note that `T` might be a non-copyable type. Always use `std::move` when creating `shared_ptr` on that value.
  // You should walk through the trie and create new nodes if necessary. If the node corresponding to the key already
  // exists, you should create a new `TrieNodeWithValue`.

  auto v = std::make_shared<T>(std::move(value));
  // handle the empty key
  if (key.empty()) {
    std::shared_ptr<const TrieNode> new_root;
    if (this->root_ != nullptr) {
      new_root = std::make_shared<TrieNodeWithValue<T>>(root_->children_, v);
    } else {
      new_root = std::make_shared<TrieNodeWithValue<T>>(v);
    }
    return Trie(new_root);
  }

  // handle the empty Trie
  if (this->root_ == nullptr) {
    auto new_root = std::make_shared<TrieNode>();
    auto curr = new_root;
    for (size_t i = 0; i < key.size(); ++i) {
      char ch = key[i];
      std::shared_ptr<TrieNode> child;
      if (i != key.size() - 1) {
        child = std::make_shared<TrieNode>();
      } else {
        child = std::make_shared<TrieNodeWithValue<T>>(v);
      }
      curr->children_ = std::map<char, std::shared_ptr<const TrieNode>>();
      curr->children_[ch] = child;
      curr = child;
    }
    return Trie{new_root};
  }

  std::vector<std::shared_ptr<const TrieNode>> nodes;
  auto cur = this->root_;
  auto it = key.begin();
  while (it != key.end()) {
    auto child = cur->children_.find(*it);
    if (child == cur->children_.end()) {
      break;
    }
    cur = child->second;
    nodes.push_back(cur);
    ++it;
  }

  auto cloned_root = this->root_->Clone();
  auto new_root = std::shared_ptr<TrieNode>(std::move(cloned_root));
  auto parent = new_root;

  if (it == key.end()) {
    for (size_t i = 0; i < key.size(); ++i) {
      char ch = key[i];
      std::shared_ptr<TrieNode> new_node = nullptr;
      if (i != key.size() - 1) {
        new_node = std::shared_ptr<TrieNode>(nodes[i]->Clone());
      } else {
        new_node = std::make_shared<TrieNodeWithValue<T>>(nodes[i]->children_, v);
      }
      parent->children_[ch] = new_node;
      parent = new_node;
    }
    return Trie{new_root};
  }

  auto index = 0;
  for (size_t i = 0; i < nodes.size(); ++i) {
    char ch = key[i];
    auto new_node = std::shared_ptr<TrieNode>(nodes[i]->Clone());
    parent->children_[ch] = new_node;
    parent = new_node;
    index++;
  }
  while (index < static_cast<int>(key.size()) - 1) {
    char ch = key[index];
    auto child = std::make_shared<TrieNode>();
    parent->children_[ch] = child;
    parent = child;
    index++;
  }
  parent->children_[key[key.size() - 1]] = std::make_shared<TrieNodeWithValue<T>>(v);
  return Trie{new_root};
}

auto Trie::Remove(std::string_view key) const -> Trie {
  // You should walk through the trie and remove nodes if necessary. If the node doesn't contain a value any more,
  // you should convert it to `TrieNode`. If a node doesn't have children any more, you should remove it.

  if (this->root_ == nullptr) {
    return {};
  }
  std::vector<std::shared_ptr<const TrieNode>> nodes;
  auto cur = this->root_;
  for (char ch : key) {
    nodes.push_back(cur);
    auto child = cur->children_.find(ch);
    if (child == cur->children_.end()) {
      return Trie(root_);
    }
    cur = child->second;
  }
  if (!cur->is_value_node_) {
    return Trie(root_);
  }

  cur = std::make_shared<const TrieNode>(cur->children_);
  for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; i--) {
    char ch = key[i];
    auto new_parent = std::shared_ptr<TrieNode>(nodes[i]->Clone());
    if (cur->children_.empty() && !cur->is_value_node_) {
      new_parent->children_.erase(ch);
    } else {
      new_parent->children_[ch] = cur;
    }
    cur = new_parent;
  }
  if (cur->children_.empty() && !cur->is_value_node_) {
    return {};
  }
  return Trie(cur);
}

// Below are explicit instantiation of template functions.
//
// Generally people would write the implementation of template classes and functions in the header file. However, we
// separate the implementation into a .cpp file to make things clearer. In order to make the compiler know the
// implementation of the template functions, we need to explicitly instantiate them here, so that they can be picked up
// by the linker.

template auto Trie::Put(std::string_view key, uint32_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint32_t *;

template auto Trie::Put(std::string_view key, uint64_t value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const uint64_t *;

template auto Trie::Put(std::string_view key, std::string value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const std::string *;

// If your solution cannot compile for non-copy tests, you can remove the below lines to get partial score.

using Integer = std::unique_ptr<uint32_t>;

template auto Trie::Put(std::string_view key, Integer value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const Integer *;

template auto Trie::Put(std::string_view key, MoveBlocked value) const -> Trie;
template auto Trie::Get(std::string_view key) const -> const MoveBlocked *;

}  // namespace bustub
