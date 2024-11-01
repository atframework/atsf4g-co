// Copyright 2024 Tencent
// Created by owent on 2024-10-29.
//

#pragma once

#include <config/compile_optimize.h>

#include <memory/rc_ptr.h>
#include <nostd/nullability.h>
#include <nostd/type_traits.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/arena.h>
#include <google/protobuf/message.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <config/server_frame_build_feature.h>

#include <memory>
#include <utility>

namespace rpc {
class context;

struct UTIL_SYMBOL_VISIBLE __shared_message_default_tag{};

struct UTIL_SYMBOL_VISIBLE __shared_message_copy_tag{};

struct UTIL_SYMBOL_VISIBLE __shared_message_allocator_tag{};

struct UTIL_SYMBOL_VISIBLE __shared_message_allocator_copy_tag{};

template <class MessageType, class Allocator = ::std::allocator<util::nostd::remove_cvref_t<MessageType>>>
class UTIL_SYMBOL_VISIBLE shared_message;

template <class>
struct __shared_message_internal_types_checker;

template <>
struct __shared_message_internal_types_checker<__shared_message_default_tag> : public ::std::true_type {};

template <>
struct __shared_message_internal_types_checker<__shared_message_copy_tag> : public ::std::true_type {};

template <>
struct __shared_message_internal_types_checker<__shared_message_allocator_tag> : public ::std::true_type {};

template <>
struct __shared_message_internal_types_checker<__shared_message_allocator_copy_tag> : public ::std::true_type {};

template <class MT, class AT>
struct __shared_message_internal_types_checker<shared_message<MT, AT>> : public ::std::true_type {};

template <>
struct __shared_message_internal_types_checker<std::shared_ptr<::google::protobuf::Arena>> : public ::std::true_type {};

template <>
struct __shared_message_internal_types_checker<::google::protobuf::Arena> : public ::std::true_type {};

template <class>
struct __shared_message_internal_types_checker : public ::std::false_type {};

template <class T>
struct __shared_message_internal_types
    : public __shared_message_internal_types_checker<util::nostd::remove_cvref_t<T>> {};

template <class>
struct __shared_message_internal_tag_types_checker;

template <>
struct __shared_message_internal_tag_types_checker<__shared_message_default_tag> : public ::std::true_type {};

template <>
struct __shared_message_internal_tag_types_checker<__shared_message_copy_tag> : public ::std::true_type {};

template <>
struct __shared_message_internal_tag_types_checker<__shared_message_allocator_tag> : public ::std::true_type {};

template <>
struct __shared_message_internal_tag_types_checker<__shared_message_allocator_copy_tag> : public ::std::true_type {};

template <class>
struct __shared_message_internal_tag_types_checker : public ::std::false_type {};

template <class T>
struct __shared_message_internal_tag_types
    : public __shared_message_internal_tag_types_checker<util::nostd::remove_cvref_t<T>> {};

SERVER_FRAME_API const std::shared_ptr<::google::protobuf::Arena> &get_shared_arena(const context &);

template <class MessageType, class Allocator>
class UTIL_SYMBOL_VISIBLE __shared_message_shared_base {
 public:
  using type = MessageType;
  using pointer = type *;
  using reference = type &;
  using const_reference = const type &;
  using allocator = Allocator;
  using arena_pointer = std::shared_ptr<::google::protobuf::Arena>;
  using element_pointer = util::memory::strong_rc_ptr<type>;

 protected:
  struct arena_deletor {
    inline void operator()(pointer) const noexcept {}
  };

  template <class Alloc>
  UTIL_FORCEINLINE static allocator __convert_allocator(const Alloc &alloc) noexcept {
    return allocator{alloc};
  }

  UTIL_FORCEINLINE static const allocator &__convert_allocator(const allocator &alloc) noexcept { return alloc; }

  template <class Alloc, class... Args>
  inline static void __make_instance(const arena_pointer &arena, element_pointer &instance, const Alloc &alloc,
                                     Args &&...args) noexcept(std::is_nothrow_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (instance) {
      return;
    }

    if (arena) {
      instance = util::memory::strong_rc_ptr<type>(
#if (defined(PROTOBUF_VERSION) && PROTOBUF_VERSION >= 5027000) || \
    (defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION >= 5027000)
          ::google::protobuf::Arena::Create<type>(arena.get(), std::forward<Args>(args)...)
#else
          ::google::protobuf::Arena::CreateMessage<type>(arena.get(), std::forward<Args>(args)...)
#endif
              ,
          arena_deletor());
      if UTIL_LIKELY_CONDITION (instance) {
        return;
      }
    }

    instance = util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), std::forward<Args>(args)...);
  }
};

template <class MessageType, class Allocator, bool EnableLazyMakeInstance>
class UTIL_SYMBOL_VISIBLE __shared_message_base;

template <class MessageType, class Allocator>
class UTIL_SYMBOL_VISIBLE __shared_message_base<MessageType, Allocator, true>
    : public __shared_message_shared_base<MessageType, Allocator> {
 public:
  using base_type = __shared_message_shared_base<MessageType, Allocator>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

 protected:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__make_instance;

  UTIL_FORCEINLINE static void __lazy_make_default_instance(
      const arena_pointer &arena, element_pointer &instance) noexcept(std::is_nothrow_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (!instance) {
      Allocator alloc;
      __make_instance(arena, instance, alloc);
    }
  }

  template <class T>
  UTIL_FORCEINLINE static T &&__move_member(T &t) noexcept {
    return static_cast<T &&>(t);
  }

  UTIL_FORCEINLINE static void __ctor_make_default_instance(
      const arena_pointer & /*arena*/, element_pointer & /*instance*/,
      const Allocator & /*alloc*/) noexcept(std::is_nothrow_constructible<type>::value) {}

  UTIL_FORCEINLINE static void __ctor_make_default_instance(
      const arena_pointer & /*arena*/,
      element_pointer & /*instance*/) noexcept(std::is_nothrow_constructible<type>::value) {}
};

template <class MessageType, class Allocator>
class UTIL_SYMBOL_VISIBLE __shared_message_base<MessageType, Allocator, false>
    : public __shared_message_shared_base<MessageType, Allocator> {
 public:
  using base_type = __shared_message_shared_base<MessageType, Allocator>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

 protected:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__make_instance;

  UTIL_FORCEINLINE static void __lazy_make_default_instance(
      const arena_pointer & /*arena*/,
      element_pointer & /*instance*/) noexcept(std::is_nothrow_constructible<type>::value) {}

  template <class T>
  UTIL_FORCEINLINE static T &__move_member(T &t) noexcept {
    return static_cast<T &>(t);
  }

  UTIL_FORCEINLINE static void __ctor_make_default_instance(
      const arena_pointer &arena, element_pointer &instance,
      const Allocator &alloc) noexcept(std::is_nothrow_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (!instance) {
      __make_instance(arena, instance, alloc);
    }
  }

  UTIL_FORCEINLINE static void __ctor_make_default_instance(
      const arena_pointer &arena, element_pointer &instance) noexcept(std::is_nothrow_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (!instance) {
      Allocator alloc;
      __make_instance(arena, instance, alloc);
    }
  }
};

template <class MessageType, class Allocator>
class UTIL_SYMBOL_VISIBLE shared_message final
    : public __shared_message_base<util::nostd::remove_cvref_t<MessageType>, Allocator,
                                   std::is_default_constructible<util::nostd::remove_cvref_t<MessageType>>::value> {
 public:
  using base_type =
      __shared_message_base<util::nostd::remove_cvref_t<MessageType>, Allocator,
                            std::is_default_constructible<util::nostd::remove_cvref_t<MessageType>>::value>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

 private:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__ctor_make_default_instance;
  using base_type::__lazy_make_default_instance;
  using base_type::__make_instance;
  using base_type::__move_member;

  template <class>
  struct __internal_types_checker;

  template <>
  struct __internal_types_checker<type> : public ::std::true_type {};

  template <class T>
  struct __internal_types_checker : public __shared_message_internal_types_checker<T> {};

  template <class T>
  struct __internal_types : public __internal_types_checker<util::nostd::remove_cvref_t<T>> {};

 public:
  // This object can not pointer to a empty data, so disable move constructor and move assignment
  inline shared_message(shared_message &&other) noexcept  // NOLINT: runtime/explicit
      : arena_(__move_member(other.arena_)), instance_(__move_member(other.instance_)) {}

  inline shared_message &operator=(shared_message &&other) noexcept {
    arena_ = __move_member(other.arena_);
    instance_ = __move_member(other.instance_);
    return *this;
  }

  template <
      class OtherMessageType, class OtherAllocatorType,
      class = util::nostd::enable_if_t<std::is_base_of<type, util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline shared_message(shared_message<OtherMessageType, OtherAllocatorType> &&other)  // NOLINT: runtime/explicit
      noexcept
      : arena_(__move_member(other.arena_)), instance_(__move_member(other.instance_)) {}

  template <
      class OtherMessageType, class OtherAllocatorType,
      class = util::nostd::enable_if_t<std::is_base_of<type, util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline shared_message &operator=(shared_message<OtherMessageType, OtherAllocatorType> &&other) noexcept {
    arena_ = other.arena_;
    instance_ = __move_member(other.instance_);
    return *this;
  }

  inline shared_message(const shared_message &other)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_constructible<type>::value)
      : arena_(other.arena_), instance_(other.share_instance()) {}

  inline shared_message &operator=(const shared_message &other) noexcept(std::is_nothrow_constructible<type>::value) {
    arena_ = other.arena_;
    instance_ = other.share_instance();
    return *this;
  }

  template <
      class OtherMessageType, class OtherAllocatorType,
      class = util::nostd::enable_if_t<std::is_base_of<type, util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline shared_message(const shared_message<OtherMessageType, OtherAllocatorType> &other)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_constructible<OtherAllocatorType>::value)
      : arena_(other.arena_), instance_(other.share_instance()) {}

  template <
      class OtherMessageType, class OtherAllocatorType,
      class = util::nostd::enable_if_t<std::is_base_of<type, util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline shared_message &operator=(const shared_message<OtherMessageType, OtherAllocatorType> &other) noexcept(
      std::is_nothrow_constructible<OtherAllocatorType>::value) {
    arena_ = other.arena_;
    instance_ = other.share_instance();
    return *this;
  }

  // Construct with default allocator
  template <class = util::nostd::enable_if_t<std::is_default_constructible<type>::value>>
  inline shared_message() noexcept(std::is_nothrow_constructible<type>::value) {
    __ctor_make_default_instance(arena_, instance_);
  }

  template <class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_default_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(__shared_message_default_tag,
                        ArenaSourceType &&arena_source) noexcept(std::is_nothrow_constructible<type>::value)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    __ctor_make_default_instance(arena_, instance_);
  }
  template <class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_default_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(ArenaSourceType &&arena_source)  // NOLINT: runtime/explicit
      : shared_message(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source)) {}

  template <class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(__shared_message_default_tag, ArenaSourceType &&arena_source, type &&message)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      allocator alloc;
      instance_ = util::memory::allocate_strong_rc<type>(alloc, std::move(message));
    } else if (message.GetArena() == arena_.get()) {
      instance_ = util::memory::strong_rc_ptr<type>(&message, arena_deletor());
    } else {
      allocator alloc;
      make_instance(alloc, std::move(message));
    }
  }
  template <class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(ArenaSourceType &&arena_source, type &&message)
      : shared_message(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source),
                       std::move(message)) {}

  template <class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(__shared_message_copy_tag, ArenaSourceType &&arena_source, const type &message)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      allocator alloc;
      instance_ = util::memory::allocate_strong_rc<type>(alloc, message);
    } else {
      allocator alloc;
      make_instance(alloc, message);
    }
  }

  template <class ArenaSourceType, class Arg0, class... Args>
  inline shared_message(util::nostd::enable_if_t<!__internal_types<Arg0>::value &&
                                                     !__shared_message_internal_tag_types<ArenaSourceType>::value,
                                                 __shared_message_default_tag>,
                        ArenaSourceType &&arena_source, Arg0 &&arg0, Args &&...args)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    allocator alloc;
    make_instance(alloc, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
  }

  // Construct with specify allocator
  template <class Alloc, class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_default_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(__shared_message_allocator_tag, const Alloc &alloc,
                        ArenaSourceType &&arena_source) noexcept(std::is_nothrow_constructible<type>::value)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    __ctor_make_default_instance(arena_, instance_, __convert_allocator(alloc));
  }

  template <class Alloc, class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(__shared_message_allocator_tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                        type &&message)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      instance_ = util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), std::move(message));
    } else if (message.GetArena() == arena_.get()) {
      instance_ = util::memory::strong_rc_ptr<type>(&message, arena_deletor());
    } else {
      make_instance(alloc, std::move(message));
    }
  }

  template <class Alloc, class ArenaSourceType,
            class = util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                             !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline shared_message(__shared_message_allocator_copy_tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                        const type &message)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      instance_ = util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), message);
    } else {
      make_instance(alloc, message);
    }
  }

  template <class Alloc, class ArenaSourceType, class Arg0, class... Args>
  inline shared_message(util::nostd::enable_if_t<!__shared_message_internal_types<Arg0>::value &&
                                                     !__shared_message_internal_tag_types<ArenaSourceType>::value,
                                                 __shared_message_allocator_tag>,
                        const Alloc &alloc, ArenaSourceType &&arena_source, Arg0 &&arg0, Args &&...args)
      : arena_(get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    make_instance(__convert_allocator(alloc), std::forward<Arg0>(arg0), std::forward<Args>(args)...);
  }

  // Other member functions
  inline util::nostd::nonnull<const pointer> get() const noexcept(std::is_nothrow_constructible<type>::value) {
    __lazy_make_default_instance(arena_, instance_);
    return instance_.get();
  }

  inline util::nostd::nonnull<pointer> get() noexcept(std::is_nothrow_constructible<type>::value) {
    __lazy_make_default_instance(arena_, instance_);
    return instance_.get();
  }

  inline util::nostd::nonnull<const pointer> operator->() const noexcept(std::is_nothrow_constructible<type>::value) {
    return get();
  }

  inline util::nostd::nonnull<pointer> operator->() noexcept(std::is_nothrow_constructible<type>::value) {
    return get();
  }

  inline const type &operator*() const noexcept(std::is_nothrow_constructible<type>::value) {
    __lazy_make_default_instance(arena_, instance_);
    return *instance_;
  }

  inline type &operator*() noexcept(std::is_nothrow_constructible<type>::value) {
    __lazy_make_default_instance(arena_, instance_);
    return *instance_;
  }

  inline const arena_pointer &share_arena() const noexcept { return arena_; }

  inline const util::nostd::nonnull<element_pointer> &share_instance() const
      noexcept(std::is_nothrow_constructible<type>::value) {
    __lazy_make_default_instance(arena_, instance_);
    return instance_;
  }

  inline void swap(shared_message &other) noexcept {
    arena_.swap(other.arena_);
    instance_.swap(other.instance_);
  }

 private:
  inline static const std::shared_ptr<::google::protobuf::Arena> &get_arena_from(
      const std::shared_ptr<::google::protobuf::Arena> &source) {
    return source;
  }

  inline static const std::shared_ptr<::google::protobuf::Arena> &get_arena_from(const context &ctx) {
    return get_shared_arena(ctx);
  }

  template <class T, class A>
  inline static const std::shared_ptr<::google::protobuf::Arena> &get_arena_from(const shared_message<T, A> &sm) {
    return sm.share_arena();
  }

  template <class Alloc, class... Args>
  inline void make_instance(const Alloc &alloc, Args &&...args) const noexcept {
    if UTIL_UNLIKELY_CONDITION (instance_) {
      return;
    }

    if (arena_) {
      instance_ = util::memory::strong_rc_ptr<type>(
#if (defined(PROTOBUF_VERSION) && PROTOBUF_VERSION >= 5027000) || \
    (defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION >= 5027000)
          ::google::protobuf::Arena::Create<type>(arena_.get(), std::forward<Args>(args)...)
#else
          ::google::protobuf::Arena::CreateMessage<type>(arena_.get(), std::forward<Args>(args)...)
#endif
              ,
          arena_deletor());
      if UTIL_LIKELY_CONDITION (instance_) {
        return;
      }
    }

    instance_ = util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), std::forward<Args>(args)...);
  }

  template <class MT, class A>
  friend class shared_message;

  arena_pointer arena_;
  mutable element_pointer instance_;
};

template <class MessageType, class ArenaSourceType, class... Args>
UTIL_FORCEINLINE shared_message<MessageType> make_shared_message(ArenaSourceType &&arena_source, Args &&...args) {
  return shared_message<MessageType>(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source),
                                     std::forward<Args>(args)...);
}

template <class MessageType, class ArenaSourceType, class Alloc, class... Args>
UTIL_FORCEINLINE shared_message<MessageType> allocate_shared_message(const Alloc &alloc, ArenaSourceType &&arena_source,
                                                                     Args &&...args) {
  return shared_message<MessageType, Alloc>(__shared_message_allocator_tag{}, alloc,
                                            std::forward<ArenaSourceType>(arena_source), std::forward<Args>(args)...);
}

template <class MessageType, class ArenaSourceType, class... Args>
UTIL_FORCEINLINE shared_message<MessageType> clone_shared_message(ArenaSourceType &&arena_source, Args &&...args) {
  return shared_message<MessageType>(__shared_message_copy_tag{}, std::forward<ArenaSourceType>(arena_source),
                                     std::forward<Args>(args)...);
}

template <class MessageType, class ArenaSourceType, class Alloc, class... Args>
UTIL_FORCEINLINE shared_message<MessageType> allocate_clone_shared_message(const Alloc &alloc,
                                                                           ArenaSourceType &&arena_source,
                                                                           Args &&...args) {
  return shared_message<MessageType, Alloc>(__shared_message_allocator_copy_tag{}, alloc,
                                            std::forward<ArenaSourceType>(arena_source), std::forward<Args>(args)...);
}

}  // namespace rpc
