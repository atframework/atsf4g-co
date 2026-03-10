#pragma once

#include <algorithm>
#include <cassert>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <stack>
#include <vector>

#include "memory/rc_ptr.h"

template <typename Type, typename Alloc>
class persistent_btree;

template <typename Type>
struct btree_node {
  using value_type = Type;
  using btree_node_pointer = util::memory::strong_rc_ptr<btree_node<Type>>;

  bool is_leaf() { return leaf_; }

  int32_t last_child_pos() { return static_cast<int32_t>(keys.size()); }
  int32_t start_child_pos() { return 0; }

  btree_node_pointer start_child() {
    if (!is_leaf()) {
      return children[0];
    }
    return nullptr;
  }

  btree_node_pointer last_child() {
    if (!is_leaf()) {
      return children[children.size() - 1];
    }
    return nullptr;
  }

  size_t get_subtree_sz(size_t pos) {
    if (!is_leaf()) {
      return children[pos]->tree_sz_;
    }
    return 0;
  }

  void recul_tree_sz() {
    tree_sz_ = keys.size();
    for (const auto& unit : children) {
      tree_sz_ += unit->tree_sz_;
    }
  }

  btree_node(bool leaf, size_t sz) : leaf_(leaf), tree_sz_(0) {
    keys.reserve(sz);
    children.reserve(sz);
  }

  btree_node(const util::memory::strong_rc_ptr<btree_node<value_type>>& node, size_t sz) {
    if (!node) {
      return;
    }
    keys.reserve(sz);
    children.reserve(sz);
    leaf_ = node->leaf_;
    tree_sz_ = node->tree_sz_;
    keys.assign(node->keys.begin(), node->keys.end());
    children.assign(node->children.begin(), node->children.end());
  }
  // friend class btree_iterator;

  bool leaf_;
  size_t tree_sz_;  // 子树key大小
  std::vector<util::memory::strong_rc_ptr<value_type>> keys;
  std::vector<util::memory::strong_rc_ptr<btree_node<value_type>>> children;
};

template <typename Node>
struct btree_iterator {
  using iterator_category = std::bidirectional_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = typename Node::value_type;
  using const_node = const Node;
  using btree_node_pointer = util::memory::strong_rc_ptr<Node>;
  using iterator = btree_iterator<Node>;
  using const_iterator = btree_iterator<const Node>;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;
  using const_btree_node_pointer = const btree_node_pointer;

  btree_iterator() : node_(nullptr), position_(0), path_(), root_(nullptr) {}
  btree_iterator(btree_node_pointer node, int32_t pos, btree_node_pointer root,
                 std::stack<std::pair<btree_node_pointer, size_t>>&& path)
      : node_(node), position_(pos), path_(path), root_(root) {}

  btree_iterator(btree_node_pointer node, size_t pos, btree_node_pointer root,
                 std::stack<std::pair<btree_node_pointer, size_t>>&& path)
      : node_(node), position_(static_cast<int32_t>(pos)), path_(path), root_(root) {}

  const_btree_node_pointer get_root() { return root_; }

  btree_iterator& operator++() {
    increment();
    return *this;
  }
  btree_iterator& operator--() {
    decrement();
    return *this;
  }

  btree_iterator operator++(int) {
    btree_iterator tmp = *this;
    ++*this;
    return tmp;
  }
  btree_iterator operator--(int) {
    btree_iterator tmp = *this;
    --*this;
    return tmp;
  }

  const_reference operator*() const { return *node_->keys[static_cast<size_t>(position_)]; }
  const_pointer operator->() const { return node_->keys[static_cast<size_t>(position_)].get(); }

  bool operator==(const iterator& x) const {
    return node_ == x.node_ && position_ == x.position_ && path_ == x.path_ && root_ == x.root_;
  }

  bool operator!=(const iterator& x) const {
    return node_ != x.node_ || position_ != x.position_ || path_ != x.path_ || root_ != x.root_;
  }

  void increment() {
    if (node_->is_leaf() && ++position_ < node_->last_child_pos()) {
      return;
    }
    increment_slow();
  }

  void decrement() {
    if (node_->is_leaf() && --position_ >= 0) {
      return;
    }
    decrement_slow();
  }

  void increment_slow() {
    if (node_->is_leaf()) {
      assert(position_ >= node_->last_child_pos());
      while (!path_.empty() && position_ == node_->last_child_pos()) {
        assert(path_.top().first->children[static_cast<size_t>(path_.top().second)] == node_);
        position_ = static_cast<int32_t>(path_.top().second);
        node_ = path_.top().first;
        path_.pop();
      }
    } else {
      assert(position_ < node_->last_child_pos());

      path_.push(std::make_pair(node_, position_ + 1));
      node_ = node_->children[static_cast<size_t>(position_ + 1)];

      while (!node_->is_leaf()) {
        path_.push(std::make_pair(node_, 0));
        node_ = node_->start_child();
      }
      position_ = 0;
    }
  }

  void decrement_slow() {
    if (node_->is_leaf()) {
      assert(position_ <= -1);
      while (!path_.empty() && position_ < node_->start_child_pos()) {
        assert(path_.top().first->children[static_cast<size_t>(path_.top().second)] == node_);
        position_ = path_.top().second - 1;
        node_ = path_.top().first;
        path_.pop();
      }
    } else {
      assert(position_ >= node_->start_child_pos());
      path_.push(std::make_pair(node_, position_));
      node_ = node_->children[static_cast<size_t>(position_)];

      while (!node_->is_leaf()) {
        path_.push(std::make_pair(node_, node_->last_child_pos()));
        node_ = node_->last_child();
      }
      position_ = node_->last_child_pos() - 1;
    }
  }

  difference_type distance(iterator first, iterator last) {
    difference_type n = 0;
    while (first != last) {
        ++first;
        ++n;
    }
    return n;
  }

 private:
  btree_node_pointer node_;
  int32_t position_;
  std::stack<std::pair<btree_node_pointer, size_t>> path_;
  btree_node_pointer root_;
};

template <typename Type, typename Alloc = std::allocator<Type>>
class btree_mirror {
 public:
  using value_type = Type;
  using allocator_type = Alloc;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;

  using node_type = btree_node<value_type>;
  using const_node = const node_type;
  using btree_node_pointer = util::memory::strong_rc_ptr<node_type>;
  using btree_pointer = util::memory::strong_rc_ptr<persistent_btree<Type, Alloc>>;

  using iterator = btree_iterator<node_type>;
  using const_iterator = btree_iterator<const node_type>;
  using const_btree_node_pointer = const btree_node_pointer;

 private:
  /* data */
 public:
  btree_mirror(btree_pointer tree, btree_node_pointer root) : tree_(tree), root_(root) {}

  iterator begin() {
    btree_node_pointer node = root_;
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    while (!node->is_leaf()) {
      path.push(std::make_pair(node, node->start_child_pos()));
      node = node->start_child();
    }
    return iterator(node, 0, root_, std::move(path));
  }
  iterator end() {
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    return iterator(root_, root_->last_child_pos(), root_, std::move(path));
  }
  iterator find(const value_type& target) {
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    return tree_->find_inner_(root_, target, root_, path);
  }
  iterator at(size_t pos) {
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    return tree_->at_inner_(root_, pos, root_, path);
  }
  iterator index(const value_type& key) {
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    return tree_->index_inner_(root_, key);
  }

  size_t size() { return root_->tree_sz_; }

  friend class persistent_btree<Type, Alloc>;

 private:
  btree_pointer tree_;
  btree_node_pointer root_;
};

template <typename Type, typename Alloc = std::allocator<Type>>
class persistent_btree : public util::memory::enable_shared_rc_from_this<persistent_btree<Type, Alloc>> {
 public:
  using value_type = Type;
  using allocator_type = Alloc;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;

  using node_type = btree_node<value_type>;
  using btree_node_pointer = util::memory::strong_rc_ptr<node_type>;
  using const_btree_node_pointer = const util::memory::strong_rc_ptr<node_type>;

  using value_pointer = util::memory::strong_rc_ptr<value_type>;
  using iterator = btree_iterator<node_type>;
  using const_iterator = btree_iterator<const node_type>;
  using mirror_type = btree_mirror<Type, Alloc>;
  using mirror_pointer = util::memory::strong_rc_ptr<mirror_type>;

  using compare_fn_t = std::function<bool(const value_type& l, const value_type& r)>;

  persistent_btree() : T(20), max_root_version_size_(10) {
    root_ = allocator_new_node(true);
    compare_fn_ = std::less<value_type>();
  }

  persistent_btree(size_t degree, size_t max_version_sz, compare_fn_t compare_fn)
      : T(degree), max_root_version_size_(max_version_sz), compare_fn_(compare_fn) {
    root_ = allocator_new_node(true);
  }

  iterator begin() {
    btree_node_pointer node = root_;
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    while (!node->is_leaf()) {
      path.push(std::make_pair(node, node->start_child_pos()));
      node = node->start_child();
    }
    return iterator(node, 0, root_, std::move(path));
  }

  iterator end() {
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    return iterator(root_, root_->last_child_pos(), root_, std::move(path));
  }

  iterator find(const value_type& target) {
    std::stack<std::pair<btree_node_pointer, int32_t>> path;
    return find_inner_(root_, target, root_, path);
  }

  iterator find(btree_node_pointer root, const value_type& target) {
    std::stack<std::pair<btree_node_pointer, int32_t>> path;
    return find_inner_(root, target, root, path);
  }

  iterator erase(iterator iter) {
    assert(iter.get_root() == root_);
    iterator innternal_iter(iter);
    iter++;

    if (iter == end()) {
      erase(*innternal_iter);
      return end();
    }
    value_type value = *iter;
    erase(*innternal_iter);

    // 根节点变化，导致只能重新获取
    return find(value);
  }

  iterator at(size_t pos) {
    std::stack<std::pair<btree_node_pointer, size_t>> path;
    return at_inner_(root_, pos, root_, path);
  }

  void erase(const value_type& key) { root_ = remove(root_, key); }

  size_t index(const value_type& key) { return index_inner_(root_, key); }

  size_t size() { return root_->tree_sz_; }

  void pop() { erase(end()--); }

  const util::memory::strong_rc_ptr<value_type> get_min_key() { return get_min_key(root_); }

  mirror_pointer create_mirror() {
    using alloc_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<mirror_type>;
    auto ptr = this->shared_from_this();
    return util::memory::allocate_strong_rc<mirror_type>(alloc_type(), ptr, root_);
  }

  const util::memory::strong_rc_ptr<value_type> get_min_key(btree_node_pointer node) {
    if (!node) {
      return nullptr;
    }
    if (node->leaf_) {
      return node->keys.back();
    }
    return get_min_key(node->children.back());
  }

  void clear() {
    root_ = allocator_new_node(true);
    root_->leaf_ = true;
  }

  bool contains(const value_type& key) { return contains(root_, key); }

  bool value_comp(const value_type& l, const value_type& r) { return compare_fn_(l, r); }

  bool empty() { return root_->leaf_ && root_->keys.empty(); }

  void batch_query(size_t from, size_t sz, std::vector<util::memory::strong_rc_ptr<value_type>>& result) {
    batch_query(root_, from, sz, result);
  }

  void get_preorder_traversal(std::vector<util::memory::strong_rc_ptr<value_type>>& result) {
    return get_preorder_traversal(root_, result);
  }

  void get_postorder_traversal(std::vector<util::memory::strong_rc_ptr<value_type>>& result) {
    return get_postorder_traversal(root_, result);
  }

  void batch_query(btree_node_pointer node, size_t from, size_t sz,
                   std::vector<util::memory::strong_rc_ptr<value_type>>& result) {
    if (!node || sz == result.size()) {
      return;
    }
    if (node->leaf_) {
      for (size_t i = 0; i < node->keys.size(); ++i) {
        if (i + 1 >= from) {
          result.push_back(node->keys[i]);
        }
        if (result.size() == sz) {
          return;
        }
      }
      return;
    }
    size_t start_pos = from;
    for (size_t i = 0; i < node->children.size(); ++i) {
      if (node->children[i]->tree_sz_ >= start_pos) {
        batch_query(node->children[i], start_pos, sz, result);
      }
      if (sz == result.size() || i == node->children.size() - 1) {
        return;
      }
      if (start_pos >= node->children[i]->tree_sz_) {
        start_pos -= node->children[i]->tree_sz_;
      } else {
        start_pos = 0;
      }
      if (start_pos <= 1) {
        result.push_back(node->keys[i]);
      }
      if (start_pos >= 1) {
        start_pos--;
      }
    }
    return;
  }

  void insert(const value_type& key) { root_ = insert_not_full(root_, key); }

  void traverse(btree_node_pointer btree_node, int32_t level = 1) {
    if (!btree_node) return;
    size_t i;
    for (i = 0; i < btree_node->keys.size(); i++) {
      // std::cout << " " << *btree_node->keys[i];
    }
    // std::cout << "] " << std::endl;

    if (!btree_node->leaf_) {
      for (i = 0; i < btree_node->children.size(); i++) traverse(btree_node->children[i], level + 1);
    }
  }

  void traverse_leaf(btree_node_pointer btree_node, int32_t level = 1) {
    if (!btree_node) return;
    size_t i;
    // std::cout << "level: " << level << "[ ";
    //  for (i = 0; i < btree_node->keys.size(); i++) {
    //    //std::cout << " " << *btree_node->keys[i];
    //  }
    // std::cout << " " << (btree_node->leaf_ ? 1 : 0) << " " << btree_node->children.size() << " "
    // << btree_node->keys.size();
    // std::cout << "] " << std::endl;

    if (!btree_node->leaf_) {
      for (i = 0; i < btree_node->children.size(); i++) traverse_leaf(btree_node->children[i], level + 1);
    }
  }

  void get_preorder_traversal(btree_node_pointer node, std::vector<util::memory::strong_rc_ptr<value_type>>& result) {
    if (!node) {
      return;
    }
    if (node->leaf_) {
      for (size_t i = 0; i < node->keys.size(); ++i) {
        result.push_back(node->keys[i]);
      }
      return;
    }
    for (size_t i = 0; i < node->keys.size(); ++i) {
      get_preorder_traversal(node->children[i], result);
      result.push_back(node->keys[i]);
    }
    get_preorder_traversal(node->children.back(), result);
  }

  const_btree_node_pointer get_root() { return root_; }

 private:
  void split_child(btree_node_pointer btree_node, size_t i) {
    assert(i < btree_node->children.size());

    auto y = btree_node->children[i];
    auto new_y = allocator_new_node(y);
    btree_node->children[i] = new_y;
    auto z = allocator_new_node(new_y->leaf_);
    size_t origin_tree_sz = btree_node->tree_sz_;

    btree_node->children.insert(btree_node->children.begin() + static_cast<long int>(i) + 1, z);
    btree_node->keys.insert(btree_node->keys.begin() + static_cast<long int>(i), new_y->keys[T - 1]);

    z->keys.assign(new_y->keys.begin() + static_cast<long int>(T), new_y->keys.end());
    new_y->keys.resize(T - 1);
    if (!new_y->leaf_) {
      z->children.assign(new_y->children.begin() + static_cast<long int>(T), new_y->children.end());
      new_y->children.resize(T);
    }
    // 更新子树大小
    z->recul_tree_sz();
    new_y->recul_tree_sz();
    btree_node->tree_sz_ = origin_tree_sz;
  }

  btree_node_pointer insert_not_full(btree_node_pointer btree_node, const value_type& key) {
    btree_node_pointer new_node = nullptr;

    if (btree_node->keys.size() == 2 * T - 1) {
      new_node = allocator_new_node(false);
      new_node->children.push_back(btree_node);
      new_node->tree_sz_ = btree_node->tree_sz_;
      split_child(new_node, 0);
    } else {
      new_node = allocator_new_node(btree_node);
    }

    if (new_node->leaf_) {
      if (new_node->keys.size() == 0) {
        // 为空直接插入
        new_node->keys.push_back(allocator_key_ptr(key));
        new_node->tree_sz_++;
        return new_node;
      }
      new_node->keys.push_back(nullptr);
      size_t i = new_node->keys.size() - 1;

      while (i > 0 && compare_fn_(key, *new_node->keys[i - 1])) {
        new_node->keys[i] = new_node->keys[i - 1];
        i--;
      }
      new_node->keys[i] = allocator_key_ptr(key);
      new_node->tree_sz_++;
      return new_node;
    }

    size_t i = binary_search(new_node->keys, key);
    if (new_node->children[i]->keys.size() == 2 * T - 1) {
      split_child(new_node, i);
      if (compare_fn_(*new_node->keys[i], key)) {
        i++;
      }
    }
    auto new_child = insert_not_full(new_node->children[i], key);
    new_node->children[i] = new_child;
    new_node->tree_sz_++;
    return new_node;
  }

  util::memory::strong_rc_ptr<value_type> get_predecessor(btree_node_pointer btree_node, size_t idx) {
    auto cur = btree_node->children[idx];
    while (!cur->leaf_) {
      cur = cur->children[cur->keys.size()];
    }
    assert(cur->keys.size() >= 1);
    return cur->keys[cur->keys.size() - 1];
  }

  util::memory::strong_rc_ptr<value_type> get_successor(btree_node_pointer btree_node, size_t idx) {
    assert(idx + 1 < btree_node->children.size());
    auto cur = btree_node->children[idx + 1];
    while (!cur->leaf_) {
      cur = cur->children[0];
    }
    return cur->keys[0];
  }

  void merge(btree_node_pointer parent, size_t idx) {
    assert(idx + 1 < parent->children.size());

    auto child = parent->children[idx];
    auto sibling = parent->children[idx + 1];

    // 创建子节点和兄弟节点的副本
    auto new_child = allocator_new_node(child);
    auto new_sibling = allocator_new_node(sibling);

    // 将中间键插入到子节点中
    new_child->keys.push_back(parent->keys[idx]);

    // 将兄弟节点的键和子节点合并到子节点中
    new_child->keys.insert(new_child->keys.end(), new_sibling->keys.begin(), new_sibling->keys.end());
    if (!new_child->leaf_) {
      new_child->children.insert(new_child->children.end(), new_sibling->children.begin(), new_sibling->children.end());
    }
    parent->keys.erase(parent->keys.begin() + static_cast<long int>(idx));
    parent->children.erase(parent->children.begin() + static_cast<long int>(idx + 1));
    parent->children[idx] = new_child;
    // 更新subtree_sz

    new_child->tree_sz_ += new_sibling->tree_sz_ + 1;
    new_sibling->tree_sz_ = 0;
    if (parent->keys.size() == 0) {
      *parent = *new_child;
    }
  }

  void fill(btree_node_pointer btree_node, size_t idx) {
    if (idx != 0 && btree_node->children[idx - 1]->keys.size() >= T) {
      borrow_from_prev(btree_node, idx);
    } else if (idx != btree_node->keys.size() && btree_node->children[idx + 1]->keys.size() >= T) {
      borrow_from_next(btree_node, idx);
    } else {
      if (idx != btree_node->keys.size()) {
        merge(btree_node, idx);
      } else {
        merge(btree_node, idx - 1);
      }
    }
  }

  void borrow_from_prev(btree_node_pointer btree_node, size_t idx) {
    assert(idx > 0);
    auto child = btree_node->children[idx];
    auto sibling = btree_node->children[idx - 1];

    auto new_child = allocator_new_node(child);
    auto new_sibling = allocator_new_node(sibling);

    new_child->keys.insert(new_child->keys.begin(), btree_node->keys[idx - 1]);
    btree_node->keys[idx - 1] = sibling->keys.back();
    new_sibling->keys.pop_back();

    new_child->tree_sz_++;
    new_sibling->tree_sz_--;

    if (!new_sibling->leaf_) {
      new_child->tree_sz_ += sibling->children.back()->tree_sz_;
      new_sibling->tree_sz_ -= sibling->children.back()->tree_sz_;

      new_child->children.insert(new_child->children.begin(), sibling->children.back());
      new_sibling->children.pop_back();
    }

    btree_node->children[idx] = new_child;
    btree_node->children[idx - 1] = new_sibling;
  }

  void borrow_from_next(btree_node_pointer btree_node, size_t idx) {
    assert(idx + 1 < btree_node->children.size());

    auto child = btree_node->children[idx];
    auto sibling = btree_node->children[idx + 1];

    auto new_child = allocator_new_node(child);
    auto new_sibling = allocator_new_node(sibling);

    new_child->keys.push_back(btree_node->keys[idx]);
    btree_node->keys[idx] = new_sibling->keys.front();
    new_sibling->keys.erase(new_sibling->keys.begin());

    new_child->tree_sz_++;
    new_sibling->tree_sz_--;

    if (!new_sibling->leaf_) {
      new_child->tree_sz_ += new_sibling->children.front()->tree_sz_;
      new_sibling->tree_sz_ -= new_sibling->children.front()->tree_sz_;

      new_child->children.push_back(new_sibling->children.front());
      new_sibling->children.erase(new_sibling->children.begin());
    }

    btree_node->children[idx] = new_child;
    btree_node->children[idx + 1] = new_sibling;
  }

  btree_node_pointer remove(btree_node_pointer btree_node, const value_type& key) {
    if (!btree_node) {
      return btree_node;
    }
    auto new_node = allocator_new_node(btree_node);
    if (btree_node->leaf_) {
      auto it = std::find_if(new_node->keys.begin(), new_node->keys.end(),
                             [key, this](const util::memory::strong_rc_ptr<value_type> value) {
                               return !this->value_comp(key, *value) && !this->value_comp(*value, key);
                             });
      if (it != new_node->keys.end()) {
        new_node->keys.erase(it);
        new_node->tree_sz_--;
      }
      return new_node;
    }

    size_t idx = binary_search(new_node->keys, key);
    if (idx < new_node->keys.size() && !compare_fn_(*new_node->keys[idx], key) &&
        !compare_fn_(key, *new_node->keys[idx])) {
      if (new_node->children[idx]->keys.size() >= T) {
        util::memory::strong_rc_ptr<value_type> pred = get_predecessor(new_node, idx);
        new_node->keys[idx] = pred;
        new_node->children[idx] = remove(new_node->children[idx], *pred);
        new_node->tree_sz_--;
      } else if (new_node->children[idx + 1]->keys.size() >= T) {
        util::memory::strong_rc_ptr<value_type> succ = get_successor(new_node, idx);
        new_node->keys[idx] = succ;
        new_node->children[idx + 1] = remove(new_node->children[idx + 1], *succ);
        new_node->tree_sz_--;

      } else {
        merge(new_node, idx);
        new_node = remove(new_node, key);
      }
    } else {
      if (new_node->children[idx]->keys.size() < T) {
        fill(new_node, idx);
        new_node = remove(new_node, key);
        return new_node;
      }
      if (idx > new_node->keys.size()) {
        new_node->tree_sz_ -= new_node->children[idx - 1]->tree_sz_;
        new_node->children[idx - 1] = remove(new_node->children[idx - 1], key);
        new_node->tree_sz_ += new_node->children[idx - 1]->tree_sz_;
      } else {
        new_node->tree_sz_ -= new_node->children[idx]->tree_sz_;
        new_node->children[idx] = remove(new_node->children[idx], key);
        new_node->tree_sz_ += new_node->children[idx]->tree_sz_;
      }
    }
    return new_node;
  }

  iterator find_inner_(btree_node_pointer node, const value_type& target, btree_node_pointer root,
                       std::stack<std::pair<btree_node_pointer, size_t>>& path) {
    size_t pos = 0;
    if (node->leaf_) {
      pos = binary_search(node->keys, target);
      if (pos < node->keys.size() && !compare_fn_(target, *node->keys[pos])) {
        return iterator(node, pos, root, std::move(path));
      }
      return end();
    }

    pos = binary_search(node->keys, target);
    if (pos < node->keys.size() && !compare_fn_(target, *node->keys[pos])) {
      return iterator(node, pos, root, std::move(path));
    }
    path.push(std::make_pair(node, pos));
    return find_inner_(node->children[pos], target, root, path);
  }

  iterator at_inner_(btree_node_pointer node, size_t pos, btree_node_pointer root,
                     std::stack<std::pair<btree_node_pointer, size_t>>& path) {
    if (pos <= 0 || !node) {
      return end();
    }
    if (node->leaf_) {
      if (pos > node->keys.size()) {
        return end();
      }
      return iterator(node, pos - 1, root, std::move(path));
    }

    size_t i = 0;

    while (i < node->keys.size() && node->children[i]->tree_sz_ + 1 < pos) {
      pos -= node->children[i]->tree_sz_ + 1;
      i++;
    }
    if (pos <= node->children[i]->tree_sz_) {
      path.push(std::make_pair(node, static_cast<size_t>(i)));
      return at_inner_(node->children[i], pos, root, path);
    }
    if (pos == node->children[i]->tree_sz_ + 1 && i < node->keys.size()) {
      return iterator(node, i, root, std::move(path));
    }
    return end();
  }

  size_t index_inner_(btree_node_pointer node, const value_type& key) {
    if (!node) {
      return 0;
    }
    size_t pos = 0;
    size_t i = 0;
    while (i < node->keys.size() && compare_fn_(*node->keys[i], key)) {
      pos += (node->leaf_ ? 1 : node->children[i]->tree_sz_ + 1);
      i++;
    }
    if (i < node->keys.size() && !compare_fn_(key, *node->keys[i]) && !compare_fn_(*node->keys[i], key)) {
      pos += (node->leaf_ ? 1 : node->children[i]->tree_sz_ + 1);
      return pos;
    }
    if (!node->leaf_) {
      return pos + index_inner_(node->children[i], key);
    }
    return pos;
  }

  void get_postorder_traversal(btree_node_pointer node, std::vector<util::memory::strong_rc_ptr<value_type>>& result) {
    if (!node) {
      return;
    }
    if (node->leaf_) {
      for (size_t i = node->keys.size() - 1; i >= 0; --i) {
        result.push_back(node->keys[i]);
        if (i == 0) {
          break;
        }
      }
      return;
    }
    get_postorder_traversal(node->children.back(), result);
    for (size_t i = node->keys.size() - 1; i >= 0; --i) {
      result.push_back(node->keys[i]);
      get_postorder_traversal(node->children[i], result);
      if (i == 0) {
        break;
      }
    }
  }

  bool contains(btree_node_pointer node, const value_type& target) {
    size_t pos = binary_search(node->keys, target);
    if (pos < node->keys.size() && !compare_fn_(target, *node->keys[pos])) {
      return true;
    }
    if (node->leaf_) {
      return false;
    }
    return contains(node->children[pos], target);
  }

  size_t binary_search(const std::vector<util::memory::strong_rc_ptr<value_type>>& key, const value_type& target) {
    int32_t st = 0, ed = static_cast<int32_t>(key.size()) - 1, i = static_cast<int32_t>(key.size());
    while (st <= ed) {
      int32_t mid = (st + ed) >> 1;
      if (compare_fn_(*key[static_cast<size_t>(mid)], target)) {
        st = mid + 1;
      } else {
        i = mid;
        ed = mid - 1;
      }
    }
    return static_cast<size_t>(i);
  }

  value_pointer allocator_key_ptr(value_type key) {
    using alloc_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<value_type>;
    return util::memory::allocate_strong_rc<value_type>(allocator_type(), key);
  }

  btree_node_pointer allocator_new_node(bool val) {
    using alloc_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<node_type>;
    return util::memory::allocate_strong_rc<node_type>(allocator_type(), val, 2 * T - 1);
  }

  btree_node_pointer allocator_new_node(btree_node_pointer node) {
    // if (node == nullptr) {
    using alloc_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<node_type>;
    auto ptr = util::memory::allocate_strong_rc<node_type>(alloc_type(), node, 2 * T - 1);
    return ptr;
    // }
    // return node;
  }

  size_t T;  // B树的最小度数
  size_t max_root_version_size_;
  compare_fn_t compare_fn_;
  btree_node_pointer root_;
};