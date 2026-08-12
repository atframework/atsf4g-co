// Copyright 2026 atframework

// Offline unit tests for lobbysvr user_chat_manager (src/lobbysvr/service/logic/chat/user_chat_manager.h/.cpp) and
// the four chat task actions under src/lobbysvr/service/logic/chat/. Two users share one world channel and each owns
// a private channel; dtmq proxy traffic is answered by the SS mock engine, channel events are injected through
// rpc::dtmq::client_subscriber::global_receive_channel_event/global_tick, chat RPCs are driven as real CS upstream
// posts, and downstream notifications are verified through the CS mock records (content and flush timing).

#include <gsl/select-gsl.h>

// clang-format off
#include <config/compiler/protobuf_prefix.h>
// clang-format on

#include <google/protobuf/any.pb.h>
#include <protocol/common/com.struct.dtmq.common.pb.h>
#include <protocol/extension/atframework.pb.h>
#include <protocol/pbdesc/com.const.pb.h>
#include <protocol/pbdesc/com.protocol.chat.pb.h>
#include <protocol/pbdesc/com.struct.chat.pb.h>
#include <protocol/pbdesc/com.struct.dtmq.pb.h>
#include <protocol/pbdesc/dtmq_proxy.pb.h>

// clang-format off
#include <config/compiler/protobuf_suffix.h>
// clang-format on

#include <atframework/testing/mock_cs.h>
#include <atframework/testing/mock_discovery.h>
#include <atframework/testing/mock_ss.h>
#include <atframework/testing/runtime.h>

#include <config/extern_service_types.h>
#include <config/logic_config.h>
#include <rpc/internal/rpc_template_cs_message.h>
#include <time/time_utility.h>
#include <utility/protobuf_mini_dumper.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/handle_cs_rpc_lobbysvrclientservice.atfw.gen.h"
#include "data/user.h"
#include "data/session.h"
#include "frame/test_macros.h"
#include "logic/chat/user_chat_manager.h"
#include "logic/logic_server_setup.h"
#include "logic/session_manager.h"
#include "rpc/dtmq/dtmq_algorithm.h"
#include "rpc/dtmq/dtmq_client_subscriber.h"
#include "rpc/dtmq/dtmqproxysvrservice.atfw.gen.h"
#include "rpc/lobbysvrclientservice/lobbysvrclientservice.atfw.gen.h"

namespace {

constexpr uint64_t kGatewayNodeId = 0x82000001;
constexpr uint64_t kDtmqProxyNodeId = 0x1C0001;
constexpr uint64_t kUserId1 = 10001;
constexpr uint64_t kUserId2 = 10002;
constexpr uint32_t kZoneId = 1;

struct chat_test_user {
  user::ptr_t user_inst;
  atfw::testing::mock_client client;
  std::shared_ptr<session> sess;
  uint64_t session_id = 0;
};

std::string get_shared_subscriber_key() {
  return std::string{"server:"} + std::string{logic_config::me()->get_local_server_name()};
}

atframework::dtmq::DChannelIdKey make_world_channel_key() {
  atframework::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(static_cast<uint32_t>(atframework::chat::EN_CHAT_CHANNEL_TYPE_PUBLIC));
  channel_key.set_channel_id(rpc::dtmq::make_world_partition_channel_id(channel_key.channel_type(),
                                                                        logic_config::me()->get_local_world_id(), 0));
  return channel_key;
}

atframework::dtmq::DChannelIdKey make_private_channel_key(uint64_t user_id) {
  atframework::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(static_cast<uint32_t>(atframework::chat::EN_CHAT_CHANNEL_TYPE_PRIVATE));
  channel_key.set_channel_id(rpc::dtmq::make_unicast_channel_id(channel_key.channel_type(), kZoneId, user_id));
  return channel_key;
}

atframework::dtmq::DChannelIdKey make_sys_notification_channel_key() {
  atframework::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(static_cast<uint32_t>(atframework::chat::EN_CHAT_CHANNEL_TYPE_SYS_NOTIFICATION));
  channel_key.set_channel_id(
      rpc::dtmq::make_world_broadcast_channel_id(channel_key.channel_type(), logic_config::me()->get_local_world_id()));
  return channel_key;
}

atframework::dtmq::DChannelIdKey make_sys_announcement_channel_key() {
  atframework::dtmq::DChannelIdKey channel_key;
  channel_key.set_channel_type(static_cast<uint32_t>(atframework::chat::EN_CHAT_CHANNEL_TYPE_SYS_ANNOUNCEMENT));
  channel_key.set_channel_id(
      rpc::dtmq::make_world_broadcast_channel_id(channel_key.channel_type(), logic_config::me()->get_local_world_id()));
  return channel_key;
}

bool start_chat_runtime(atfw::testing::runtime &test) {
  atfw::testing::runtime_options options;
  options.features = {atfw::testing::feature::ss, atfw::testing::feature::cs};
  if (0 != test.start(options) || !test.is_running()) {
    CASE_MSG_INFO() << "runtime start failed: " << test.get_diagnostic() << '\n';
    return false;
  }
  return true;
}

bool setup_dtmq_proxy_node(atfw::testing::runtime &test) {
  atfw::testing::mock_node node;
  node.set_id(kDtmqProxyNodeId)
      .set_name("unit-test-dtmq-proxy")
      .set_type_id(static_cast<uint32_t>(atframework::component::logic_service_type::kDtMqProxySvr))
      .set_type_name("dtmq-proxysvr")
      .set_zone_id(kZoneId)
      .add_label("hpa_scaling_ready", "1");
  auto remote = test.discovery().add_node(node);
  if (!remote) {
    CASE_MSG_INFO() << "add dtmq proxy node failed\n";
    return false;
  }

  // Mock injection writes the global discovery set directly; the common-module discovery index only replays
  // existing nodes on reload.
  if (nullptr != logic_server_last_common_module()) {
    logic_server_last_common_module()->reload();
  }
  return true;
}

struct dtmq_proxy_capture {
  atframework::dtmq::SSChannelSubscribeReq last_subscribe_req;
  atframework::dtmq::SSChannelSendMessageReq last_send_message_req;
  int send_message_count = 0;
};

struct dtmq_proxy_rules {
  atfw::testing::ss_rule_handle subscribe;
  atfw::testing::ss_rule_handle send_message;
};

// Subscribe: acknowledge every heartbeat channel. Send message: accept and capture.
dtmq_proxy_rules mock_dtmq_proxy(atfw::testing::runtime &test, dtmq_proxy_capture &capture) {
  dtmq_proxy_rules rules;
  rules.subscribe = test.ss().mock(rpc::dtmq::packer::get_full_name_of_subscribe(),
                                   atframework::dtmq::SSChannelSubscribeReq::descriptor()->full_name(),
                                   atframework::dtmq::SSChannelSubscribeRsp::descriptor()->full_name(),
                                   [&capture](const atfw::testing::ss_request_view &request,
                                              google::protobuf::Message &response) -> rpc::result_code_type {
                                     const auto &req =
                                         static_cast<const atframework::dtmq::SSChannelSubscribeReq &>(request.body);
                                     capture.last_subscribe_req.CopyFrom(req);
                                     auto &rsp = static_cast<atframework::dtmq::SSChannelSubscribeRsp &>(response);
                                     for (const auto &heartbeat : req.heartbeat()) {
                                       auto *node = rsp.add_subscribe_node();
                                       if (nullptr != node) {
                                         node->mutable_channel_key()->CopyFrom(heartbeat.channel_key());
                                         node->set_server_id(kDtmqProxyNodeId);
                                       }
                                     }
                                     RPC_RETURN_CODE(0);
                                   });

  rules.send_message =
      test.ss().mock(rpc::dtmq::packer::get_full_name_of_send_message(),
                     atframework::dtmq::SSChannelSendMessageReq::descriptor()->full_name(),
                     atframework::dtmq::SSChannelSendMessageRsp::descriptor()->full_name(),
                     [&capture](const atfw::testing::ss_request_view &request,
                                google::protobuf::Message &response) -> rpc::result_code_type {
                       const auto &req = static_cast<const atframework::dtmq::SSChannelSendMessageReq &>(request.body);
                       capture.last_send_message_req.CopyFrom(req);
                       ++capture.send_message_count;
                       auto &rsp = static_cast<atframework::dtmq::SSChannelSendMessageRsp &>(response);
                       rsp.set_client_result(0);
                       rsp.set_message_sequence(req.message_content().sequence());
                       rsp.set_last_sequence(req.message_content().sequence());
                       rsp.set_last_hash_code(req.message_content().hash_code());
                       RPC_RETURN_CODE(0);
                     });

  return rules;
}

void pump_rounds(atfw::testing::runtime &test, int count) {
  for (int i = 0; i < count; ++i) {
    test.pump_once();
  }
}

bool run_sync_task(atfw::testing::runtime &test, const char *name,
                   std::function<rpc::result_code_type(rpc::context &)> fn) {
  auto task = test.run_task(name, std::chrono::seconds{2}, std::move(fn));
  if (task.empty()) {
    CASE_MSG_INFO() << name << " start failed: " << task.get_diagnostic() << '\n';
    return false;
  }
  auto result = test.wait(task, std::chrono::seconds{5});
  if (!result.task_exited || 0 != result.result_code) {
    CASE_MSG_INFO() << name << " failed, exited=" << result.task_exited << " code=" << result.result_code << '\n';
    return false;
  }
  return true;
}

// Create the mock client session, bind user <-> session and run user_chat_manager::login_init (what the login
// flow does for chat channels).
bool create_chat_user(atfw::testing::runtime &test, uint64_t user_id, uint64_t session_id, gsl::string_view openid,
                      chat_test_user &out) {
  out.client = test.cs().create_client(kGatewayNodeId, session_id);
  if (!out.client || 0 != out.client.add()) {
    CASE_MSG_INFO() << "client add failed for session " << session_id << '\n';
    return false;
  }

  session::key_t key;
  key.node_id = kGatewayNodeId;
  key.session_id = session_id;
  out.sess = session_manager::me()->find(key);
  if (!out.sess) {
    CASE_MSG_INFO() << "session not found for session " << session_id << '\n';
    return false;
  }

  out.session_id = session_id;
  out.user_inst = user::create(user_id, kZoneId, std::string{openid});
  if (!out.user_inst) {
    CASE_MSG_INFO() << "user create failed for user " << user_id << '\n';
    return false;
  }

  user::ptr_t user_inst = out.user_inst;
  std::shared_ptr<session> sess = out.sess;
  return run_sync_task(test, "chat.login_init", [&user_inst, &sess](rpc::context &ctx) -> rpc::result_code_type {
    sess->set_user(user_inst);
    user_inst->set_session(ctx, sess);
    int32_t res = RPC_AWAIT_CODE_RESULT(user_inst->get_user_chat_manager().login_init(ctx));
    RPC_RETURN_CODE(res);
  });
}

rpc::dtmq::client_subscriber::ptr_t find_channel(user_chat_manager &mgr, gsl::string_view channel_id) {
  rpc::dtmq::client_subscriber::ptr_t ret;
  mgr.foreach_channel(
      [&ret, &channel_id](const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t> &channel) {
        if (channel_id == channel->get_channel_key().channel_id()) {
          ret = channel;
          return false;
        }
        return true;
      });
  return ret;
}

size_t count_channels(user_chat_manager &mgr) {
  size_t ret = 0;
  mgr.foreach_channel([&ret](const atfw::util::nostd::nonnull<rpc::dtmq::client_subscriber::ptr_t> &) {
    ++ret;
    return true;
  });
  return ret;
}

template <class TRequest>
atframework::CSMsg pack_chat_cs_request(gsl::string_view rpc_full_name, const TRequest &req_body) {
  atframework::CSMsg msg;
  rpc::internal::setup_cs_rpc_request_header(*msg.mutable_head(), "1.0.0.0", "unit-test-client",
                                             "atframework.shared.LobbysvrClientService", rpc_full_name,
                                             TRequest::descriptor()->full_name());
  CASE_EXPECT_TRUE(req_body.SerializeToString(msg.mutable_body_bin()));
  msg.mutable_head()->set_timestamp(util::time::time_utility::get_now());
  return msg;
}

bool post_and_pump(atfw::testing::runtime &test, const atfw::testing::mock_client &client,
                   const atframework::CSMsg &msg) {
  if (0 != client.post(msg)) {
    CASE_MSG_INFO() << "post failed\n";
    return false;
  }
  pump_rounds(test, 6);
  return true;
}

using op_type = atfw::testing::cs_downstream_record::op_type;

// Find the latest downstream post record addressed to the session whose CSMsg head matches the rpc name (response
// or stream). Multiple sends/sync pushes of the same rpc name are distinguished by taking the last match.
const atfw::testing::cs_downstream_record *find_downstream_by_rpc_name(atfw::testing::runtime &test,
                                                                       uint64_t session_id,
                                                                       gsl::string_view rpc_full_name,
                                                                       atframework::CSMsg &out_msg) {
  const atfw::testing::cs_downstream_record *ret = nullptr;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto *record = test.cs().call_at(i);
    if (nullptr == record || op_type::post != record->op || record->session_id != session_id) {
      continue;
    }
    atframework::CSMsg cs_msg;
    if (!cs_msg.ParseFromString(record->message.body().post().content())) {
      continue;
    }
    if (cs_msg.head().has_rpc_response() && rpc_full_name == cs_msg.head().rpc_response().rpc_name()) {
      out_msg = std::move(cs_msg);
      ret = record;
      continue;
    }
    if (cs_msg.head().has_rpc_stream() && rpc_full_name == cs_msg.head().rpc_stream().rpc_name()) {
      out_msg = std::move(cs_msg);
      ret = record;
    }
  }
  return ret;
}

size_t count_channel_sync_records(atfw::testing::runtime &test, uint64_t session_id) {
  size_t ret = 0;
  for (size_t i = 0; i < test.cs().call_count(); ++i) {
    const auto *record = test.cs().call_at(i);
    if (nullptr == record || op_type::post != record->op || record->session_id != session_id) {
      continue;
    }
    atframework::CSMsg cs_msg;
    if (!cs_msg.ParseFromString(record->message.body().post().content())) {
      continue;
    }
    if (cs_msg.head().has_rpc_stream() &&
        cs_msg.head().rpc_stream().rpc_name() == rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync()) {
      ++ret;
    }
  }
  return ret;
}

bool parse_channel_sync_body(const atframework::CSMsg &cs_msg, atframework::chat::SCChatChannelSync &out) {
  return out.ParseFromString(cs_msg.body_bin());
}

atframework::dtmq::DChannelMessage make_channel_message(int64_t sequence, gsl::string_view sender_key,
                                                        uint32_t channel_type, gsl::string_view text) {
  atframework::dtmq::DChannelMessage msg;
  msg.set_sequence(sequence);
  msg.set_sender_key(sender_key.data(), sender_key.size());
  msg.set_channel_type(channel_type);
  msg.mutable_detail()->set_text(text.data(), text.size());
  *msg.mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  return msg;
}

// Build a list of messages with a valid hash chain (the WAL validates incremental messages via
// rpc::dtmq::calculate_hash_code(previous_hash, msg)). Returns the last message's hash code.
uint64_t chain_message_hashes(std::vector<atframework::dtmq::DChannelMessage> &msgs, uint64_t previous_hash) {
  for (auto &msg : msgs) {
    uint64_t hash = rpc::dtmq::calculate_hash_code(previous_hash, msg);
    msg.set_hash_code(hash);
    previous_hash = hash;
  }
  return msgs.empty() ? previous_hash : msgs.back().hash_code();
}

// Snapshot event: makes the subscriber ready and replaces its local cache.
atframework::dtmq::SSChannelEventSync make_snapshot_event(const atframework::dtmq::DChannelIdKey &channel_key,
                                                          int64_t create_sequence, int64_t last_sequence,
                                                          const std::vector<atframework::dtmq::DChannelMessage> &msgs) {
  atframework::dtmq::SSChannelEventSync event_sync;
  auto *metadata = event_sync.mutable_channel_snapshot()->mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_create_sequence(create_sequence);
  *metadata->mutable_create_timepoint() = protobuf_from_system_clock(std::chrono::system_clock::now());
  metadata->set_last_sequence(last_sequence);
  if (!msgs.empty()) {
    metadata->set_last_hash_code(msgs.back().hash_code());
  }
  for (const auto &msg : msgs) {
    *event_sync.mutable_channel_snapshot()->add_messages() = msg;
  }
  event_sync.add_subscriber_keys(get_shared_subscriber_key());
  return event_sync;
}

// Incremental event: appends messages to a ready subscriber.
atframework::dtmq::SSChannelEventSync make_incremental_event(
    const atframework::dtmq::DChannelIdKey &channel_key, int64_t last_sequence,
    const std::vector<atframework::dtmq::DChannelMessage> &msgs) {
  atframework::dtmq::SSChannelEventSync event_sync;
  auto *metadata = event_sync.mutable_channel_metadata();
  metadata->mutable_channel_key()->CopyFrom(channel_key);
  metadata->set_last_sequence(last_sequence);
  if (!msgs.empty()) {
    metadata->set_last_hash_code(msgs.back().hash_code());
  }
  for (const auto &msg : msgs) {
    *event_sync.add_channel_message() = msg;
  }
  event_sync.add_subscriber_keys(get_shared_subscriber_key());
  return event_sync;
}

bool receive_channel_event(atfw::testing::runtime &test, const atframework::dtmq::SSChannelEventSync &event_sync) {
  return run_sync_task(test, "chat.receive_event", [&event_sync](rpc::context &ctx) -> rpc::result_code_type {
    int32_t res = RPC_AWAIT_CODE_RESULT(
        rpc::dtmq::client_subscriber::global_receive_channel_event(ctx, kDtmqProxyNodeId, event_sync));
    RPC_RETURN_CODE(res);
  });
}

// user_chat_manager::global_tick only flushes once per 100ms wall window; sleep first so consecutive flushes in one
// case are not deduplicated, then pump to let the spawned push task finish.
void flush_pending_chat_messages(atfw::testing::runtime &test) {
  std::this_thread::sleep_for(std::chrono::milliseconds{110});
  static_cast<void>(run_sync_task(test, "chat.global_tick", [](rpc::context &ctx) -> rpc::result_code_type {
    user_chat_manager::global_tick(ctx);
    RPC_RETURN_CODE(0);
  }));
  pump_rounds(test, 6);
}

atframework::chat::DChatChannelKey make_chat_key_of_private_channel(uint64_t user_id, uint32_t zone_id) {
  atframework::chat::DChatChannelKey chat_key;
  chat_key.mutable_private_channel()->set_user_id(user_id);
  chat_key.mutable_private_channel()->set_zone_id(zone_id);
  return chat_key;
}

}  // namespace

// login_init creates world/private/sys-notification/sys-announcement subscribers for both users; the shared world
// (and sys) channels merge into one underlying subscription; the first global_tick batches one subscribe RPC to the
// dtmq proxy with exactly the five unique channels; chat_get_all_channel returns the four channel metadata.
CASE_TEST(lobbysvr_user_chat, login_init_subscribe_and_get_all_channel) {
  atfw::testing::runtime test;
  if (!start_chat_runtime(test)) {
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());

  dtmq_proxy_capture capture;
  auto proxy_rules = mock_dtmq_proxy(test, capture);
  CASE_EXPECT_TRUE(!!proxy_rules.subscribe);
  CASE_EXPECT_TRUE(!!proxy_rules.send_message);

  chat_test_user user1;
  chat_test_user user2;
  if (!create_chat_user(test, kUserId1, 3001, "openid-chat-1", user1) ||
      !create_chat_user(test, kUserId2, 3002, "openid-chat-2", user2)) {
    test.stop();
    return;
  }

  // Four channels per user; world channel data layer is shared between users, private channels are not.
  CASE_EXPECT_EQ(4, static_cast<int>(count_channels(user1.user_inst->get_user_chat_manager())));
  CASE_EXPECT_EQ(4, static_cast<int>(count_channels(user2.user_inst->get_user_chat_manager())));

  auto user1_world = find_channel(user1.user_inst->get_user_chat_manager(), make_world_channel_key().channel_id());
  auto user2_world = find_channel(user2.user_inst->get_user_chat_manager(), make_world_channel_key().channel_id());
  auto user1_private =
      find_channel(user1.user_inst->get_user_chat_manager(), make_private_channel_key(kUserId1).channel_id());
  auto user2_private =
      find_channel(user2.user_inst->get_user_chat_manager(), make_private_channel_key(kUserId2).channel_id());
  CASE_EXPECT_TRUE(!!user1_world);
  CASE_EXPECT_TRUE(!!user2_world);
  CASE_EXPECT_TRUE(!!user1_private);
  CASE_EXPECT_TRUE(!!user2_private);
  if (!user1_world || !user2_world || !user1_private || !user2_private) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(user1_world->get_shared_channel_identify(), user2_world->get_shared_channel_identify());
  CASE_EXPECT_NE(user1_private->get_shared_channel_identify(), user2_private->get_shared_channel_identify());
  CASE_EXPECT_EQ("user:1:10001", user1_world->get_subscriber_key());
  CASE_EXPECT_EQ("user:1:10002", user2_world->get_subscriber_key());

  // First tick sends the batched subscribe heartbeat.
  CASE_EXPECT_TRUE(run_sync_task(test, "dtmq.global_tick", [](rpc::context &ctx) -> rpc::result_code_type {
    rpc::dtmq::client_subscriber::global_tick(ctx);
    RPC_RETURN_CODE(0);
  }));
  pump_rounds(test, 6);

  CASE_EXPECT_EQ(1, static_cast<int>(test.ss().calls(rpc::dtmq::packer::get_full_name_of_subscribe())));
  std::set<std::string> heartbeat_channel_ids;
  for (const auto &heartbeat : capture.last_subscribe_req.heartbeat()) {
    heartbeat_channel_ids.insert(heartbeat.channel_key().channel_id());
  }
  CASE_EXPECT_EQ(5, static_cast<int>(heartbeat_channel_ids.size()));
  CASE_EXPECT_TRUE(heartbeat_channel_ids.end() != heartbeat_channel_ids.find(make_world_channel_key().channel_id()));
  CASE_EXPECT_TRUE(heartbeat_channel_ids.end() !=
                   heartbeat_channel_ids.find(make_private_channel_key(kUserId1).channel_id()));
  CASE_EXPECT_TRUE(heartbeat_channel_ids.end() !=
                   heartbeat_channel_ids.find(make_private_channel_key(kUserId2).channel_id()));
  CASE_EXPECT_TRUE(heartbeat_channel_ids.end() !=
                   heartbeat_channel_ids.find(make_sys_notification_channel_key().channel_id()));
  CASE_EXPECT_TRUE(heartbeat_channel_ids.end() !=
                   heartbeat_channel_ids.find(make_sys_announcement_channel_key().channel_id()));
  CASE_EXPECT_EQ(get_shared_subscriber_key(), capture.last_subscribe_req.subscriber().subscriber_key());

  // chat_get_all_channel through the real CS task action.
  atframework::chat::CSChatGetAllChannelReq get_all_req;
  auto packed = pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_get_all_channel(), get_all_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user1.client, packed));

  atframework::CSMsg rsp_msg;
  const auto *record = find_downstream_by_rpc_name(
      test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_get_all_channel(), rsp_msg);
  CASE_EXPECT_TRUE(nullptr != record);
  if (nullptr != record) {
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    atframework::chat::SCChatGetAllChannelRsp rsp_body;
    CASE_EXPECT_TRUE(rsp_body.ParseFromString(rsp_msg.body_bin()));
    CASE_EXPECT_EQ(4, rsp_body.channel_metadata_size());
    std::set<std::string> metadata_channel_ids;
    for (const auto &metadata : rsp_body.channel_metadata()) {
      metadata_channel_ids.insert(metadata.channel_key().channel_id());
    }
    CASE_EXPECT_TRUE(metadata_channel_ids.end() != metadata_channel_ids.find(make_world_channel_key().channel_id()));
    CASE_EXPECT_TRUE(metadata_channel_ids.end() !=
                     metadata_channel_ids.find(make_private_channel_key(kUserId1).channel_id()));
    CASE_EXPECT_TRUE(metadata_channel_ids.end() !=
                     metadata_channel_ids.find(make_sys_notification_channel_key().channel_id()));
    CASE_EXPECT_TRUE(metadata_channel_ids.end() !=
                     metadata_channel_ids.find(make_sys_announcement_channel_key().channel_id()));
  }

  CASE_EXPECT_EQ(0, test.stop());
}

// World channel: both users pull the initial snapshot (which installs the push callbacks), a snapshot event then
// reaches both sessions; a text message sent by user1 goes through the dtmq send_message contract; incremental
// events are batched until user_chat_manager::global_tick flushes one merged sync per session; an immediate second
// world message is rejected by the per-second write limit.
CASE_TEST(lobbysvr_user_chat, world_channel_snapshot_and_incremental_flow) {
  atfw::testing::runtime test;
  if (!start_chat_runtime(test)) {
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());

  dtmq_proxy_capture capture;
  auto proxy_rules = mock_dtmq_proxy(test, capture);
  CASE_EXPECT_TRUE(!!proxy_rules.subscribe);
  CASE_EXPECT_TRUE(!!proxy_rules.send_message);

  chat_test_user user1;
  chat_test_user user2;
  if (!create_chat_user(test, kUserId1, 3001, "openid-chat-1", user1) ||
      !create_chat_user(test, kUserId2, 3002, "openid-chat-2", user2)) {
    test.stop();
    return;
  }

  const std::string world_channel_id = make_world_channel_key().channel_id();
  const auto world_channel_type = make_world_channel_key().channel_type();

  // Both users pull the initial snapshot of the world channel (installs the push callbacks).
  for (auto *chat_user : {&user1, &user2}) {
    atframework::chat::CSChatGetChannelSnapshotReq snapshot_req;
    snapshot_req.set_channel_id(world_channel_id);
    auto packed =
        pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_get_channel_snapshot(), snapshot_req);
    CASE_EXPECT_TRUE(post_and_pump(test, chat_user->client, packed));

    atframework::CSMsg rsp_msg;
    const auto *record = find_downstream_by_rpc_name(
        test, chat_user->session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_get_channel_snapshot(), rsp_msg);
    CASE_EXPECT_TRUE(nullptr != record);
    if (nullptr == record) {
      test.stop();
      return;
    }
    CASE_EXPECT_EQ(0, rsp_msg.head().error_code());
    atframework::chat::SCChatGetChannelSnapshotRsp rsp_body;
    CASE_EXPECT_TRUE(rsp_body.ParseFromString(rsp_msg.body_bin()));
    CASE_EXPECT_EQ(world_channel_id, rsp_body.channel_snapshot().metadata().channel_key().channel_id());
  }

  // dtmq proxy publishes the world channel snapshot; both subscribers become ready and both sessions receive one
  // snapshot sync after the flush tick.
  int64_t base_sequence = 0;
  if (auto world = find_channel(user1.user_inst->get_user_chat_manager(), world_channel_id)) {
    base_sequence = world->get_last_message_sequence();
  }
  CASE_EXPECT_TRUE(
      receive_channel_event(test, make_snapshot_event(make_world_channel_key(), base_sequence + 1, base_sequence, {})));
  pump_rounds(test, 4);

  size_t user1_sync_before_flush = count_channel_sync_records(test, user1.session_id);
  size_t user2_sync_before_flush = count_channel_sync_records(test, user2.session_id);
  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(user1_sync_before_flush + 1, count_channel_sync_records(test, user1.session_id));
  CASE_EXPECT_EQ(user2_sync_before_flush + 1, count_channel_sync_records(test, user2.session_id));

  // user1 sends a text message into the world channel through the CS task.
  atframework::chat::CSChatSendMessageReq send_req;
  send_req.mutable_channel_key()->set_world_partition_channel(0);
  send_req.mutable_detail()->set_text("hello world from u1");
  auto packed_send = pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), send_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user1.client, packed_send));

  atframework::CSMsg send_rsp_msg;
  const auto *send_rsp_record = find_downstream_by_rpc_name(
      test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), send_rsp_msg);
  CASE_EXPECT_TRUE(nullptr != send_rsp_record);
  if (nullptr != send_rsp_record) {
    CASE_EXPECT_EQ(0, send_rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ(1, capture.send_message_count);
  CASE_EXPECT_EQ(world_channel_id, capture.last_send_message_req.channel_key().channel_id());
  CASE_EXPECT_EQ(world_channel_type, capture.last_send_message_req.channel_key().channel_type());
  CASE_EXPECT_EQ("user:1:10001", capture.last_send_message_req.message_content().sender_key());
  CASE_EXPECT_EQ("hello world from u1", capture.last_send_message_req.message_content().detail().text());

  // The dtmq proxy broadcasts two incremental messages. Before the flush tick nothing is pushed downstream.
  const int64_t msg1_seq = base_sequence + 1;
  const int64_t msg2_seq = base_sequence + 2;
  std::vector<atframework::dtmq::DChannelMessage> incremental_msgs{
      make_channel_message(msg1_seq, "user:1:10001", world_channel_type, "m1-from-u1"),
      make_channel_message(msg2_seq, "user:1:10002", world_channel_type, "m2-from-u2"),
  };
  chain_message_hashes(incremental_msgs, 0);
  CASE_EXPECT_TRUE(
      receive_channel_event(test, make_incremental_event(make_world_channel_key(), msg2_seq, incremental_msgs)));
  pump_rounds(test, 4);
  CASE_EXPECT_EQ(user1_sync_before_flush + 1, count_channel_sync_records(test, user1.session_id));
  CASE_EXPECT_EQ(user2_sync_before_flush + 1, count_channel_sync_records(test, user2.session_id));

  // The flush tick merges both messages into one sync per session, in sequence order.
  flush_pending_chat_messages(test);
  for (auto *chat_user : {&user1, &user2}) {
    atframework::CSMsg sync_msg;
    const auto *sync_record = find_downstream_by_rpc_name(
        test, chat_user->session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync(), sync_msg);
    CASE_EXPECT_TRUE(nullptr != sync_record);
    if (nullptr == sync_record) {
      continue;
    }
    atframework::chat::SCChatChannelSync sync_body;
    CASE_EXPECT_TRUE(parse_channel_sync_body(sync_msg, sync_body));
    CASE_EXPECT_EQ(1, sync_body.chat_channel_size());
    if (sync_body.chat_channel_size() > 0) {
      const auto &channel_data = sync_body.chat_channel(0);
      CASE_EXPECT_EQ(world_channel_id, channel_data.metadata().channel_key().channel_id());
      CASE_EXPECT_TRUE(channel_data.has_incremental());
      CASE_EXPECT_EQ(2, channel_data.incremental().message_list_size());
      if (channel_data.incremental().message_list_size() >= 2) {
        CASE_EXPECT_EQ(msg1_seq, channel_data.incremental().message_list(0).sequence());
        CASE_EXPECT_EQ("m1-from-u1", channel_data.incremental().message_list(0).detail().text());
        CASE_EXPECT_EQ(msg2_seq, channel_data.incremental().message_list(1).sequence());
        CASE_EXPECT_EQ("m2-from-u2", channel_data.incremental().message_list(1).detail().text());
      }
      CASE_EXPECT_EQ(msg2_seq, channel_data.metadata().last_message_sequence());
    }
  }

  // Two world sends back-to-back inside the same task: the second is rejected by the per-second write limit. The
  // first send may itself be rejected if an earlier world send in this case happened within the same wall-clock
  // second; either way the second send must report too-frequent.
  const auto world_key = make_world_channel_key();
  CASE_EXPECT_TRUE(
      run_sync_task(test, "chat.world_rate_limit", [&user1, &world_key](rpc::context &ctx) -> rpc::result_code_type {
        int32_t first =
            RPC_AWAIT_CODE_RESULT(user1.user_inst->get_user_chat_manager().send_text_message(ctx, world_key, "first"));
        int32_t second =
            RPC_AWAIT_CODE_RESULT(user1.user_inst->get_user_chat_manager().send_text_message(ctx, world_key, "second"));
        // If the first send was accepted (fresh second), the second must be rate-limited. If the first was already
        // rate-limited (same second as an earlier send), the second stays rate-limited too.
        if (0 == first) {
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_CHAT_WRITE_TOO_FREQUENT, second);
        } else {
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_CHAT_WRITE_TOO_FREQUENT, first);
          CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_CHAT_WRITE_TOO_FREQUENT, second);
        }
        RPC_RETURN_CODE(0);
      }));

  // Flush whatever may remain pending so later cases start clean.
  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(0, test.stop());
}

// Private channels: user2's private channel messages are only delivered to user2's session even though both users
// share the world channel; user1 may also write into user2's private channel (PRIVATE is writable by ACL); system
// channels reject writes at the manager ACL; event messages carry the Any payload through the send contract.
CASE_TEST(lobbysvr_user_chat, private_channel_isolation_and_access_check) {
  atfw::testing::runtime test;
  if (!start_chat_runtime(test)) {
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());

  dtmq_proxy_capture capture;
  auto proxy_rules = mock_dtmq_proxy(test, capture);
  CASE_EXPECT_TRUE(!!proxy_rules.subscribe);
  CASE_EXPECT_TRUE(!!proxy_rules.send_message);

  chat_test_user user1;
  chat_test_user user2;
  if (!create_chat_user(test, kUserId1, 3001, "openid-chat-1", user1) ||
      !create_chat_user(test, kUserId2, 3002, "openid-chat-2", user2)) {
    test.stop();
    return;
  }

  const auto user2_private_key = make_private_channel_key(kUserId2);
  const std::string user2_private_channel_id = user2_private_key.channel_id();

  // user2 pulls the initial snapshot of its private channel to install push callbacks.
  atframework::chat::CSChatGetChannelSnapshotReq snapshot_req;
  snapshot_req.set_channel_id(user2_private_channel_id);
  auto packed_snapshot =
      pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_get_channel_snapshot(), snapshot_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user2.client, packed_snapshot));

  // The private channel becomes ready via a snapshot event.
  int64_t private_base_sequence = 0;
  if (auto channel = find_channel(user2.user_inst->get_user_chat_manager(), user2_private_channel_id)) {
    private_base_sequence = channel->get_last_message_sequence();
  }
  CASE_EXPECT_TRUE(receive_channel_event(
      test, make_snapshot_event(user2_private_key, private_base_sequence + 1, private_base_sequence, {})));
  flush_pending_chat_messages(test);
  size_t user2_sync_baseline = count_channel_sync_records(test, user2.session_id);
  size_t user1_sync_baseline = count_channel_sync_records(test, user1.session_id);

  // user2 sends a message into its own private channel through the CS task.
  atframework::chat::CSChatSendMessageReq send_req;
  *send_req.mutable_channel_key() = make_chat_key_of_private_channel(kUserId2, kZoneId);
  send_req.mutable_detail()->set_text("secret-u2");
  auto packed_send = pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), send_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user2.client, packed_send));

  atframework::CSMsg send_rsp_msg;
  const auto *send_rsp_record = find_downstream_by_rpc_name(
      test, user2.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), send_rsp_msg);
  CASE_EXPECT_TRUE(nullptr != send_rsp_record);
  if (nullptr != send_rsp_record) {
    CASE_EXPECT_EQ(0, send_rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ(user2_private_channel_id, capture.last_send_message_req.channel_key().channel_id());
  CASE_EXPECT_EQ("user:1:10002", capture.last_send_message_req.message_content().sender_key());
  CASE_EXPECT_EQ("secret-u2", capture.last_send_message_req.message_content().detail().text());

  // The private channel event is only delivered to user2's session; user1's session receives nothing.
  const int64_t msg1_seq = private_base_sequence + 1;
  std::vector<atframework::dtmq::DChannelMessage> private_msgs{
      make_channel_message(msg1_seq, "user:1:10002", user2_private_key.channel_type(), "secret-u2"),
  };
  chain_message_hashes(private_msgs, 0);
  CASE_EXPECT_TRUE(receive_channel_event(test, make_incremental_event(user2_private_key, msg1_seq, private_msgs)));
  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(user2_sync_baseline + 1, count_channel_sync_records(test, user2.session_id));
  CASE_EXPECT_EQ(user1_sync_baseline, count_channel_sync_records(test, user1.session_id));

  atframework::CSMsg sync_msg;
  const auto *sync_record = find_downstream_by_rpc_name(
      test, user2.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync(), sync_msg);
  CASE_EXPECT_TRUE(nullptr != sync_record);
  if (nullptr != sync_record) {
    atframework::chat::SCChatChannelSync sync_body;
    CASE_EXPECT_TRUE(parse_channel_sync_body(sync_msg, sync_body));
    CASE_EXPECT_EQ(1, sync_body.chat_channel_size());
    if (sync_body.chat_channel_size() > 0) {
      CASE_EXPECT_EQ(user2_private_channel_id, sync_body.chat_channel(0).metadata().channel_key().channel_id());
      CASE_EXPECT_TRUE(sync_body.chat_channel(0).has_incremental());
      CASE_EXPECT_EQ(1, sync_body.chat_channel(0).incremental().message_list_size());
      if (sync_body.chat_channel(0).incremental().message_list_size() > 0) {
        CASE_EXPECT_EQ("secret-u2", sync_body.chat_channel(0).incremental().message_list(0).detail().text());
      }
    }
  }

  // user1 writes into user2's private channel too (PRIVATE channels are writable per manager ACL).
  atframework::chat::CSChatSendMessageReq cross_send_req;
  *cross_send_req.mutable_channel_key() = make_chat_key_of_private_channel(kUserId2, kZoneId);
  cross_send_req.mutable_detail()->set_text("from-u1");
  auto packed_cross_send =
      pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), cross_send_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user1.client, packed_cross_send));
  atframework::CSMsg cross_rsp_msg;
  const auto *cross_rsp_record = find_downstream_by_rpc_name(
      test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), cross_rsp_msg);
  CASE_EXPECT_TRUE(nullptr != cross_rsp_record);
  if (nullptr != cross_rsp_record) {
    CASE_EXPECT_EQ(0, cross_rsp_msg.head().error_code());
  }
  CASE_EXPECT_EQ("user:1:10001", capture.last_send_message_req.message_content().sender_key());

  // System channels reject writes at the manager ACL (they are not addressable from the CS chat API at all).
  const auto sys_notification_key = make_sys_notification_channel_key();
  const auto sys_announcement_key = make_sys_announcement_channel_key();
  CASE_EXPECT_TRUE(run_sync_task(
      test, "chat.sys_deny",
      [&user1, &sys_notification_key, &sys_announcement_key](rpc::context &ctx) -> rpc::result_code_type {
        int32_t res = RPC_AWAIT_CODE_RESULT(
            user1.user_inst->get_user_chat_manager().send_text_message(ctx, sys_notification_key, "notification"));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_CHAT_ACCESS_DENY_FOR_WRITE, res);
        res = RPC_AWAIT_CODE_RESULT(
            user1.user_inst->get_user_chat_manager().send_text_message(ctx, sys_announcement_key, "announcement"));
        CASE_EXPECT_EQ(PROJECT_NAMESPACE_ID::EN_ERR_CHAT_ACCESS_DENY_FOR_WRITE, res);
        RPC_RETURN_CODE(0);
      }));

  // Event messages carry the Any payload through the send contract.
  atframework::dtmq::DChannelSyncPoint event_payload;
  event_payload.set_last_sequence(42);
  google::protobuf::Any any_payload;
  CASE_EXPECT_TRUE(any_payload.PackFrom(event_payload));
  atframework::chat::CSChatSendMessageReq event_req;
  *event_req.mutable_channel_key() = make_chat_key_of_private_channel(kUserId1, kZoneId);
  event_req.mutable_detail()->mutable_event()->CopyFrom(any_payload);
  auto packed_event = pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), event_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user1.client, packed_event));
  atframework::CSMsg event_rsp_msg;
  const auto *event_rsp_record = find_downstream_by_rpc_name(
      test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_send_message(), event_rsp_msg);
  CASE_EXPECT_TRUE(nullptr != event_rsp_record);
  if (nullptr != event_rsp_record) {
    CASE_EXPECT_EQ(0, event_rsp_msg.head().error_code());
  }
  CASE_EXPECT_TRUE(capture.last_send_message_req.message_content().detail().has_event());
  const std::string expected_type_url =
      "type.googleapis.com/" + std::string(atframework::dtmq::DChannelSyncPoint::descriptor()->full_name());
  CASE_EXPECT_EQ(expected_type_url, capture.last_send_message_req.message_content().detail().event().type_url());

  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(0, test.stop());
}

// Heartbeat resync: incremental catch-up from the local cache when sequences only lag; a full snapshot when the
// last hash mismatches; nothing when up-to-date. Merge timing: a snapshot event arriving before the flush tick
// supersedes a queued incremental sync so only the snapshot is pushed.
CASE_TEST(lobbysvr_user_chat, heartbeat_resync_and_snapshot_merge) {
  atfw::testing::runtime test;
  if (!start_chat_runtime(test)) {
    return;
  }
  if (!setup_dtmq_proxy_node(test)) {
    test.stop();
    return;
  }
  CASE_EXPECT_EQ(0, handle::lobbysvrclientservice::register_handles_for_lobbysvrclientservice());

  dtmq_proxy_capture capture;
  auto proxy_rules = mock_dtmq_proxy(test, capture);
  CASE_EXPECT_TRUE(!!proxy_rules.subscribe);
  CASE_EXPECT_TRUE(!!proxy_rules.send_message);

  chat_test_user user1;
  if (!create_chat_user(test, kUserId1, 3001, "openid-chat-1", user1)) {
    test.stop();
    return;
  }

  const auto world_channel_key = make_world_channel_key();
  const std::string world_channel_id = world_channel_key.channel_id();

  atframework::chat::CSChatGetChannelSnapshotReq snapshot_req;
  snapshot_req.set_channel_id(world_channel_id);
  auto packed_snapshot =
      pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_get_channel_snapshot(), snapshot_req);
  CASE_EXPECT_TRUE(post_and_pump(test, user1.client, packed_snapshot));

  // Load three messages into the world channel cache through a snapshot event.
  int64_t base_sequence = 0;
  if (auto channel = find_channel(user1.user_inst->get_user_chat_manager(), world_channel_id)) {
    base_sequence = channel->get_last_message_sequence();
  }
  const int64_t msg1_seq = base_sequence + 1;
  const int64_t msg2_seq = base_sequence + 2;
  const int64_t msg3_seq = base_sequence + 3;
  std::vector<atframework::dtmq::DChannelMessage> snapshot_msgs{
      make_channel_message(msg1_seq, "user:1:10001", world_channel_key.channel_type(), "s1"),
      make_channel_message(msg2_seq, "user:1:10002", world_channel_key.channel_type(), "s2"),
      make_channel_message(msg3_seq, "user:1:10002", world_channel_key.channel_type(), "s3"),
  };
  chain_message_hashes(snapshot_msgs, 0);
  const uint64_t msg1_hash = snapshot_msgs[0].hash_code();
  const uint64_t msg3_hash = snapshot_msgs[2].hash_code();
  CASE_EXPECT_TRUE(
      receive_channel_event(test, make_snapshot_event(world_channel_key, msg1_seq, msg3_seq, snapshot_msgs)));
  flush_pending_chat_messages(test);
  test.cs().clear_history();

  auto post_heartbeat = [&test, &user1, &world_channel_key](int64_t last_sequence, uint64_t last_hash_code) {
    atframework::chat::CSChatChannelHeartbeatReq heartbeat_req;
    auto *sync_point = heartbeat_req.add_heartbeat_data();
    sync_point->mutable_channel_key()->CopyFrom(world_channel_key);
    sync_point->set_last_sequence(last_sequence);
    sync_point->set_last_hash_code(last_hash_code);
    auto packed =
        pack_chat_cs_request(rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_heartbeat(), heartbeat_req);
    return post_and_pump(test, user1.client, packed);
  };

  // Lagging behind with a matching hash: incremental catch-up from the local cache (msg2 + msg3).
  CASE_EXPECT_TRUE(post_heartbeat(msg1_seq, msg1_hash));
  {
    atframework::CSMsg sync_msg;
    const auto *sync_record = find_downstream_by_rpc_name(
        test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync(), sync_msg);
    CASE_EXPECT_TRUE(nullptr != sync_record);
    if (nullptr != sync_record) {
      atframework::chat::SCChatChannelSync sync_body;
      CASE_EXPECT_TRUE(parse_channel_sync_body(sync_msg, sync_body));
      CASE_EXPECT_EQ(1, sync_body.chat_channel_size());
      if (sync_body.chat_channel_size() > 0) {
        const auto &channel_data = sync_body.chat_channel(0);
        CASE_EXPECT_TRUE(channel_data.has_incremental());
        CASE_EXPECT_EQ(2, channel_data.incremental().message_list_size());
        if (channel_data.incremental().message_list_size() >= 2) {
          CASE_EXPECT_EQ(msg2_seq, channel_data.incremental().message_list(0).sequence());
          CASE_EXPECT_EQ(msg3_seq, channel_data.incremental().message_list(1).sequence());
        }
      }
    }
  }

  // Hash mismatch: full snapshot resync with all cached messages.
  test.cs().clear_history();
  CASE_EXPECT_TRUE(post_heartbeat(msg1_seq, 424242));
  {
    atframework::CSMsg sync_msg;
    const auto *sync_record = find_downstream_by_rpc_name(
        test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync(), sync_msg);
    CASE_EXPECT_TRUE(nullptr != sync_record);
    if (nullptr != sync_record) {
      atframework::chat::SCChatChannelSync sync_body;
      CASE_EXPECT_TRUE(parse_channel_sync_body(sync_msg, sync_body));
      CASE_EXPECT_EQ(1, sync_body.chat_channel_size());
      if (sync_body.chat_channel_size() > 0) {
        CASE_EXPECT_TRUE(sync_body.chat_channel(0).has_snapshot());
        CASE_EXPECT_EQ(3, sync_body.chat_channel(0).snapshot().message_list_size());
      }
    }
  }

  // Up-to-date: no sync push at all, only the heartbeat RPC response.
  test.cs().clear_history();
  CASE_EXPECT_TRUE(post_heartbeat(msg3_seq, msg3_hash));
  CASE_EXPECT_EQ(0, static_cast<int>(count_channel_sync_records(test, user1.session_id)));

  // Merge timing: a snapshot event supersedes a queued incremental sync before the flush.
  const int64_t msg4_seq = msg3_seq + 1;
  std::vector<atframework::dtmq::DChannelMessage> incremental_msgs{
      make_channel_message(msg4_seq, "user:1:10002", world_channel_key.channel_type(), "m4"),
  };
  chain_message_hashes(incremental_msgs, msg3_hash);
  CASE_EXPECT_TRUE(receive_channel_event(test, make_incremental_event(world_channel_key, msg4_seq, incremental_msgs)));
  std::vector<atframework::dtmq::DChannelMessage> snapshot_msgs2{
      make_channel_message(msg4_seq, "user:1:10002", world_channel_key.channel_type(), "m4"),
  };
  chain_message_hashes(snapshot_msgs2, msg3_hash);
  CASE_EXPECT_TRUE(
      receive_channel_event(test, make_snapshot_event(world_channel_key, msg1_seq, msg4_seq, snapshot_msgs2)));
  test.cs().clear_history();
  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(1, static_cast<int>(count_channel_sync_records(test, user1.session_id)));
  {
    atframework::CSMsg sync_msg;
    const auto *sync_record = find_downstream_by_rpc_name(
        test, user1.session_id, rpc::lobbysvrclientservice::packer::get_full_name_of_chat_channel_sync(), sync_msg);
    CASE_EXPECT_TRUE(nullptr != sync_record);
    if (nullptr != sync_record) {
      atframework::chat::SCChatChannelSync sync_body;
      CASE_EXPECT_TRUE(parse_channel_sync_body(sync_msg, sync_body));
      CASE_EXPECT_EQ(1, sync_body.chat_channel_size());
      if (sync_body.chat_channel_size() > 0) {
        CASE_EXPECT_TRUE(sync_body.chat_channel(0).has_snapshot());
        CASE_EXPECT_EQ(msg4_seq, sync_body.chat_channel(0).metadata().last_message_sequence());
      }
    }
  }

  flush_pending_chat_messages(test);
  CASE_EXPECT_EQ(0, test.stop());
}
