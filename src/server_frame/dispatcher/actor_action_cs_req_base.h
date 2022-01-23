// Copyright 2021 atframework
// Created by owent on 2016/11/14.
//

#pragma once

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/com.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <list>
#include <memory>
#include <utility>

#include "dispatcher/dispatcher_type_defines.h"

#include "dispatcher/actor_action_base.h"

class session;
class player_cache;

class actor_action_cs_req_base : public actor_action_req_base<PROJECT_NAMESPACE_ID::CSMsg> {
 public:
  using base_type = actor_action_req_base<PROJECT_NAMESPACE_ID::CSMsg>;
  using msg_type = base_type::msg_type;
  using msg_ref_type = msg_type &;
  using msg_cref_type = const msg_type &;

 protected:
  using base_type::get_request;

 public:
  using base_type::get_response_code;
  using base_type::get_result;
  using base_type::name;
  using base_type::set_response_code;
  using base_type::set_result;
  using base_type::operator();

 public:
  explicit actor_action_cs_req_base(dispatcher_start_data_t &&start_param);
  virtual ~actor_action_cs_req_base();

  std::pair<uint64_t, uint64_t> get_gateway_info() const;

  std::shared_ptr<session> get_session() const;

  std::shared_ptr<player_cache> get_player_cache() const;

  template <typename TPLAYER>
  std::shared_ptr<TPLAYER> get_player() const {
    return std::static_pointer_cast<TPLAYER>(get_player_cache());
  }

  msg_ref_type add_rsp_msg();

  std::list<msg_type> &get_rsp_list();
  const std::list<msg_type> &get_rsp_list() const;

  std::shared_ptr<dispatcher_implement> get_dispatcher() const override;
  const char *get_type_name() const override;

 protected:
  void send_response() override;
  void send_response(bool sync_dirty);

 private:
  mutable std::shared_ptr<session> session_inst_;
  std::list<msg_type> response_messages_;
  bool has_sync_dirty_;
};
