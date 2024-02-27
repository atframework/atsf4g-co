// Copyright 2021 atframework
// Created by owent on 2017/2/6.
//

#pragma once

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/duration.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/timestamp.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <gsl/select-gsl.h>

#include <stdint.h>
#include <chrono>
#include <cstddef>
#include <string>

/**
 * @brief 返回易读数据
 * @note 因为protobuf默认的DebugString某些情况下会打印出巨量不易读的内容，故而自己实现。优化一下。
 * @param msg 要打印的message
 */
SERVER_FRAME_API std::string protobuf_mini_dumper_get_readable(const ::google::protobuf::Message &msg);

/**
 * @brief 返回错误码文本描述
 * @param error_code 错误码，需要定义在MTSvrErrorDefine或MTErrorDefine里
 * @return 错误码的文本描述，永远不会返回NULL
 */
SERVER_FRAME_API gsl::string_view protobuf_mini_dumper_get_error_msg(int error_code);

/**
 * @brief 返回指定枚举类型的错误码文本描述
 * @param error_code 错误码
 * @return 错误码的文本描述，永远不会返回NULL
 */
SERVER_FRAME_API std::string protobuf_mini_dumper_get_error_msg(int error_code,
                                                                const ::google::protobuf::EnumDescriptor *enum_desc,
                                                                bool fallback_common_errmsg);

/**
 * @brief 返回指定枚举类型的错误码文本描述
 * @param error_code 错误码
 * @return 错误码的文本描述
 */
SERVER_FRAME_API gsl::string_view protobuf_mini_dumper_get_enum_name(
    int32_t error_code, const ::google::protobuf::EnumDescriptor *enum_desc);

/**
 * @brief protobuf 数据拷贝
 * @note 加这个接口是为了解决protobuf的CopyFrom重载了CopyFrom(const
 * Message&)。如果类型不匹配只能在运行时发现抛异常。加一层这个接口是为了提到编译期
 * @param dst 拷贝目标
 * @param src 拷贝源
 */
template <class TMsg>
UTIL_SYMBOL_VISIBLE inline void protobuf_copy_message(TMsg &dst, const TMsg &src) {
  if (&src == &dst) {
    return;
  }
  dst.CopyFrom(src);
}

template <class TField>
UTIL_SYMBOL_VISIBLE inline void protobuf_copy_message(::google::protobuf::RepeatedField<TField> &dst,
                                                      const ::google::protobuf::RepeatedField<TField> &src) {
  if (&src == &dst) {
    return;
  }
  dst.Reserve(src.size());
  dst.CopyFrom(src);
}

template <class TField>
UTIL_SYMBOL_VISIBLE inline void protobuf_copy_message(::google::protobuf::RepeatedPtrField<TField> &dst,
                                                      const ::google::protobuf::RepeatedPtrField<TField> &src) {
  if (&src == &dst) {
    return;
  }
  dst.Reserve(src.size());
  dst.CopyFrom(src);
}

#if defined(LIBATFRAME_UTILS_ENABLE_GSL_WITH_GSL_LITE) && LIBATFRAME_UTILS_ENABLE_GSL_WITH_GSL_LITE
template <class TField, class TValue>
inline void protobuf_copy_message(::google::protobuf::RepeatedField<TField> &dst, gsl::span<TValue> src) {
#else
template <class TField, class TValue, size_t SpanExtent>
UTIL_SYMBOL_VISIBLE inline void protobuf_copy_message(::google::protobuf::RepeatedField<TField> &dst,
                                                      gsl::span<TValue, SpanExtent> src) {
#endif
  if (dst.empty() && src.empty()) {
    return;
  }
  if (src.empty()) {
    dst.Clear();
    return;
  }

  if (static_cast<size_t>(dst.size()) == src.size() && src.data() == dst.data()) {
    return;
  }

  dst.Reserve(static_cast<int>(src.size()));
  dst.Clear();
  for (auto &element : src) {
    dst.Add(element);
  }
}

/**
 * @brief protobuf 数据移动，移动前检查Arena
 * @note 加这个接口是为了解决protobuf的CopyFrom重载了CopyFrom(const
 * Message&)。如果类型不匹配只能在运行时发现抛异常。加一层这个接口是为了提到编译期
 * @param dst 拷贝目标
 * @param src 拷贝源
 */
template <class TMsg>
UTIL_SYMBOL_VISIBLE inline void protobuf_move_message(TMsg &dst, TMsg &&src) {
  if (&src == &dst) {
    return;
  }
  if (dst.GetArena() == src.GetArena()) {
    dst.Swap(&src);
  } else {
    protobuf_copy_message(dst, src);
  }

  src.Clear();
}

template <class TField>
UTIL_SYMBOL_VISIBLE inline void protobuf_move_message(::google::protobuf::RepeatedField<TField> &dst,
                                                      ::google::protobuf::RepeatedField<TField> &&src) {
  if (&src == &dst) {
    return;
  }
  if (dst.GetArena() == src.GetArena()) {
    dst.Swap(&src);
  } else {
    protobuf_copy_message(dst, src);
  }

  src.Clear();
}

template <class TField>
UTIL_SYMBOL_VISIBLE inline void protobuf_move_message(::google::protobuf::RepeatedPtrField<TField> &dst,
                                                      ::google::protobuf::RepeatedPtrField<TField> &&src) {
  if (&src == &dst) {
    return;
  }
  if (dst.GetArena() == src.GetArena()) {
    dst.Swap(&src);
  } else {
    protobuf_copy_message(dst, src);
  }

  src.Clear();
}

template <class TEle>
UTIL_SYMBOL_VISIBLE int protobuf_remove_repeated_at(::google::protobuf::RepeatedPtrField<TEle> &arr, int index) {
  if (index < 0 || index >= arr.size()) {
    return 0;
  }

  if (index != arr.size() - 1) {
    arr.SwapElements(index, arr.size() - 1);
  }

  arr.RemoveLast();
  return 1;
}

template <class TEle, class TCheckFn>
UTIL_SYMBOL_VISIBLE int protobuf_remove_repeated_if(::google::protobuf::RepeatedPtrField<TEle> &arr,
                                                    const TCheckFn &fn) {
  int new_index = 0;
  int old_index = 0;
  int ret = 0;
  for (; old_index < arr.size(); ++old_index, ++new_index) {
    if (new_index != old_index) {
      arr.SwapElements(new_index, old_index);
    }

    if (fn(*arr.Mutable(new_index))) {
      --new_index;
    }
  }

  while (arr.size() > new_index) {
    arr.RemoveLast();
    ++ret;
  }

  return ret;
}

template <class TEle, class TCheckFn>
UTIL_SYMBOL_VISIBLE int protobuf_remove_repeated_if(::google::protobuf::RepeatedField<TEle> &arr, TCheckFn &&fn) {
  int new_index = 0;
  int old_index = 0;
  int ret = 0;
  for (; old_index < arr.size(); ++old_index, ++new_index) {
    if (fn(*arr.Mutable(new_index))) {
      --new_index;
    } else if (new_index != old_index) {
      *arr.Mutable(new_index) = *arr.Mutable(old_index);
    }
  }

  if (arr.size() > new_index) {
    ret = arr.size() - new_index;
    arr.Truncate(new_index);
  }

  return ret;
}

/**
 * @brief Prototbuf well known 时间点类型转系统时间
 * @param tp 时间点
 * @return 系统时间
 */
SERVER_FRAME_API std::chrono::system_clock::time_point protobuf_to_system_clock(const google::protobuf::Timestamp &tp);

/**
 * @brief 系统时间转Prototbuf well known 时间点类型
 * @param tp 时间点
 * @return Prototbuf well known 时间点类型
 */
SERVER_FRAME_API google::protobuf::Timestamp protobuf_from_system_clock(std::chrono::system_clock::time_point tp);

/**
 * @brief Prototbuf well known 时间周期类型转标准时间
 * @param dur 时间周期
 * @return 标准时间周期
 */
template <class DurationType = std::chrono::system_clock::duration>
UTIL_SYMBOL_VISIBLE DurationType protobuf_to_chrono_duration(const google::protobuf::Duration &dur) {
  return std::chrono::duration_cast<DurationType>(std::chrono::seconds{dur.seconds()}) +
         std::chrono::duration_cast<DurationType>(std::chrono::nanoseconds{dur.nanos()});
}

/**
 * @brief 标准时间转Prototbuf well known 时间周期类型
 * @param dur 时间周期
 * @return Prototbuf well known 时间周期类型
 */
template <class DurationType = std::chrono::system_clock::duration>
UTIL_SYMBOL_VISIBLE google::protobuf::Timestamp protobuf_from_chrono_duration(DurationType dur) {
  google::protobuf::Timestamp ret;
  ret.set_seconds(static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(dur).count()));
  ret.set_nanos(static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(dur - std::chrono::seconds{ret.seconds()}).count() %
      1000000000));
  return ret;
}
