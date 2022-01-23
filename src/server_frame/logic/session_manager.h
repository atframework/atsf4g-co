// Copyright 2021 atframework

#pragma once

#include <design_pattern/singleton.h>

#include <config/server_frame_build_feature.h>

#include <data/session.h>
#include <utility/environment_helper.h>

#include <map>
#include <unordered_map>
#include <vector>

PROJECT_NAMESPACE_BEGIN
class CSMsg;
PROJECT_NAMESPACE_END

class session_manager : public util::design_pattern::singleton<session_manager> {
 public:
  using sess_ptr_t = session::ptr_t;
  using session_index_t = UTIL_ENV_AUTO_MAP(session::key_t, sess_ptr_t, session::compare_callback);
  using session_counter_t = std::map<uint64_t, size_t>;

 protected:
  session_manager();
  ~session_manager();

 public:
  int init();

  int proc();

  const sess_ptr_t find(const session::key_t& key) const;
  sess_ptr_t find(const session::key_t& key);

  sess_ptr_t create(const session::key_t& key);

  void remove(const session::key_t& key, int reason = 0);
  void remove(sess_ptr_t sess, int reason = 0);

  void remove_all();

  size_t size() const;

  int32_t broadcast_msg_to_client(const PROJECT_NAMESPACE_ID::CSMsg& msg);

 private:
  session_counter_t session_counter_;
  session_index_t all_sessions_;
  time_t last_proc_timepoint_;
};
