// Copyright 2021 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <data/session.h>

#include <map>
#include <unordered_map>

namespace atframework {
class CSMsg;
}

namespace rpc {
class context;
}

class session_manager {
 public:
  using sess_ptr_t = session::ptr_t;
  using session_index_t = std::unordered_map<session::key_t, sess_ptr_t, session::compare_callback>;
  using session_counter_t = std::map<uint64_t, size_t>;

#if defined(USER_SDK_DLL) && USER_SDK_DLL
#  if defined(USER_SDK_NATIVE) && USER_SDK_NATIVE
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(session_manager)
#  else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(session_manager)
#  endif
#else
  ATFW_UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(session_manager)
#endif

 private:
  USER_SDK_API session_manager();

 public:
  USER_SDK_API ~session_manager();

  USER_SDK_API int init();

  USER_SDK_API int proc();

  USER_SDK_API const sess_ptr_t find(const session::key_t& key) const;
  USER_SDK_API sess_ptr_t find(const session::key_t& key);

  USER_SDK_API sess_ptr_t create(const session::key_t& key);

  USER_SDK_API void remove(rpc::context& ctx, const session::key_t& key, int reason = 0,
                               atfw::util::nostd::string_view message = "");
  USER_SDK_API void remove(rpc::context& ctx, sess_ptr_t sess, int reason = 0,
                               atfw::util::nostd::string_view message = "");

  USER_SDK_API void remove_all(rpc::context& ctx, int32_t reason, atfw::util::nostd::string_view message = "");

  USER_SDK_API size_t size() const;

  USER_SDK_API int32_t broadcast_msg_to_client(const atframework::CSMsg& msg);

 private:
  session_counter_t session_counter_;
  session_index_t all_sessions_;
  time_t last_proc_timepoint_;
};
