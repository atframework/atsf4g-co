//
// Created by owent on 2016/9/27.
//

#include <sstream>

#include <log/log_wrapper.h>

#include <atframe/atapp.h>
#include <libatbus.h>
#include <libatbus_protocol.h>
#include <proto_base.h>


#include <config/atframe_service_types.h>
#include <config/extern_service_types.h>


#include "ss_msg_dispatcher.h"

#include <config/compiler/protobuf_prefix.h>

#include <protocol/pbdesc/svr.const.err.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <protocol/pbdesc/svr.protocol.pb.h>

#include <config/compiler/protobuf_suffix.h>

#include <utility/protobuf_mini_dumper.h>

#include <rpc/rpc_utils.h>

ss_msg_dispatcher::ss_msg_dispatcher() : sequence_allocator_(0) {}
ss_msg_dispatcher::~ss_msg_dispatcher() {}

int32_t ss_msg_dispatcher::init() {
    sequence_allocator_ = static_cast<uint64_t>((util::time::time_utility::get_sys_now() - hello::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET) << 23) +
                          static_cast<uint64_t>(util::time::time_utility::get_now_usec() << 3);
    return 0;
}

int32_t ss_msg_dispatcher::reload() {
    service_name_ = get_app()->get_app_name();
    return dispatcher_implement::reload();
}

uint64_t ss_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
    hello::SSMsg *real_msg = get_protobuf_msg<hello::SSMsg>(raw_msg);
    if (NULL == real_msg) {
        return 0;
    }

    return real_msg->head().dst_task_id();
}

ss_msg_dispatcher::msg_type_t ss_msg_dispatcher::pick_msg_type_id(msg_raw_t &raw_msg) { return 0; }

const std::string &ss_msg_dispatcher::pick_rpc_name(msg_raw_t &raw_msg) {
    hello::SSMsg *real_msg = get_protobuf_msg<hello::SSMsg>(raw_msg);
    if (NULL == real_msg) {
        return get_empty_string();
    }

    if (!real_msg->has_head()) {
        return get_empty_string();
    }

    if (real_msg->head().has_rpc_request()) {
        return real_msg->head().rpc_request().rpc_name();
    }

    if (real_msg->head().has_rpc_stream()) {
        return real_msg->head().rpc_stream().rpc_name();
    }

    return get_empty_string();
}

ss_msg_dispatcher::msg_op_type_t ss_msg_dispatcher::pick_msg_op_type(msg_raw_t &raw_msg) {
    hello::SSMsg *real_msg = get_protobuf_msg<hello::SSMsg>(raw_msg);
    if (NULL == real_msg) {
        return hello::EN_MSG_OP_TYPE_MIXUP;
    }

    if (false == hello::EnMsgOpType_IsValid(real_msg->head().op_type())) {
        return hello::EN_MSG_OP_TYPE_MIXUP;
    }

    return static_cast<msg_op_type_t>(real_msg->head().op_type());
}

const atframework::DispatcherOptions *ss_msg_dispatcher::get_options_by_message_type(msg_type_t msg_type) { return NULL; }

int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, hello::SSMsg &ss_msg) {
    if (0 == ss_msg.head().sequence()) {
        ss_msg.mutable_head()->set_sequence(allocate_sequence());
    }

    size_t msg_buf_len = ss_msg.ByteSizeLong();
    size_t tls_buf_len = atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
    if (msg_buf_len > tls_buf_len) {
        FWLOGERROR("send to proc [{:#x}: {}] failed: require {}, only have {}", bus_id, get_app()->convert_app_id_to_string(bus_id), msg_buf_len, tls_buf_len);
        return hello::err::EN_SYS_BUFF_EXTEND;
    }

    ::google::protobuf::uint8 *buf_start =
        reinterpret_cast< ::google::protobuf::uint8 *>(atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
    ss_msg.SerializeWithCachedSizesToArray(buf_start);
    FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes\n{}", bus_id, get_app()->convert_app_id_to_string(bus_id), msg_buf_len,
               protobuf_mini_dumper_get_readable(ss_msg));

    return send_to_proc(bus_id, buf_start, msg_buf_len);
}

int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, const void *msg_buf, size_t msg_len) {
    atapp::app *owner = get_app();
    if (NULL == owner) {
        FWLOGERROR("module not attached to a atapp");
        return hello::err::EN_SYS_INIT;
    }

    if (!owner->get_bus_node()) {
        FWLOGERROR("owner app has no valid bus node");
        return hello::err::EN_SYS_INIT;
    }

    int res = owner->get_bus_node()->send_data(bus_id, atframe::component::message_type::EN_ATST_SS_MSG, msg_buf, msg_len, false);

    if (res < 0) {
        FWLOGERROR("send msg to proc [{:#x}: {}] {} bytes failed, res: {}", bus_id, get_app()->convert_app_id_to_string(bus_id), msg_len, res);
    } else {
        FWLOGDEBUG("send msg to proc [{:#x}: {}] {} bytes success", bus_id, get_app()->convert_app_id_to_string(bus_id), msg_len);
    }

    return res;
}

int32_t ss_msg_dispatcher::dispatch(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg) {
    if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.type) {
        FWLOGERROR("message type {} invalid", msg.type);
        return hello::err::EN_SYS_PARAM;
    }

    if (0 == source.id || (NULL == msg.data && msg.data_size > 0)) {
        FWLOGERROR("receive a message from unknown source or without data content");
        return hello::err::EN_SYS_PARAM;
    }

    uint64_t from_server_id = source.id;

    rpc::context  ctx;
    hello::SSMsg *ss_msg = ctx.create<hello::SSMsg>();
    if (nullptr == ss_msg) {
        FWLOGERROR("{} create message instance failed", name());
        return hello::err::EN_SYS_MALLOC;
    }

    dispatcher_msg_raw_t callback_msg = dispatcher_make_default<dispatcher_msg_raw_t>();

    int32_t ret = unpack_protobuf_msg(*ss_msg, callback_msg, msg.data, msg.data_size);
    if (ret != 0) {
        FWLOGERROR("{} unpack received message from [{:#x}: {}] failed, res: {}", name(), from_server_id, get_app()->convert_app_id_to_string(from_server_id),
                   ret);
        return ret;
    }
    ss_msg->mutable_head()->set_bus_id(from_server_id);

    ret = on_receive_message(ctx, callback_msg, nullptr, ss_msg->head().sequence());
    if (ret < 0) {
        FWLOGERROR("{} dispatch message from [{:#x}: {}] failed, res: {}", name(), from_server_id, get_app()->convert_app_id_to_string(from_server_id), ret);
    }

    return ret;
}

int32_t ss_msg_dispatcher::on_receive_send_data_response(const atapp::app::message_sender_t &source, const atapp::app::message_t &msg, int32_t error_code) {
    if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.type) {
        FWLOGERROR("message type {} invalid", msg.type);
        return hello::err::EN_SYS_PARAM;
    }

    if (0 == source.id || (NULL == msg.data && msg.data_size > 0)) {
        FWLOGERROR("send a message from unknown source");
        return hello::err::EN_SYS_PARAM;
    }

    if (error_code >= 0) {
        FWLOGDEBUG("receive_send_data_response from [{:#x}: {}]", source.id, get_app()->convert_app_id_to_string(source.id));
        return error_code;
    }

    if (NULL == msg.data && msg.data_size > 0) {
        FWLOGERROR("receive_send_data_response from [{:#x}: {}] without data, res: {}", source.id, get_app()->convert_app_id_to_string(source.id), error_code);
        return error_code;
    }

    const void *buffer = msg.data;
    size_t      len    = msg.data_size;

    rpc::context  ctx;
    hello::SSMsg *ss_msg = ctx.create<hello::SSMsg>();
    if (nullptr == ss_msg) {
        FWLOGERROR("{} create message instance failed", name());
        return hello::err::EN_SYS_MALLOC;
    }

    dispatcher_msg_raw_t callback_msg = dispatcher_make_default<dispatcher_msg_raw_t>();

    int32_t ret = unpack_protobuf_msg(*ss_msg, callback_msg, buffer, len);
    if (ret != 0) {
        FWLOGERROR("{} unpack on_receive_send_data_response from [{:#x}: {}] failed, res: {}", name(), source.id,
                   get_app()->convert_app_id_to_string(source.id), ret);
        return ret;
    }

    ss_msg->mutable_head()->set_bus_id(source.id);
    // 转移要恢复的任务ID
    ss_msg->mutable_head()->set_dst_task_id(ss_msg->head().src_task_id());
    ss_msg->mutable_head()->set_src_task_id(0);
    ss_msg->mutable_head()->set_error_code(hello::err::EN_SYS_RPC_SEND_FAILED);

    ret = on_send_message_failed(ctx, callback_msg, error_code, msg.msg_sequence);
    if (ret < 0) {
        FWLOGERROR("{} dispatch on_send_message_failed from [{:#x}: {}] failed, res: {}", name(), source.id, get_app()->convert_app_id_to_string(source.id),
                   ret);
    }

    return ret;
}

uint64_t ss_msg_dispatcher::allocate_sequence() { return ++sequence_allocator_; }
