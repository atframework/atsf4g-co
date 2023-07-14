// Copyright 2021 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <data/session.h>
#include <utility/environment_helper.h>

#include <map>
#include <unordered_map>
#include <vector>

namespace atframework {
class CSMsg;
}

class session_manager {
 public:
  using sess_ptr_t = session::ptr_t;
  using session_index_t = UTIL_ENV_AUTO_MAP(session::key_t, sess_ptr_t, session::compare_callback);
  using session_counter_t = std::map<uint64_t, size_t>;

#if defined(SERVER_FRAME_API_DLL) && SERVER_FRAME_API_DLL
#  if defined(SERVER_FRAME_API_NATIVE) && SERVER_FRAME_API_NATIVE
  UTIL_DESIGN_PATTERN_SINGLETON_EXPORT_DECL(session_manager)
#  else
  UTIL_DESIGN_PATTERN_SINGLETON_IMPORT_DECL(session_manager)
#  endif
#else
  UTIL_DESIGN_PATTERN_SINGLETON_VISIBLE_DECL(session_manager)
#endif

 private:
  SERVER_FRAME_CONFIG_API session_manager();

 public:
  SERVER_FRAME_CONFIG_API ~session_manager();

  SERVER_FRAME_CONFIG_API int init();

  SERVER_FRAME_CONFIG_API int proc();

  SERVER_FRAME_CONFIG_API const sess_ptr_t find(const session::key_t& key) const;
  SERVER_FRAME_CONFIG_API sess_ptr_t find(const session::key_t& key);

  SERVER_FRAME_CONFIG_API sess_ptr_t create(const session::key_t& key);

  SERVER_FRAME_CONFIG_API void remove(const session::key_t& key, int reason = 0);
  SERVER_FRAME_CONFIG_API void remove(sess_ptr_t sess, int reason = 0);

  SERVER_FRAME_CONFIG_API void remove_all(int32_t reason);

  SERVER_FRAME_CONFIG_API size_t size() const;

  SERVER_FRAME_CONFIG_API int32_t broadcast_msg_to_client(const atframework::CSMsg& msg);

 private:
  session_counter_t session_counter_;
  session_index_t all_sessions_;
  time_t last_proc_timepoint_;
};
