// Copyright 2022 atframework
// Created by owent on 2022-02-15.
//

#pragma once

#include <config/server_frame_build_feature.h>

#include <design_pattern/result_type.h>

#include <libcopp/future/poller.h>
#if defined(LIBCOPP_MACRO_ENABLE_STD_COROUTINE) && LIBCOPP_MACRO_ENABLE_STD_COROUTINE
#  include <libcopp/coroutine/callable_promise.h>
#endif

#include <std/explicit_declare.h>

#if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
#  include <assert.h>
#endif
#include <stdint.h>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "rpc/rpc_macros.h"

// #define PROJECT_SERVER_FRAME_USE_STD_COROUTINE

namespace rpc {

bool is_exiting_error_code(int32_t code);

template <class TVALUE>
class always_ready;

template <>
class always_ready<void> {
 public:
  using value_type = void;

 public:
  always_ready() {}

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  bool await_ready() const noexcept { return true; }
  void await_suspend(LIBCOPP_MACRO_STD_COROUTINE_NAMESPACE coroutine_handle<>) const noexcept {}
  void await_resume() const noexcept {}
#endif
};

template <class TVALUE>
class always_ready {
 public:
  using value_type = TVALUE;

 public:
  always_ready(value_type&& input) : result_data_(std::move(input)) {}

  inline operator value_type() const noexcept { return result_data_; }

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
  bool await_ready() const noexcept { return true; }
  void await_suspend(LIBCOPP_MACRO_STD_COROUTINE_NAMESPACE coroutine_handle<>) const noexcept {}
  value_type await_resume() const noexcept { return result_data_; }
#endif

 private:
  value_type result_data_;
};

using always_ready_code_type = always_ready<int32_t>;
using always_ready_void_type = always_ready<void>;

#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
struct rpc_error_code_transform {
  int32_t operator()(copp::promise_status in) const noexcept;
};

template <class TVALUE>
struct rpc_error_code_transform_wrapper;

template <>
struct rpc_error_code_transform_wrapper<int32_t> : public rpc_error_code_transform {};

template <class TVALUE>
struct rpc_error_code_transform_wrapper {
  TVALUE operator()(copp::promise_status in) const noexcept {
    return static_cast<TVALUE>(rpc_error_code_transform()(in));
  }
};

using result_void_type = copp::callable_future<void>;
template <class TVALUE>
using rpc_result = copp::callable_future<
    TVALUE, typename std::conditional<std::is_integral<TVALUE>::value, rpc_error_code_transform_wrapper<TVALUE>,
                                      copp::promise_error_transform<TVALUE>>::type>;

using result_code_type = rpc_result<int32_t>;

#else

int32_t rpc_get_not_ready_code();

namespace details {
template <class TVALUE>
struct _rpc_result_guard_enum_padding_underlying_type {
  using type = typename std::conditional<sizeof(TVALUE) <= sizeof(int32_t), int32_t, TVALUE>::type;
};

template <class TVALUE, bool IS_INTEGER, bool IS_ENUM>
struct _rpc_result_guard_value;

template <class TVALUE, bool IS_INTEGER>
struct _rpc_result_guard_value<TVALUE, IS_INTEGER, true> {
  using type =
      typename _rpc_result_guard_enum_padding_underlying_type<typename std::underlying_type<TVALUE>::type>::type;
};

template <class TVALUE>
struct _rpc_result_guard_value<TVALUE, true, false> {
  using type = typename std::decay<TVALUE>::type;
};

template <class TVALUE>
struct _rpc_result_guard_value<TVALUE, false, false> {
  using type = TVALUE;
};

template <class TVALUE>
struct _rpc_result_guard_traits {
  using decay_type = typename std::decay<TVALUE>::type;
  using type = typename _rpc_result_guard_value<TVALUE, std::is_integral<decay_type>::value,
                                                std::is_enum<decay_type>::value>::type;
};

template <class TVALUE, bool>
struct _rpc_result_not_ready;

template <class TVALUE>
struct _rpc_result_not_ready<TVALUE, true> {
  inline static TVALUE not_ready_value() noexcept { return static_cast<TVALUE>(rpc_get_not_ready_code()); }
};

template <class TVALUE>
struct _rpc_result_not_ready<TVALUE, false> {
  inline static TVALUE not_ready_value() { return TVALUE(); }
};

template <class TVALUE, bool IS_LREFERENCE>
struct rpc_result_guard_getter {};

template <class TVALUE>
struct rpc_result_guard_getter<TVALUE, true> {
  using value_type = TVALUE;

  template <class TINPUT>
  inline static value_type get(TINPUT& data) noexcept {
    return data;
  }
};

template <class TVALUE>
struct rpc_result_guard_getter<TVALUE, false> {
  using value_type = typename std::add_rvalue_reference<TVALUE>::type;

  template <class TINPUT>
  inline static value_type get(TINPUT& data) noexcept {
    return std::move(data);
  }
};

}  // namespace details

template <class TVALUE>
class rpc_result_guard {
 public:
  template <class TINPUT>
  rpc_result_guard(TINPUT&& input) : data_(input) {}

  typename details::rpc_result_guard_getter<TVALUE, std::is_lvalue_reference<TVALUE>::value>::value_type
  get() noexcept {
    return details::rpc_result_guard_getter<TVALUE, std::is_lvalue_reference<TVALUE>::value>::template get(data_);
  }

 private:
  TVALUE data_;
};

template <class TVALUE>
class rpc_result {
 public:
  using value_type = TVALUE;

 public:
  rpc_result()
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
      : awaited_(false)
#  endif
  {
  }

  template <class TINPUT>
  rpc_result(rpc_result_guard<TINPUT>&& input)
      : result_data_(input.get())
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
        ,
        waited_(false)
#  endif
  {
  }

  rpc_result(rpc_result&&) = default;
  rpc_result& operator=(rpc_result&&) = default;

#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  ~rpc_result() {
    // rpc::result_XXX must be awaited with RPC_AWAIT_IGNORE_RESULT(...), RPC_AWAIT_IGNORE_VOID(...) or
    // RPC_AWAIT_TYPE_RESULT(...)
    assert(awaited_ || !result_data_.is_ready());
  }
#  endif

  // Remove this and implement co_yield to get the result in the future
  explicit inline operator value_type() const noexcept {
    if (result_data_.is_ready()) {
      return *result_data_.data();
    } else {
      return details::_rpc_result_not_ready<value_type, std::is_integral<value_type>::value>::not_ready_value();
    }
  }

  inline bool is_ready() const noexcept { return result_data_.is_ready(); }

#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  inline void _internal_set_awaited() noexcept { awaited_ = true; }
#  endif
 private:
  copp::future::poller<value_type> result_data_;
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  bool awaited_;
#  endif
};

using result_code_type = rpc_result<int32_t>;

class result_void_type {
 public:
  result_void_type();
  explicit result_void_type(bool is_ready);
  ~result_void_type();

  result_void_type(result_void_type&&) = default;
  result_void_type& operator=(result_void_type&&) = default;

  inline bool is_ready() const noexcept { return result_data_.is_ready(); }

#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  inline void _internal_set_awaited() noexcept { awaited_ = true; }
#  endif
 private:
  copp::future::poller<void> result_data_;
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  bool awaited_;
#  endif
};

namespace details {
// Make enum as integer error code, when used with lambda, we need all return values be detected as the same type.
// So we ensure the underlying_type is at least int32_t here to let all legacy enum TYPE {} be treated as int32_t
template <class TVALUE>
rpc_result_guard<typename details::_rpc_result_guard_traits<TVALUE>::type> _make_rpc_result_guard(TVALUE&& input) {
  return rpc_result_guard<typename details::_rpc_result_guard_traits<TVALUE>::type>(std::forward<TVALUE>(input));
}

template <class TRESULT>
struct _rpc_result_traits {
  using decay_type = typename std::decay<TRESULT>::type;
  using value_type = typename decay_type::value_type;
};

template <class TRESULT>
typename _rpc_result_traits<TRESULT>::value_type _get_rpc_result_value(TRESULT&& result) {
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  result._internal_set_awaited();
#  endif
  return static_cast<typename _rpc_result_traits<TRESULT>::value_type>(result);
}

}  // namespace details

#endif

namespace details {
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
template <class TRESULT>
TRESULT _ignore_result(EXPLICIT_UNUSED_ATTR TRESULT&& result) {
  return std::forward<TRESULT>(result);
}
#else
template <class TRESULT,
          typename std::enable_if<!std::is_same<::rpc::result_void_type, typename std::decay<TRESULT>::type>::value,
                                  int>::type* = nullptr>
TRESULT _ignore_result(EXPLICIT_UNUSED_ATTR TRESULT&& result) {
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  result._internal_set_awaited();
#  endif
  return std::forward<TRESULT>(result);
}

template <class TRESULT,
          typename std::enable_if<std::is_same<::rpc::result_void_type, typename std::decay<TRESULT>::type>::value,
                                  int>::type* = nullptr>
TRESULT _ignore_void(EXPLICIT_UNUSED_ATTR TRESULT&& result) {
#  if defined(PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT) && PROJECT_SERVER_FRAME_LEGACY_COROUTINE_CHECK_AWAIT
  result._internal_set_awaited();
#  endif
  return std::forward<TRESULT>(result);
}
#endif
}  // namespace details

}  // namespace rpc

// When using c++20 coroutine, declare RPC_AWAIT_CODE_RESULT like this
#if defined(PROJECT_SERVER_FRAME_USE_STD_COROUTINE) && PROJECT_SERVER_FRAME_USE_STD_COROUTINE
#  define RPC_AWAIT_IGNORE_RESULT(x) (::rpc::details::_ignore_result(co_await (x)))
#  define RPC_AWAIT_IGNORE_VOID(x) (co_await (x))
#  define RPC_AWAIT_TYPE_RESULT(...) (co_await (__VA_ARGS__))
#  define RPC_RETURN_TYPE(...) co_return (__VA_ARGS__)
#  define RPC_RETURN_VOID co_return

#else
#  define RPC_AWAIT_IGNORE_RESULT(x) (::rpc::details::_ignore_result(x))
#  define RPC_AWAIT_IGNORE_VOID(x) (::rpc::details::_ignore_void(x))
#  define RPC_AWAIT_TYPE_RESULT(...) ::rpc::details::_get_rpc_result_value(__VA_ARGS__)
#  define RPC_RETURN_VOID return ::rpc::result_void_type(true)
#  define RPC_RETURN_TYPE(...) return ::rpc::details::_make_rpc_result_guard(__VA_ARGS__)

#endif

#define RPC_AWAIT_CODE_RESULT(...) RPC_AWAIT_TYPE_RESULT(__VA_ARGS__)
#define RPC_RETURN_CODE(x) RPC_RETURN_TYPE(x)

#define RPC_RESULT_TYPE_DECL(...) EXPLICIT_NODISCARD_ATTR ::rpc::rpc_result<__VA_ARGS__>
#define RPC_RESULT_CODE_DECL EXPLICIT_NODISCARD_ATTR ::rpc::result_code_type
#define RPC_RESULT_VOID_DECL EXPLICIT_NODISCARD_ATTR ::rpc::result_void_type
