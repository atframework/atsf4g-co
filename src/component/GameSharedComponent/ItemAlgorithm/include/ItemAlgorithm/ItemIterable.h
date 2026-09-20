// Copyright 2026 atframework

#pragma once

#include <nostd/function_ref.h>
#include <nostd/type_traits.h>

#include <config/compile_optimize.h>
#include <gsl/select-gsl.h>

#include <memory/rc_ptr.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "ItemAlgorithm/ItemAlgorithmConfig.h"

ITEM_ALGORITHM_NAMESPACE_BEGIN

namespace item_algorithm {

// ============================================================
// 物品请求序列的只读 / 可写视图
//
// 容器的操作接口 (check_add / add / check_sub / sub / check_has ...) 不再直接吃
// google::protobuf::RepeatedPtrField<T>, 而是接收这层视图:
//   - 调用方可以用任何"可遍历的容器" (RepeatedPtrField / std::vector / 数组 / gsl::span)
//     通过 make_item_readable_iterable / make_item_writable_iterable 构造视图;
//   - 容器内部只依赖 foreach / size / empty 三个接口, 不关心数据实际存在哪里。
// ============================================================

template <class T, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable;

template <class T>
struct ATFW_UTIL_SYMBOL_VISIBLE item_iterable_foreach_return_guard;

/// @brief foreach 回调返回 void 时的语法糖: 总是跑完全部元素
template <>
struct ATFW_UTIL_SYMBOL_VISIBLE item_iterable_foreach_return_guard<void> {
  template <class T, bool Writable, class F>
  static void invoke(const item_iterable<T, Writable>& self, F&& fn) {
    auto delegate_fn = [&fn](typename item_iterable<T, Writable>::callback_parameter item) -> bool {
      fn(item);
      return true;
    };
    self.foreach (delegate_fn);
  }
};

/// @brief foreach 回调返回 bool 时的语法糖: 返回 false 提前中断, 并把结果传回调用方
template <>
struct ATFW_UTIL_SYMBOL_VISIBLE item_iterable_foreach_return_guard<bool> {
  template <class T, bool Writable, class F>
  ATFW_UTIL_FORCEINLINE static item_iterable_foreach_return_guard invoke(const item_iterable<T, Writable>& self,
                                                                         F&& fn) {
    auto delegate_fn = [&fn](typename item_iterable<T, Writable>::callback_parameter item) -> bool { return fn(item); };
    return {self.foreach (delegate_fn)};
  }

  ATFW_UTIL_FORCEINLINE item_iterable_foreach_return_guard(bool v) noexcept : result_(v) {}

  ATFW_UTIL_FORCEINLINE operator bool() const noexcept { return result_; }

  bool result_;
};

/// @brief 只读视图与可写视图的 const 处理差异
template <bool Writable>
class item_iterable_access_controller;

template <>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable_access_controller<false> {
 protected:
  template <class T>
  ATFW_UTIL_FORCEINLINE static const T& internal_access(const T& inout) {
    return inout;
  }
};

template <>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable_access_controller<true> {
 protected:
  template <class T>
  ATFW_UTIL_FORCEINLINE static T& internal_access(const T& inout) {
    return const_cast<T&>(inout);
  }

  template <class T>
  ATFW_UTIL_FORCEINLINE static T& internal_access(T& inout) {
    return inout;
  }
};

/// @brief 物品请求序列视图基类
///
/// Writable 为 true 时 foreach 回调拿到可写引用 (caller 可原地修改元素);
/// 为 false 时拿到 const 引用。
template <class T, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable : public item_iterable_access_controller<Writable> {
 public:
  using element_type = T;
  using value_type = atfw::util::nostd::remove_cv_t<element_type>;
  using size_type = std::size_t;
  using pointer = element_type*;
  using const_pointer = const element_type*;
  using reference = element_type&;
  using const_reference = const element_type&;
  using callback_parameter = typename std::conditional<Writable, reference, const_reference>::type;

 protected:
  using item_iterable_access_controller<Writable>::internal_access;

 public:
  virtual ~item_iterable() = default;

  /// @brief 依次访问每个元素; 回调返回 false 时中断遍历
  /// @return true 表示所有元素都被访问过 (回调没有提前中断)
  virtual bool foreach (atfw::util::nostd::function_ref<bool(callback_parameter)>) const = 0;

  virtual bool empty() const noexcept = 0;

  virtual size_t size() const noexcept = 0;

  /// @brief 回调返回 void / bool 时的语法糖 (返回 bool 版本会把中断标记带出来)
  template <class F,
            class R = item_iterable_foreach_return_guard<atfw::util::nostd::invoke_result_t<F, callback_parameter>>,
            class = atfw::util::nostd::enable_if_t<
                !std::is_convertible<F, atfw::util::nostd::function_ref<bool(callback_parameter)>>::value>>
  ATFW_UTIL_FORCEINLINE R foreach (F&& fn) const {
    return R::invoke(*this, std::forward<F>(fn));
  }
};

/// @brief 只读视图
template <class T>
using item_readable_iterable = item_iterable<T, false>;

/// @brief 可写视图
template <class T>
using item_writable_iterable = item_iterable<T, true>;

template <class TC, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable_view;

/// @brief 基于 gsl::span 的视图 (覆盖数组 / std::vector / RepeatedPtrField 等连续或可迭代容器)
template <class T, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable_view<gsl::span<T>, Writable>
    : public item_iterable<atfw::util::nostd::remove_cv_t<T>, Writable> {
 protected:
  using base_type = item_iterable<atfw::util::nostd::remove_cv_t<T>, Writable>;

  using base_type::internal_access;

 public:
  explicit item_iterable_view(gsl::span<T> container) noexcept : container_(container) {}

  ~item_iterable_view() override = default;

  bool foreach (atfw::util::nostd::function_ref<bool(typename base_type::callback_parameter)> callback) const override {
    for (auto& item : container_) {
      if (!callback(internal_access(item))) {
        return false;
      }
    }
    return true;
  }
  using base_type::foreach;

  bool empty() const noexcept override { return container_.empty(); }

  size_t size() const noexcept override { return static_cast<size_t>(container_.size()); }

 private:
  gsl::span<T> container_;
};

/// @brief 基于任意可迭代容器 (含 google::protobuf::RepeatedPtrField) 的视图
template <class TC, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_iterable_view
    : public item_iterable<atfw::util::nostd::remove_cv_t<typename TC::value_type>, Writable> {
 protected:
  using base_type = item_iterable<atfw::util::nostd::remove_cv_t<typename TC::value_type>, Writable>;

  using base_type::internal_access;

 public:
  explicit item_iterable_view(const TC& container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : container_{&container} {}

  ~item_iterable_view() override = default;

  bool foreach (atfw::util::nostd::function_ref<bool(typename base_type::callback_parameter)> callback) const override {
    for (auto& item : *container_) {
      if (!callback(internal_access(item))) {
        return false;
      }
    }
    return true;
  }
  using base_type::foreach;

  bool empty() const noexcept override { return container_->empty(); }

  size_t size() const noexcept override { return static_cast<size_t>(container_->size()); }

 private:
  const TC* container_;
};

template <class T>
struct ATFW_UTIL_SYMBOL_VISIBLE item_remove_pointer;

template <class T>
struct ATFW_UTIL_SYMBOL_VISIBLE item_remove_pointer<atfw::util::memory::strong_rc_ptr<T>> {
  using type = T;
};

template <class T>
struct ATFW_UTIL_SYMBOL_VISIBLE item_remove_pointer<std::shared_ptr<T>> {
  using type = T;
};

template <class T>
struct ATFW_UTIL_SYMBOL_VISIBLE item_remove_pointer {
  using type = atfw::util::nostd::remove_pointer_t<T>;
};

template <class T>
using item_remove_pointer_t = typename item_remove_pointer<T>::type;

template <class T, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_pointer_iterable_view;

/// @brief 元素为指针的视图 (遍历时自动跳过空指针, 回调拿到解引用后的对象)
template <class T, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_pointer_iterable_view<gsl::span<T>, Writable>
    : public item_iterable<atfw::util::nostd::remove_cv_t<item_remove_pointer_t<T>>, Writable> {
 protected:
  using base_type = item_iterable<atfw::util::nostd::remove_cv_t<item_remove_pointer_t<T>>, Writable>;

  using base_type::internal_access;

 public:
  explicit item_pointer_iterable_view(gsl::span<T> container) noexcept : container_{container} {}

  ~item_pointer_iterable_view() override = default;

  bool foreach (atfw::util::nostd::function_ref<bool(typename base_type::callback_parameter)> callback) const override {
    for (auto& item : container_) {
      if (nullptr == item) {
        continue;
      }
      if (!callback(internal_access(*item))) {
        return false;
      }
    }
    return true;
  }
  using base_type::foreach;

  bool empty() const noexcept override { return container_.empty(); }

  size_t size() const noexcept override { return static_cast<size_t>(container_.size()); }

 private:
  gsl::span<T> container_;
};

template <class TC, bool Writable>
class ATFW_UTIL_SYMBOL_VISIBLE item_pointer_iterable_view
    : public item_iterable<atfw::util::nostd::remove_cv_t<item_remove_pointer_t<typename TC::value_type>>, Writable> {
 protected:
  using base_type =
      item_iterable<atfw::util::nostd::remove_cv_t<item_remove_pointer_t<typename TC::value_type>>, Writable>;

  using base_type::internal_access;

 public:
  explicit item_pointer_iterable_view(const TC& container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) noexcept
      : container_{&container} {}

  ~item_pointer_iterable_view() override = default;

  bool foreach (atfw::util::nostd::function_ref<bool(typename base_type::callback_parameter)> callback) const override {
    for (auto& item : *container_) {
      if (nullptr == item) {
        continue;
      }
      if (!callback(internal_access(*item))) {
        return false;
      }
    }
    return true;
  }
  using base_type::foreach;

  bool empty() const noexcept override { return container_->empty(); }

  size_t size() const noexcept override { return static_cast<size_t>(container_->size()); }

 private:
  const TC* container_;
};

// ============================================================
// 构造视图的辅助函数
// 用法:
//   std::vector<T> vec;                       auto v = make_item_readable_iterable(vec);
//   google::protobuf::RepeatedPtrField<T> pb; auto v = make_item_readable_iterable(pb);
//   gsl::span<T> span;                        auto v = make_item_readable_iterable(span);
//   T arr[3];                                 auto v = make_item_readable_iterable(arr);
//   std::vector<T*> ptrs;                     auto v = make_item_pointer_readable_iterable(ptrs);
// ============================================================

template <class TC, class = decltype(std::begin(std::declval<TC>()))>
item_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, false> make_item_readable_iterable(
    TC& container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, false>{container};
}

template <class T>
item_iterable_view<gsl::span<T>, false> make_item_readable_iterable(const gsl::span<T>& container
                                                                    ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_iterable_view<gsl::span<T>, false>{container};
}

template <class T, size_t SIZE>
item_iterable_view<gsl::span<T>, false> make_item_readable_iterable(T (&container)[SIZE]) {
  return item_iterable_view<gsl::span<T>, false>{gsl::make_span(container)};
}

template <class T>
item_iterable_view<gsl::span<atfw::util::nostd::remove_cv_t<T>>, false> make_item_readable_iterable(
    std::initializer_list<T> container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_iterable_view<gsl::span<T>, false>{gsl::span<T>{container.begin(), container.end()}};
}

template <class TC, class = decltype(std::begin(std::declval<TC>()))>
item_pointer_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, false> make_item_pointer_readable_iterable(
    TC& container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_pointer_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, false>{container};
}

template <class T, size_t SIZE>
item_pointer_iterable_view<gsl::span<T>, false> make_item_pointer_readable_iterable(T (&container)[SIZE]) {
  return item_pointer_iterable_view<gsl::span<T>, false>{gsl::make_span(container)};
}

template <class T>
item_pointer_iterable_view<gsl::span<T>, false> make_item_pointer_readable_iterable(
    std::initializer_list<T> container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_pointer_iterable_view<gsl::span<T>, false>{gsl::span<T>{container.begin(), container.end()}};
}

/// @brief 可写视图: 只接受右值容器 (避免误把只读的具名量当可写)
template <class TC, class = decltype(std::begin(std::declval<TC>())),
          class = atfw::util::nostd::enable_if_t<std::is_rvalue_reference<TC&&>::value>>
item_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, true> make_item_writable_iterable(
    TC&& container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, true>{container};
}

template <class T, size_t SIZE, class = atfw::util::nostd::enable_if_t<std::is_rvalue_reference<T (&&)[SIZE]>::value>>
item_iterable_view<gsl::span<T>, true> make_item_writable_iterable(T (&&container)[SIZE]) {
  return item_iterable_view<gsl::span<T>, true>{gsl::make_span(container)};
}

template <class T, class = atfw::util::nostd::enable_if_t<std::is_rvalue_reference<T&&>::value>>
item_iterable_view<gsl::span<atfw::util::nostd::remove_cv_t<T>>, true> make_item_writable_iterable(
    std::initializer_list<T&&> container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_iterable_view<gsl::span<T>, true>{gsl::span<T>{container.begin(), container.end()}};
}

template <class TC, class = decltype(std::begin(std::declval<TC>())),
          class = atfw::util::nostd::enable_if_t<std::is_rvalue_reference<TC&&>::value>>
item_pointer_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, true> make_item_pointer_writable_iterable(
    TC&& container ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_pointer_iterable_view<atfw::util::nostd::remove_cvref_t<TC>, true>{container};
}

template <class T, size_t SIZE, class = atfw::util::nostd::enable_if_t<std::is_rvalue_reference<T (&&)[SIZE]>::value>>
item_pointer_iterable_view<gsl::span<T>, true> make_item_pointer_writable_iterable(T (&&container)[SIZE]) {
  return item_pointer_iterable_view<gsl::span<T>, true>{gsl::make_span(container)};
}

template <class T, class = atfw::util::nostd::enable_if_t<std::is_rvalue_reference<T&&>::value>>
item_pointer_iterable_view<gsl::span<T>, true> make_item_pointer_writable_iterable(std::initializer_list<T&&> container
                                                                                   ATFW_UTIL_ATTRIBUTE_LIFETIME_BOUND) {
  return item_pointer_iterable_view<gsl::span<T>, true>{gsl::span<T>{container.begin(), container.end()}};
}

}  // namespace item_algorithm

ITEM_ALGORITHM_NAMESPACE_END
