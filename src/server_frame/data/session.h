// Copyright 2021 atframework

#pragma once

#include <design_pattern/nomovable.h>
#include <design_pattern/noncopyable.h>

#include <gsl/select-gsl.h>
#include <log/log_wrapper.h>

#include <config/server_frame_build_feature.h>

#include <dispatcher/task_type_traits.h>
#include <rpc/rpc_common_types.h>
#include <rpc/telemetry/rpc_global_service.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#ifdef __cpp_impl_three_way_comparison
#  include <compare>
#endif

namespace atframework {
class CSMsg;
class CSMsgHead;
}  // namespace atframework

namespace google {
namespace protobuf {
class Message;
}
}  // namespace google

namespace rpc {
class context;
};

class player_cache;

class session {
 public:
  using ptr_t = std::shared_ptr<session>;
  struct key_t {
    std::string node_name;
    uint64_t node_id;
    uint64_t session_id;

    SERVER_FRAME_API key_t();
    SERVER_FRAME_API key_t(uint64_t node_id, gsl::string_view node_name, uint64_t session_id);
    SERVER_FRAME_API ~key_t();

    SERVER_FRAME_API bool operator==(const key_t &r) const noexcept;
#if defined(__cpp_impl_three_way_comparison)
    SERVER_FRAME_API std::strong_ordering operator<=>(const key_t &r) const noexcept;
#else
    SERVER_FRAME_API bool operator!=(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator<(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator<=(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator>(const key_t &r) const noexcept;
    SERVER_FRAME_API bool operator>=(const key_t &r) const noexcept;
#endif
  };

  struct flag_t {
    enum type {
      EN_SESSION_FLAG_NONE = 0,
      EN_SESSION_FLAG_CLOSING = 0x0001,
      EN_SESSION_FLAG_CLOSED = 0x0002,
      EN_SESSION_FLAG_GATEWAY_REMOVED = 0x0004,
    };
  };

  class flag_guard_t {
   public:
    SERVER_FRAME_API flag_guard_t() noexcept;
    SERVER_FRAME_API ~flag_guard_t();

    SERVER_FRAME_API void setup(session &owner, flag_t::type f) noexcept;
    SERVER_FRAME_API void reset() noexcept;
    ATFW_UTIL_FORCEINLINE operator bool() const noexcept { return !!owner_ && !!flag_; }

    UTIL_DESIGN_PATTERN_NOCOPYABLE(flag_guard_t)
    UTIL_DESIGN_PATTERN_NOMOVABLE(flag_guard_t)

   private:
    flag_t::type flag_;
    session *owner_;
  };

 public:
  SERVER_FRAME_API session() noexcept;
  SERVER_FRAME_API ~session();

  ATFW_UTIL_FORCEINLINE void set_key(const key_t &key) noexcept { id_ = key; }
  ATFW_UTIL_FORCEINLINE const key_t &get_key() const noexcept { return id_; }

  ATFW_UTIL_FORCEINLINE void set_login_task_id(task_type_trait::id_type id) noexcept { login_task_id_ = id; }
  ATFW_UTIL_FORCEINLINE task_type_trait::id_type get_login_task_id() const noexcept { return login_task_id_; }

  ATFW_UTIL_FORCEINLINE bool check_flag(flag_t::type f) const noexcept {
    // 一次只能检查一个flag
    if (f & (f - 1)) {
      return false;
    }
    return !!(flags_ & static_cast<uint32_t>(f));
  }

  ATFW_UTIL_FORCEINLINE void set_flag(flag_t::type f, bool v) noexcept {
    // 一次只能设置一个flag
    if (f & (f - 1)) {
      return;
    }

    if (v) {
      flags_ |= static_cast<uint32_t>(f);
    } else {
      flags_ &= ~static_cast<uint32_t>(f);
    }
  }

  SERVER_FRAME_API bool is_closing() const noexcept;
  SERVER_FRAME_API bool is_closed() const noexcept;
  SERVER_FRAME_API bool is_valid() const noexcept;

  /**
   * @brief 监视关联的player
   * @param 关联的player
   */
  SERVER_FRAME_API void set_player(std::shared_ptr<player_cache> u) noexcept;

  /**
   * @brief 获取关联的session
   * @return 关联的session
   */
  SERVER_FRAME_API std::shared_ptr<player_cache> get_player() const noexcept;

  // 下行post包
  SERVER_FRAME_API int32_t send_msg_to_client(rpc::context &ctx, atframework::CSMsg &msg);
  SERVER_FRAME_API int32_t send_msg_to_client(rpc::context &ctx, atframework::CSMsg &msg, uint64_t server_sequence);

  SERVER_FRAME_API int32_t send_msg_to_client(const void *msg_data, size_t msg_size);

  SERVER_FRAME_API static int32_t broadcast_msg_to_client(uint64_t node_id, const atframework::CSMsg &msg);

  SERVER_FRAME_API static int32_t broadcast_msg_to_client(uint64_t node_id, const void *msg_data, size_t msg_size);

  struct compare_callback {
    SERVER_FRAME_API bool operator()(const key_t &l, const key_t &r) const noexcept;
    SERVER_FRAME_API size_t operator()(const key_t &hash_obj) const noexcept;
  };

  SERVER_FRAME_API int32_t send_kickoff(int32_t reason);

  SERVER_FRAME_API void write_actor_log_head(rpc::context &ctx, const atframework::CSMsg &msg, size_t byte_size,
                                             bool is_input);
  SERVER_FRAME_API void write_actor_log_body(rpc::context &ctx, const google::protobuf::Message &msg,
                                             const atframework::CSMsgHead &head, bool is_input);

  SERVER_FRAME_API void alloc_session_sequence(atframework::CSMsg &msg);

  ATFW_UTIL_FORCEINLINE uint64_t get_last_session_sequence() const { return session_sequence_; }

  ATFW_UTIL_FORCEINLINE uint32_t get_cached_zone_id() const noexcept { return cached_zone_id_; }

  ATFW_UTIL_FORCEINLINE uint64_t get_cached_user_id() const noexcept { return cached_user_id_; }

 private:
  void create_actor_log_writter();

 private:
  key_t id_;
  uint32_t flags_;
  std::weak_ptr<player_cache> player_;
  task_type_trait::id_type login_task_id_;
  uint64_t session_sequence_;
  uint32_t cached_zone_id_;
  uint64_t cached_user_id_;

  std::shared_ptr<atfw::util::log::log_wrapper> actor_log_writter_;
  opentelemetry::nostd::shared_ptr<opentelemetry::logs::Logger> actor_log_otel_;
};

namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID {
template <class CharT>
struct ATFW_UTIL_SYMBOL_VISIBLE formatter<session, CharT> : formatter<std::string> {
  template <class FormatContext>
  auto format(const session &sess, FormatContext &ctx) const {
    return LOG_WRAPPER_FWAPI_FORMAT_TO(ctx.out(), "session ({}){}:{}({}:{})", sess.get_key().node_name,
                                       sess.get_key().node_id, sess.get_key().session_id, sess.get_cached_zone_id(),
                                       sess.get_cached_user_id());
  }
};
}  // namespace LOG_WRAPPER_FWAPI_NAMESPACE_ID
