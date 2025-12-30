// Copyright 2024 atframework
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

#include <memory/object_allocator_metrics.h>

#include <config/server_frame_build_feature.h>

#include <memory>
#include <utility>

namespace rpc {
class context;

template <class MessageType, bool IsAbstruct>
struct __shared_message_default_constructible_helper;

template <class MessageType>
struct __shared_message_default_constructible_helper<MessageType, true> : public ::std::false_type {};

template <class MessageType>
struct __shared_message_default_constructible_helper<MessageType, false>
    : public ::std::conditional<::std::is_default_constructible<MessageType>::value, ::std::true_type,
                                ::std::false_type>::type {};

template <class MessageType>
struct __shared_message_default_constructible
    : public __shared_message_default_constructible_helper<
          atfw::util::nostd::remove_cvref_t<MessageType>,
          ::std::is_abstract<atfw::util::nostd::remove_cvref_t<MessageType>>::value> {};

struct ATFW_UTIL_SYMBOL_VISIBLE __shared_message_default_tag {};

struct ATFW_UTIL_SYMBOL_VISIBLE __shared_message_copy_tag {};

struct ATFW_UTIL_SYMBOL_VISIBLE __shared_message_allocator_tag {};

struct ATFW_UTIL_SYMBOL_VISIBLE __shared_message_allocator_copy_tag {};

template <class MessageType, class Allocator>
class ATFW_UTIL_SYMBOL_VISIBLE __shared_message_shared_base;

template <class MessageType, class Allocator, bool EnableLazyMakeInstance>
class ATFW_UTIL_SYMBOL_VISIBLE __shared_message_base;

template <class MessageType, class Allocator, bool WithDefaultConstructor>
class ATFW_UTIL_SYMBOL_VISIBLE __shared_message;

template <class, class>
struct __shared_message_internal_types_checker;

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, __shared_message_default_tag> : public ::std::true_type {};

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, __shared_message_copy_tag> : public ::std::true_type {};

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, __shared_message_allocator_tag> : public ::std::true_type {
};

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, __shared_message_allocator_copy_tag>
    : public ::std::true_type {};

template <class MessageType, class MT, class AT, bool WDCT>
struct __shared_message_internal_types_checker<MessageType, __shared_message<MT, AT, WDCT>> : public ::std::true_type {
};

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, std::shared_ptr<::google::protobuf::Arena>>
    : public ::std::true_type {};

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, ::google::protobuf::Arena> : public ::std::true_type {};

template <class MessageType>
struct __shared_message_internal_types_checker<MessageType, MessageType> : public ::std::true_type {};

template <class, class>
struct __shared_message_internal_types_checker : public ::std::false_type {};

template <class MessageType, class T>
struct __shared_message_internal_types
    : public __shared_message_internal_types_checker<atfw::util::nostd::remove_cvref_t<MessageType>,
                                                     atfw::util::nostd::remove_cvref_t<T>> {};

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
    : public __shared_message_internal_tag_types_checker<atfw::util::nostd::remove_cvref_t<T>> {};

template <class T>
struct __shared_message_internal_is_shared_message;

template <class MessageType, class Allocator, bool WithDefaultConstructor>
struct __shared_message_internal_is_shared_message<__shared_message<MessageType, Allocator, WithDefaultConstructor>>
    : public ::std::true_type {};

template <class MessageType, class Allocator>
struct __shared_message_internal_is_shared_message<__shared_message_shared_base<MessageType, Allocator>>
    : public ::std::true_type {};

template <class MessageType, class Allocator, bool EnableLazyMakeInstance>
struct __shared_message_internal_is_shared_message<
    __shared_message_base<MessageType, Allocator, EnableLazyMakeInstance>> : public ::std::true_type {};

template <class>
struct __shared_message_internal_is_shared_message : public ::std::false_type {};

SERVER_FRAME_API const std::shared_ptr<::google::protobuf::Arena> &get_shared_arena(const context &);

SERVER_FRAME_API void report_shared_message_defer_after_moved(const std::string &demangle_name);

enum __shared_message_flag : uint32_t {
  kDefault = 0,
  kMoved = 0x01,
};

struct ATFW_UTIL_SYMBOL_VISIBLE __shared_message_meta {
  uint32_t flags = static_cast<uint32_t>(__shared_message_flag::kDefault);

  inline __shared_message_meta() noexcept {}
};

template <class MessageType, class Allocator>
class ATFW_UTIL_SYMBOL_VISIBLE __shared_message_shared_base {
 public:
  using type = MessageType;
  using pointer = type *;
  using reference = type &;
  using const_reference = const type &;
  using allocator = Allocator;
  using arena_pointer = std::shared_ptr<::google::protobuf::Arena>;
  using element_pointer = atfw::util::memory::strong_rc_ptr<type>;

 protected:
  __shared_message_shared_base() noexcept {}

  template <class ArenaCtorParam>
  __shared_message_shared_base(ArenaCtorParam &&a) noexcept : arena_(std::forward<ArenaCtorParam>(a)) {}

  template <class ArenaCtorParam, class ElementCtorParam>
  __shared_message_shared_base(ArenaCtorParam &&a, ElementCtorParam &&e) noexcept
      : arena_(std::forward<ArenaCtorParam>(a)), instance_(std::forward<ElementCtorParam>(e)) {}

 public:
  inline const arena_pointer &share_arena() const noexcept { return arena_; }

  inline void mark_moved() noexcept { meta_.flags |= static_cast<uint32_t>(__shared_message_flag::kMoved); }

  inline bool is_moved() const noexcept {
    return 0 != (meta_.flags & static_cast<uint32_t>(__shared_message_flag::kMoved));
  }

 protected:
  struct arena_deletor {
    inline void operator()(pointer) const noexcept {}
  };

  template <class Alloc>
  ATFW_UTIL_FORCEINLINE static allocator __convert_allocator(const Alloc &alloc) noexcept {
    return allocator{alloc};
  }

  ATFW_UTIL_FORCEINLINE static const allocator &__convert_allocator(const allocator &alloc) noexcept { return alloc; }

  template <class Alloc>
  inline static void __make_instance(const arena_pointer &arena, element_pointer &instance,
                                     const Alloc &alloc) noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (instance) {
      return;
    }

    if (arena) {
      instance = atfw::util::memory::strong_rc_ptr<type>(
#if (defined(PROTOBUF_VERSION) && PROTOBUF_VERSION >= 5027000) || \
    (defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION >= 5027000)
          ::google::protobuf::Arena::Create<type>(arena.get())
#else
          ::google::protobuf::Arena::CreateMessage<type>(arena.get())
#endif
              ,
          arena_deletor());
      if UTIL_LIKELY_CONDITION (instance) {
        return;
      }
    }

    instance = atfw::util::memory::allocate_strong_rc<type>(__convert_allocator(alloc));
  }

  template <class Alloc, class Arg0>
  inline static void __make_instance(const arena_pointer &arena, element_pointer &instance, const Alloc &alloc,
                                     Arg0 &&arg0) noexcept(std::is_nothrow_constructible<type, Arg0>::value) {
    if UTIL_UNLIKELY_CONDITION (instance) {
      return;
    }

    if (arena) {
      instance = atfw::util::memory::strong_rc_ptr<type>(
#if (defined(PROTOBUF_VERSION) && PROTOBUF_VERSION >= 5027000) || \
    (defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION >= 5027000)
          ::google::protobuf::Arena::Create<type>(arena.get(), std::forward<Arg0>(arg0))
#else
          ::google::protobuf::Arena::CreateMessage<type>(arena.get())
#endif
              ,
          arena_deletor());
      if UTIL_LIKELY_CONDITION (instance) {
#if !((defined(PROTOBUF_VERSION) && PROTOBUF_VERSION >= 5027000) || \
      (defined(GOOGLE_PROTOBUF_VERSION) && GOOGLE_PROTOBUF_VERSION >= 5027000))
        *instance = std::forward<Arg0>(arg0);
#endif
        return;
      }
    }

    instance = atfw::util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), std::forward<Arg0>(arg0));
  }

  inline static const std::shared_ptr<::google::protobuf::Arena> &__get_arena_from(
      const std::shared_ptr<::google::protobuf::Arena> &source) {
    return source;
  }

  inline static const std::shared_ptr<::google::protobuf::Arena> &__get_arena_from(const context &ctx) {
    return get_shared_arena(ctx);
  }

  template <class T, class A>
  inline static const std::shared_ptr<::google::protobuf::Arena> &__get_arena_from(
      const __shared_message_shared_base<T, A> &sm) {
    return sm.share_arena();
  }

  arena_pointer arena_;
  mutable element_pointer instance_;
  __shared_message_meta meta_;
};

template <class MessageType, class Allocator>
class ATFW_UTIL_SYMBOL_VISIBLE
__shared_message_base<MessageType, Allocator, true> : public __shared_message_shared_base<MessageType, Allocator> {
 public:
  using base_type = __shared_message_shared_base<MessageType, Allocator>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

  using base_type::is_moved;
  using base_type::mark_moved;
  using base_type::share_arena;

 protected:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__get_arena_from;
  using base_type::__make_instance;
  using base_type::arena_;
  using base_type::instance_;
  using base_type::meta_;

  __shared_message_base() noexcept {}

  template <class ArenaCtorParam>
  __shared_message_base(ArenaCtorParam &&a) noexcept : base_type(std::forward<ArenaCtorParam>(a)) {}

  template <class ArenaCtorParam, class ElementCtorParam>
  __shared_message_base(ArenaCtorParam &&a, ElementCtorParam &&e) noexcept
      : base_type(std::forward<ArenaCtorParam>(a), std::forward<ElementCtorParam>(e)) {}

  ATFW_UTIL_FORCEINLINE static void __lazy_make_default_instance(
      const arena_pointer &arena,
      element_pointer &instance) noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (!instance) {
      Allocator alloc;
      __make_instance(arena, instance, alloc);
    }
  }
};

template <class MessageType, class Allocator>
class ATFW_UTIL_SYMBOL_VISIBLE
__shared_message_base<MessageType, Allocator, false> : public __shared_message_shared_base<MessageType, Allocator> {
 public:
  using base_type = __shared_message_shared_base<MessageType, Allocator>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

  using base_type::is_moved;
  using base_type::mark_moved;
  using base_type::share_arena;

 protected:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__get_arena_from;
  using base_type::__make_instance;
  using base_type::arena_;
  using base_type::instance_;
  using base_type::meta_;

  __shared_message_base() noexcept {}

  template <class ArenaCtorParam>
  __shared_message_base(ArenaCtorParam &&a) noexcept : base_type(std::forward<ArenaCtorParam>(a)) {}

  template <class ArenaCtorParam, class ElementCtorParam>
  __shared_message_base(ArenaCtorParam &&a, ElementCtorParam &&e) noexcept
      : base_type(std::forward<ArenaCtorParam>(a), std::forward<ElementCtorParam>(e)) {}
};

template <class MessageType, class Allocator>
class ATFW_UTIL_SYMBOL_VISIBLE __shared_message<MessageType, Allocator, false>
    : public __shared_message_base<atfw::util::nostd::remove_cvref_t<MessageType>, Allocator, false> {
 public:
  using base_type = __shared_message_base<atfw::util::nostd::remove_cvref_t<MessageType>, Allocator, false>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

  using base_type::is_moved;
  using base_type::mark_moved;
  using base_type::share_arena;

 private:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__get_arena_from;
  using base_type::__make_instance;
  using base_type::arena_;
  using base_type::instance_;
  using base_type::meta_;

 public:
  // This object can not pointer to a empty data, so disable move constructor and move assignment
  template <class OtherMessageType, class OtherAllocatorType,
            class = atfw::util::nostd::enable_if_t<
                __shared_message_default_constructible<OtherMessageType>::value &&
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message(
      __shared_message<OtherMessageType, OtherAllocatorType, true> &&other)  // NOLINT: runtime/explicit
      noexcept
      : base_type(std::move(other.arena_), std::move(other.instance_)) {
    other.mark_moved();
  }

  template <class OtherMessageType, class OtherAllocatorType,
            class = atfw::util::nostd::enable_if_t<
                __shared_message_default_constructible<OtherMessageType>::value &&
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message &operator=(__shared_message<OtherMessageType, OtherAllocatorType, true> &&other) noexcept {
    if UTIL_UNLIKELY_CONDITION (this == &other) {
      return *this;
    }

    arena_ = std::move(other.arena_);
    instance_ = std::move(other.instance_);

    other.mark_moved();
    return *this;
  }

  inline __shared_message(const __shared_message &other)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_default_constructible<type>::value)
      : base_type(other.arena_, other.share_instance()) {}

  inline __shared_message &operator=(const __shared_message &other) noexcept(
      std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (this == &other) {
      return *this;
    }

    arena_ = other.arena_;
    instance_ = other.share_instance();
    return *this;
  }

  template <class OtherMessageType, class OtherAllocatorType, bool OtherWithDefaultConstructor,
            class = atfw::util::nostd::enable_if_t<
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message(const __shared_message<OtherMessageType, OtherAllocatorType, OtherWithDefaultConstructor>
                              &other)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_default_constructible<OtherAllocatorType>::value)
      : base_type(other.arena_, other.share_instance()) {}

  template <class OtherMessageType, class OtherAllocatorType, bool OtherWithDefaultConstructor,
            class = atfw::util::nostd::enable_if_t<
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message &
  operator=(const __shared_message<OtherMessageType, OtherAllocatorType, OtherWithDefaultConstructor> &other) noexcept(
      std::is_nothrow_default_constructible<OtherAllocatorType>::value) {
    if UTIL_UNLIKELY_CONDITION (this == &other) {
      return *this;
    }

    arena_ = other.arena_;
    instance_ = other.share_instance();
    return *this;
  }

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_default_tag, ArenaSourceType &&arena_source,
                          type &&message) noexcept(std::is_nothrow_move_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      allocator alloc;
      instance_ = atfw::util::memory::allocate_strong_rc<type>(alloc, std::move(message));
    } else if (message.GetArena() == arena_.get()) {
      instance_ = atfw::util::memory::strong_rc_ptr<type>(&message, arena_deletor());
    } else {
      allocator alloc;
      __make_instance(arena_, instance_, alloc, std::move(message));
    }
  }
  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(ArenaSourceType &&arena_source,
                          type &&message) noexcept(std::is_nothrow_move_constructible<type>::value)
      : __shared_message(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source),
                         std::move(message)) {}

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_copy_tag, ArenaSourceType &&arena_source,
                          const type &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      allocator alloc;
      instance_ = atfw::util::memory::allocate_strong_rc<type>(alloc, message);
    } else {
      allocator alloc;
      __make_instance(arena_, instance_, alloc, message);
    }
  }

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_copy_tag tag, ArenaSourceType &&arena_source,
                          const __shared_message &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : __shared_message(tag, std::forward<ArenaSourceType>(arena_source), *message) {}

  template <class ArenaSourceType, class Arg0, class... Args>
  inline __shared_message(util::nostd::enable_if_t<!__shared_message_internal_types<type, Arg0>::value &&
                                                       !__shared_message_internal_tag_types<ArenaSourceType>::value,
                                                   __shared_message_default_tag>,
                          ArenaSourceType &&arena_source, Arg0 &&arg0, Args &&...args)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    allocator alloc;
    __make_instance(arena_, instance_, alloc, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
  }

  // Construct with specify allocator
  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_allocator_tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                          type &&message) noexcept(std::is_nothrow_move_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      instance_ = atfw::util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), std::move(message));
    } else if (message.GetArena() == arena_.get()) {
      instance_ = atfw::util::memory::strong_rc_ptr<type>(&message, arena_deletor());
    } else {
      __make_instance(arena_, instance_, alloc, std::move(message));
    }
  }

  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_allocator_copy_tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                          const type &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      instance_ = atfw::util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), message);
    } else {
      __make_instance(arena_, instance_, alloc, message);
    }
  }

  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_allocator_copy_tag tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                          const __shared_message &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : __shared_message(tag, alloc, std::forward<ArenaSourceType>(arena_source), *message) {}

  template <class Alloc, class ArenaSourceType, class Arg0, class... Args>
  inline __shared_message(util::nostd::enable_if_t<!__shared_message_internal_types<type, Arg0>::value &&
                                                       !__shared_message_internal_tag_types<ArenaSourceType>::value,
                                                   __shared_message_allocator_tag>,
                          const Alloc &alloc, ArenaSourceType &&arena_source, Arg0 &&arg0, Args &&...args)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    __make_instance(arena_, instance_, __convert_allocator(alloc), std::forward<Arg0>(arg0),
                    std::forward<Args>(args)...);
  }

  // Other member functions
  inline atfw::util::nostd::nonnull<const pointer> get() const
      noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    return instance_.get();
  }

  inline atfw::util::nostd::nonnull<pointer> get() noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    return instance_.get();
  }

  inline atfw::util::nostd::nonnull<const pointer> operator->() const
      noexcept(std::is_nothrow_default_constructible<type>::value) {
    return get();
  }

  inline atfw::util::nostd::nonnull<pointer> operator->() noexcept(std::is_nothrow_default_constructible<type>::value) {
    return get();
  }

  inline const type &operator*() const noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    return *instance_;
  }

  inline type &operator*() noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    return *instance_;
  }

  inline const atfw::util::nostd::nonnull<element_pointer> &share_instance() const
      noexcept(std::is_nothrow_default_constructible<type>::value) {
    return instance_;
  }

  inline void swap(__shared_message &other) noexcept {
    using std::swap;

    arena_.swap(other.arena_);
    instance_.swap(other.instance_);
    swap(meta_, other.meta_);
  }

  template <class ArenaSourceType>
  ATFW_UTIL_FORCEINLINE __shared_message clone(ArenaSourceType &&arena_source) noexcept(
      std::is_nothrow_constructible<__shared_message, __shared_message_copy_tag, ArenaSourceType,
                                    const __shared_message &>::value) {
    return __shared_message{__shared_message_copy_tag{}, std::forward<ArenaSourceType>(arena_source), *this};
  }

  template <class ArenaSourceType, class Alloc>
  ATFW_UTIL_FORCEINLINE __shared_message<type, Alloc, false>
  allocate_clone(const Alloc &alloc, ArenaSourceType &&arena_source) noexcept(
      std::is_nothrow_constructible<__shared_message, __shared_message_allocator_copy_tag, const Alloc &,
                                    const __shared_message &>::value) {
    return __shared_message<type, Alloc, false>{__shared_message_allocator_copy_tag{}, alloc,
                                                std::forward<ArenaSourceType>(arena_source), *this};
  }

 private:
  template <class MT, class A, bool WDC>
  friend class __shared_message;
};

template <class MessageType, class Allocator>
class ATFW_UTIL_SYMBOL_VISIBLE __shared_message<MessageType, Allocator, true>
    : public __shared_message_base<atfw::util::nostd::remove_cvref_t<MessageType>, Allocator, true> {
 public:
  using base_type = __shared_message_base<atfw::util::nostd::remove_cvref_t<MessageType>, Allocator, true>;
  using type = typename base_type::type;
  using pointer = typename base_type::pointer;
  using reference = typename base_type::reference;
  using const_reference = typename base_type::const_reference;
  using allocator = typename base_type::allocator;
  using arena_pointer = typename base_type::arena_pointer;
  using element_pointer = typename base_type::element_pointer;

  using base_type::is_moved;
  using base_type::mark_moved;
  using base_type::share_arena;

 private:
  using arena_deletor = typename base_type::arena_deletor;
  using base_type::__convert_allocator;
  using base_type::__get_arena_from;
  using base_type::__lazy_make_default_instance;
  using base_type::__make_instance;
  using base_type::arena_;
  using base_type::instance_;
  using base_type::meta_;

  // Members that same as __shared_message<MessageType, Allocator, false>
 public:
  template <class OtherMessageType, class OtherAllocatorType,
            class = atfw::util::nostd::enable_if_t<
                __shared_message_default_constructible<OtherMessageType>::value &&
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message(
      __shared_message<OtherMessageType, OtherAllocatorType, true> &&other)  // NOLINT: runtime/explicit
      noexcept
      : base_type(std::move(other.arena_), std::move(other.instance_)) {
    other.mark_moved();
  }

  template <class OtherMessageType, class OtherAllocatorType,
            class = atfw::util::nostd::enable_if_t<
                __shared_message_default_constructible<OtherMessageType>::value &&
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message &operator=(__shared_message<OtherMessageType, OtherAllocatorType, true> &&other) noexcept {
    if UTIL_UNLIKELY_CONDITION (this == &other) {
      return *this;
    }

    arena_ = std::move(other.arena_);
    instance_ = std::move(other.instance_);

    other.mark_moved();
    return *this;
  }

  inline __shared_message(const __shared_message &other)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_default_constructible<type>::value)
      : base_type(other.arena_, other.share_instance()) {}

  inline __shared_message &operator=(const __shared_message &other) noexcept(
      std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (this == &other) {
      return *this;
    }

    arena_ = other.arena_;
    instance_ = other.share_instance();
    return *this;
  }

  template <class OtherMessageType, class OtherAllocatorType, bool OtherWithDefaultConstructor,
            class = atfw::util::nostd::enable_if_t<
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message(const __shared_message<OtherMessageType, OtherAllocatorType, OtherWithDefaultConstructor>
                              &other)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_default_constructible<OtherAllocatorType>::value)
      : base_type(other.arena_, other.share_instance()) {}

  template <class OtherMessageType, class OtherAllocatorType, bool OtherWithDefaultConstructor,
            class = atfw::util::nostd::enable_if_t<
                std::is_base_of<type, atfw::util::nostd::remove_cvref_t<OtherMessageType>>::value>>
  inline __shared_message &
  operator=(const __shared_message<OtherMessageType, OtherAllocatorType, OtherWithDefaultConstructor> &other) noexcept(
      std::is_nothrow_default_constructible<OtherAllocatorType>::value) {
    if UTIL_UNLIKELY_CONDITION (this == &other) {
      return *this;
    }

    arena_ = other.arena_;
    instance_ = other.share_instance();
    return *this;
  }

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_default_tag, ArenaSourceType &&arena_source,
                          type &&message) noexcept(std::is_nothrow_move_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      allocator alloc;
      instance_ = atfw::util::memory::allocate_strong_rc<type>(alloc, std::move(message));
    } else if (message.GetArena() == arena_.get()) {
      instance_ = atfw::util::memory::strong_rc_ptr<type>(&message, arena_deletor());
    } else {
      allocator alloc;
      __make_instance(arena_, instance_, alloc, std::move(message));
    }
  }
  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(ArenaSourceType &&arena_source,
                          type &&message) noexcept(std::is_nothrow_move_constructible<type>::value)
      : __shared_message(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source),
                         std::move(message)) {}

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_copy_tag, ArenaSourceType &&arena_source,
                          const type &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      allocator alloc;
      instance_ = atfw::util::memory::allocate_strong_rc<type>(alloc, message);
    } else {
      allocator alloc;
      __make_instance(arena_, instance_, alloc, message);
    }
  }

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_copy_tag tag, ArenaSourceType &&arena_source,
                          const __shared_message &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : __shared_message(tag, std::forward<ArenaSourceType>(arena_source), *message) {}

  template <class ArenaSourceType, class Arg0, class... Args>
  inline __shared_message(util::nostd::enable_if_t<!__shared_message_internal_types<type, Arg0>::value &&
                                                       !__shared_message_internal_tag_types<ArenaSourceType>::value,
                                                   __shared_message_default_tag>,
                          ArenaSourceType &&arena_source, Arg0 &&arg0, Args &&...args)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    allocator alloc;
    __make_instance(arena_, instance_, alloc, std::forward<Arg0>(arg0), std::forward<Args>(args)...);
  }

  // Construct with specify allocator
  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_move_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_allocator_tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                          type &&message) noexcept(std::is_nothrow_move_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      instance_ = atfw::util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), std::move(message));
    } else if (message.GetArena() == arena_.get()) {
      instance_ = atfw::util::memory::strong_rc_ptr<type>(&message, arena_deletor());
    } else {
      __make_instance(arena_, instance_, alloc, std::move(message));
    }
  }

  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_allocator_copy_tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                          const type &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    if (message.GetArena() == nullptr) {
      instance_ = atfw::util::memory::allocate_strong_rc<type>(__convert_allocator(alloc), message);
    } else {
      __make_instance(arena_, instance_, alloc, message);
    }
  }

  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<std::is_copy_constructible<type>::value &&
                                                   !__shared_message_internal_tag_types<ArenaSourceType>::value>>
  inline __shared_message(__shared_message_allocator_copy_tag tag, const Alloc &alloc, ArenaSourceType &&arena_source,
                          const __shared_message &message) noexcept(std::is_nothrow_copy_constructible<type>::value)
      : __shared_message(tag, alloc, std::forward<ArenaSourceType>(arena_source), *message) {}

  template <class Alloc, class ArenaSourceType, class Arg0, class... Args>
  inline __shared_message(util::nostd::enable_if_t<!__shared_message_internal_types<type, Arg0>::value &&
                                                       !__shared_message_internal_tag_types<ArenaSourceType>::value,
                                                   __shared_message_allocator_tag>,
                          const Alloc &alloc, ArenaSourceType &&arena_source, Arg0 &&arg0, Args &&...args)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {
    __make_instance(arena_, instance_, __convert_allocator(alloc), std::forward<Arg0>(arg0),
                    std::forward<Args>(args)...);
  }

  // Other member functions
  inline atfw::util::nostd::nonnull<const pointer> get() const
      noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    __lazy_make_default_instance(arena_, instance_);
    return instance_.get();
  }

  inline atfw::util::nostd::nonnull<pointer> get() noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    __lazy_make_default_instance(arena_, instance_);
    return instance_.get();
  }

  inline atfw::util::nostd::nonnull<const pointer> operator->() const
      noexcept(std::is_nothrow_default_constructible<type>::value) {
    return get();
  }

  inline atfw::util::nostd::nonnull<pointer> operator->() noexcept(std::is_nothrow_default_constructible<type>::value) {
    return get();
  }

  inline const type &operator*() const noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    __lazy_make_default_instance(arena_, instance_);
    return *instance_;
  }

  inline type &operator*() noexcept(std::is_nothrow_default_constructible<type>::value) {
    if UTIL_UNLIKELY_CONDITION (is_moved()) {
      report_shared_message_defer_after_moved(
          ::atfw::memory::object_allocator_metrics_controller::parse_demangle_name<type>());
    }

    __lazy_make_default_instance(arena_, instance_);
    return *instance_;
  }

  inline const atfw::util::nostd::nonnull<element_pointer> &share_instance() const
      noexcept(std::is_nothrow_default_constructible<type>::value) {
    __lazy_make_default_instance(arena_, instance_);
    return instance_;
  }

  inline void swap(__shared_message &other) noexcept {
    using std::swap;

    arena_.swap(other.arena_);
    instance_.swap(other.instance_);

    swap(meta_, other.meta_);
  }

  template <class ArenaSourceType>
  ATFW_UTIL_FORCEINLINE __shared_message clone(ArenaSourceType &&arena_source) noexcept(
      std::is_nothrow_constructible<__shared_message, __shared_message_copy_tag, ArenaSourceType,
                                    const __shared_message &>::value) {
    return __shared_message{__shared_message_copy_tag{}, std::forward<ArenaSourceType>(arena_source), *this};
  }

  template <class ArenaSourceType, class Alloc>
  ATFW_UTIL_FORCEINLINE __shared_message<type, Alloc, true>
  allocate_clone(const Alloc &alloc, ArenaSourceType &&arena_source) noexcept(
      std::is_nothrow_constructible<__shared_message, __shared_message_allocator_copy_tag, const Alloc &,
                                    const __shared_message &>::value) {
    return __shared_message<type, Alloc, true>{__shared_message_allocator_copy_tag{}, alloc,
                                               std::forward<ArenaSourceType>(arena_source), *this};
  }

 public:
  // Construct with default allocator
  inline __shared_message() noexcept(std::is_nothrow_default_constructible<type>::value) {}

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<!__shared_message_internal_tag_types<ArenaSourceType>::value &&
                                                   !__shared_message_internal_is_shared_message<
                                                       atfw::util::nostd::remove_cvref_t<ArenaSourceType>>::value>>
  inline __shared_message(__shared_message_default_tag,
                          ArenaSourceType &&arena_source) noexcept(std::is_nothrow_default_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {}

  template <class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<!__shared_message_internal_tag_types<ArenaSourceType>::value &&
                                                   !__shared_message_internal_is_shared_message<
                                                       atfw::util::nostd::remove_cvref_t<ArenaSourceType>>::value>>
  inline __shared_message(ArenaSourceType &&arena_source)  // NOLINT: runtime/explicit
      noexcept(std::is_nothrow_default_constructible<type>::value)
      : __shared_message(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source)) {}

  // Construct with specify allocator
  template <class Alloc, class ArenaSourceType,
            class = atfw::util::nostd::enable_if_t<!__shared_message_internal_tag_types<ArenaSourceType>::value &&
                                                   !__shared_message_internal_is_shared_message<
                                                       atfw::util::nostd::remove_cvref_t<ArenaSourceType>>::value>>
  inline __shared_message(__shared_message_allocator_tag, const Alloc & /*alloc*/,
                          ArenaSourceType &&arena_source) noexcept(std::is_nothrow_default_constructible<type>::value)
      : base_type(__get_arena_from(std::forward<ArenaSourceType>(arena_source))) {}

 private:
  template <class MT, class A, bool WDC>
  friend class __shared_message;
};

template <class MessageType, class Allocator = ::std::allocator<atfw::util::nostd::remove_cvref_t<MessageType>>>
using shared_abstract_message = __shared_message<MessageType, Allocator, false>;

template <class MessageType, class Allocator = ::std::allocator<atfw::util::nostd::remove_cvref_t<MessageType>>>
using shared_message = __shared_message<MessageType, Allocator, true>;

template <class MessageType, class Allocator = ::std::allocator<atfw::util::nostd::remove_cvref_t<MessageType>>>
using shared_auto_message =
    __shared_message<MessageType, Allocator, __shared_message_default_constructible<MessageType>::value>;

template <class MessageType, class ArenaSourceType, class... Args>
ATFW_UTIL_FORCEINLINE shared_auto_message<MessageType>
make_shared_message(ArenaSourceType &&arena_source, Args &&...args) noexcept(
    std::is_nothrow_constructible<shared_auto_message<MessageType>, __shared_message_default_tag, ArenaSourceType,
                                  Args...>::value) {
  return shared_auto_message<MessageType>(__shared_message_default_tag{}, std::forward<ArenaSourceType>(arena_source),
                                          std::forward<Args>(args)...);
}

template <class MessageType, class ArenaSourceType, class Alloc, class... Args>
ATFW_UTIL_FORCEINLINE shared_auto_message<MessageType>
allocate_shared_message(const Alloc &alloc, ArenaSourceType &&arena_source, Args &&...args) noexcept(
    std::is_nothrow_constructible<shared_auto_message<MessageType>, __shared_message_allocator_tag, const Alloc &,
                                  ArenaSourceType, Args...>::value) {
  return shared_auto_message<MessageType, Alloc>(__shared_message_allocator_tag{}, alloc,
                                                 std::forward<ArenaSourceType>(arena_source),
                                                 std::forward<Args>(args)...);
}

template <class MessageType, class ArenaSourceType, class... Args>
ATFW_UTIL_FORCEINLINE shared_auto_message<MessageType> clone_shared_message(
    ArenaSourceType &&arena_source,
    Args &&...args) noexcept(std::is_nothrow_constructible<shared_auto_message<MessageType>, __shared_message_copy_tag,
                                                           ArenaSourceType, Args...>::value) {
  return shared_auto_message<MessageType>(__shared_message_copy_tag{}, std::forward<ArenaSourceType>(arena_source),
                                          std::forward<Args>(args)...);
}

template <class MessageType, class ArenaSourceType, class Alloc, class... Args>
ATFW_UTIL_FORCEINLINE shared_auto_message<MessageType>
allocate_clone_shared_message(const Alloc &alloc, ArenaSourceType &&arena_source, Args &&...args) noexcept(
    std::is_nothrow_constructible<shared_auto_message<MessageType>, __shared_message_allocator_copy_tag, const Alloc &,
                                  ArenaSourceType, Args...>::value) {
  return shared_auto_message<MessageType, Alloc>(__shared_message_allocator_copy_tag{}, alloc,
                                                 std::forward<ArenaSourceType>(arena_source),
                                                 std::forward<Args>(args)...);
}

}  // namespace rpc
