//
// Created by owt50 on 2016/9/27.
//

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
#include <protocol/pbdesc/svr.protocol.pb.h>
#include <protocol/pbdesc/svr.const.pb.h>
#include <config/compiler/protobuf_suffix.h>

#include <utility/protobuf_mini_dumper.h>

ss_msg_dispatcher::ss_msg_dispatcher() : sequence_allocator_(0) {}
ss_msg_dispatcher::~ss_msg_dispatcher() {}

int32_t ss_msg_dispatcher::init() { 
    sequence_allocator_ = static_cast<uint64_t>((util::time::time_utility::get_sys_now() - hello::EN_SL_TIMESTAMP_FOR_ID_ALLOCATOR_OFFSET) << 23) + 
        static_cast<uint64_t>(util::time::time_utility::get_now_usec() << 3);
    return 0; 
}

uint64_t ss_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
    hello::SSMsg *real_msg = get_protobuf_msg<hello::SSMsg>(raw_msg);
    if (NULL == real_msg) {
        return 0;
    }

    return real_msg->head().dst_task_id();
}

ss_msg_dispatcher::msg_type_t ss_msg_dispatcher::pick_msg_type_id(msg_raw_t &raw_msg) {
    hello::SSMsg *real_msg = get_protobuf_msg<hello::SSMsg>(raw_msg);
    if (NULL == real_msg) {
        return 0;
    }

    // 路由对象系统支持，允许从SSRouterHead中读取
    if (!real_msg->body_bin().empty() && (!real_msg->has_body() || hello::SSMsgBody::BODY_ONEOF_NOT_SET == real_msg->body().body_oneof_case()) &&
        real_msg->head().has_router()) {
        return static_cast<msg_type_t>(real_msg->head().router().message_type());
    }

    return static_cast<msg_type_t>(real_msg->body().body_oneof_case());
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

const hello::DDispatcherOptions* ss_msg_dispatcher::get_options_by_message_type(msg_type_t msg_type) {
    const google::protobuf::FieldDescriptor* fd = hello::SSMsgBody::descriptor()->FindFieldByNumber(static_cast<int>(msg_type));
    if (NULL == fd) {
        return NULL;
    }

    if(fd->options().HasExtension(hello::dispatcher_options)) {
        return &fd->options().GetExtension(hello::dispatcher_options);
    }

    return NULL;
}

int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, hello::SSMsg &ss_msg) {
    if (0 == ss_msg.head().sequence()) {
        ss_msg.mutable_head()->set_sequence(allocate_sequence());
    }

    size_t msg_buf_len = ss_msg.ByteSizeLong();
    size_t tls_buf_len = atframe::gateway::proto_base::get_tls_length(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM);
    if (msg_buf_len > tls_buf_len) {
        WLOGERROR("send to proc [0x%llx] failed: require %llu, only have %llu", static_cast<unsigned long long>(bus_id),
                  static_cast<unsigned long long>(msg_buf_len), static_cast<unsigned long long>(tls_buf_len));
        return hello::err::EN_SYS_BUFF_EXTEND;
    }

    ::google::protobuf::uint8 *buf_start =
        reinterpret_cast< ::google::protobuf::uint8 *>(atframe::gateway::proto_base::get_tls_buffer(atframe::gateway::proto_base::tls_buffer_t::EN_TBT_CUSTOM));
    ss_msg.SerializeWithCachedSizesToArray(buf_start);
    WLOGDEBUG("send msg to proc [0x%llx] %llu bytes\n%s", static_cast<unsigned long long>(bus_id), static_cast<unsigned long long>(msg_buf_len),
              protobuf_mini_dumper_get_readable(ss_msg));

    return send_to_proc(bus_id, buf_start, msg_buf_len);
}

int32_t ss_msg_dispatcher::send_to_proc(uint64_t bus_id, const void *msg_buf, size_t msg_len) {
    atapp::app *owner = get_app();
    if (NULL == owner) {
        WLOGERROR("module not attached to a atapp");
        return hello::err::EN_SYS_INIT;
    }

    if (!owner->get_bus_node()) {
        WLOGERROR("owner app has no valid bus node");
        return hello::err::EN_SYS_INIT;
    }

    int res = owner->get_bus_node()->send_data(bus_id, atframe::component::message_type::EN_ATST_SS_MSG, msg_buf, msg_len, false);

    if (res < 0) {
        WLOGERROR("send msg to proc [0x%llx] %llu bytes failed, res: %d", static_cast<unsigned long long>(bus_id), static_cast<unsigned long long>(msg_len),
                  res);
    } else {
        WLOGDEBUG("send msg to proc [0x%llx] %llu bytes success", static_cast<unsigned long long>(bus_id), static_cast<unsigned long long>(msg_len));
    }

    return res;
}

int32_t ss_msg_dispatcher::dispatch(const atbus::protocol::msg &msg, const void *buffer, size_t len) {
    if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.head().type()) {
        WLOGERROR("message type %d invalid", msg.head().type());
        return hello::err::EN_SYS_PARAM;
    }

    if (atbus::protocol::msg::kDataTransformReq != msg.msg_body_case()) {
        WLOGERROR("receive msg with %llu bytes from x0%llx: %s",
            static_cast<unsigned long long>(len), static_cast<unsigned long long>(msg.head().src_bus_id()), 
            msg.DebugString().c_str()
        );
        return 0;
    }

    if (0 == msg.head().src_bus_id()) {
        WLOGERROR("receive a message from unknown source");
        return hello::err::EN_SYS_PARAM;
    }

    uint64_t from_server_id = msg.data_transform_req().from();

    hello::SSMsg ss_msg;

    start_data_t callback_data;
    callback_data.private_data = NULL;

    int32_t ret = unpack_protobuf_msg(ss_msg, callback_data.message, buffer, len);
    if (ret != 0) {
        WLOGERROR("%s unpack received message from 0x%llx failed, res: %d", name(), static_cast<unsigned long long>(from_server_id), ret);
        return ret;
    }
    ss_msg.mutable_head()->set_bus_id(from_server_id);

    ret = on_recv_msg(callback_data.message, callback_data.private_data, ss_msg.head().sequence());
    if (ret < 0) {
        WLOGERROR("%s dispatch message from 0x%llx failed, res: %d", name(), static_cast<unsigned long long>(from_server_id), ret);
    }

    return ret;
}

int32_t ss_msg_dispatcher::on_receive_send_data_response(const atbus::protocol::msg &msg) {
    if (::atframe::component::message_type::EN_ATST_SS_MSG != msg.head().type()) {
        WLOGERROR("message type %d invalid", msg.head().type());
        return hello::err::EN_SYS_PARAM;
    }

    const atbus::protocol::forward_data* fwd_data = NULL;
    if (atbus::protocol::msg::kDataTransformReq == msg.msg_body_case()) {
        fwd_data = &msg.data_transform_req();
    } else if (atbus::protocol::msg::kDataTransformRsp == msg.msg_body_case()) {
        fwd_data = &msg.data_transform_rsp();
    }

    if (NULL == fwd_data) {
        WLOGERROR("send a message from unknown source");
        return hello::err::EN_SYS_PARAM;
    }

    if (msg.head().ret() >= 0) {
        WLOGDEBUG("receive_send_data_response from 0x%llx", static_cast<unsigned long long>(fwd_data->from()));
        return msg.head().ret();
    }

    const void *buffer = reinterpret_cast<const void*>(fwd_data->content().data());
    size_t      len    = fwd_data->content().size();

    hello::SSMsg ss_msg;
    start_data_t callback_data;
    callback_data.private_data = NULL;

    int32_t ret = unpack_protobuf_msg(ss_msg, callback_data.message, buffer, len);
    if (ret != 0) {
        WLOGERROR("%s unpack on_receive_send_data_response to 0x%llx failed, res: %d", name(), static_cast<unsigned long long>(fwd_data->to()), ret);
        return ret;
    }

    if (atbus::protocol::msg::kDataTransformRsp == msg.msg_body_case() && msg.head().ret() < 0) {
        ss_msg.mutable_head()->set_bus_id(fwd_data->from());
        // 转移要恢复的任务ID
        ss_msg.mutable_head()->set_dst_task_id(ss_msg.head().src_task_id());
        ss_msg.mutable_head()->set_src_task_id(0);
        ss_msg.mutable_head()->set_error_code(hello::err::EN_SYS_RPC_SEND_FAILED);

        ret = on_send_msg_failed(callback_data.message, msg.head().ret(), msg.head().sequence());
        if (ret < 0) {
            WLOGERROR("%s dispatch on_send_msg_failed to 0x%llx failed, res: %d", name(), static_cast<unsigned long long>(fwd_data->to()), ret);
        }
    }

    return ret;
}

uint64_t ss_msg_dispatcher::allocate_sequence() {
    return ++ sequence_allocator_;
}
