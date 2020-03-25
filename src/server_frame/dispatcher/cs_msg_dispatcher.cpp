//
// Created by owt50 on 2016/9/27.
//

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <WinSock2.h>
#endif

#include <log/log_wrapper.h>

#include <atframe/atapp.h>
#include <config/atframe_service_types.h>
#include <libatbus_protocol.h>
#include <libatgw_server_protocol.h>
#include <proto_base.h>

#include <protocol/pbdesc/com.protocol.pb.h>
#include <protocol/pbdesc/svr.const.err.pb.h>


#include <logic/action/task_action_player_logout.h>
#include <logic/session_manager.h>

#include "cs_msg_dispatcher.h"
#include "task_manager.h"


cs_msg_dispatcher::cs_msg_dispatcher() {}
cs_msg_dispatcher::~cs_msg_dispatcher() {}

int32_t cs_msg_dispatcher::init() { return 0; }

uint64_t cs_msg_dispatcher::pick_msg_task_id(msg_raw_t &raw_msg) {
    // cs msg not allow resume task
    return 0;
}

cs_msg_dispatcher::msg_type_t cs_msg_dispatcher::pick_msg_type_id(msg_raw_t &raw_msg) {
    hello::CSMsg *real_msg = get_protobuf_msg<hello::CSMsg>(raw_msg);
    if (NULL == real_msg) {
        return 0;
    }

    return static_cast<msg_type_t>(real_msg->body().body_oneof_case());
}

int32_t cs_msg_dispatcher::dispatch(const atbus::protocol::msg &msg, const void *buffer, size_t len) {
    if (::atframe::component::service_type::EN_ATST_GATEWAY != msg.head().type()) {
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

    uint64_t from_server_id = msg.data_transform_req().from();

    if (NULL == buffer || 0 == from_server_id) {
        WLOGERROR("receive a message from unknown source");
        return hello::err::EN_SYS_PARAM;
    }

    ::atframe::gw::ss_msg req_msg;
    if (false == req_msg.ParseFromArray(reinterpret_cast<const void*>(buffer), static_cast<int>(len))) {
        WLOGERROR("receive msg of %llu bytes from x0%llx parse failed: %s",
            static_cast<unsigned long long>(len), static_cast<unsigned long long>(msg.head().src_bus_id()), 
            req_msg.InitializationErrorString().c_str()
        );
        return 0;
    }

    int ret = hello::err::EN_SUCCESS;
    switch (msg.head().type()) {
    case ::atframe::gw::ss_msg_body::kPost: {
        const ::atframe::gw::ss_body_post& post = req_msg.body().post();

        hello::CSMsg   cs_msg;
        session::key_t session_key;
        session_key.bus_id     = from_server_id;
        session_key.session_id = req_msg.head().session_id();

        std::shared_ptr<session> sess = session_manager::me()->find(session_key);
        if (!sess) {
            WLOGERROR("session [0x%llx, 0x%llx] not found, try to kickoff", static_cast<unsigned long long>(session_key.bus_id),
                      static_cast<unsigned long long>(session_key.session_id));
            ret = hello::err::EN_SYS_NOTFOUND;

            send_kickoff(session_key.bus_id, session_key.session_id, hello::EN_CRT_SESSION_NOT_FOUND);
            break;
        }

        start_data_t start_data;
        start_data.private_data = NULL;
        ret                     = unpack_protobuf_msg(cs_msg, start_data.message, 
                                    reinterpret_cast<const void*>(post.content().data()), 
                                    post.content().size());
        if (ret != 0) {
            WLOGERROR("%s unpack received message from 0x%llx, session id:0x%llx failed, res: %d", name(), static_cast<unsigned long long>(session_key.bus_id),
                      static_cast<unsigned long long>(session_key.session_id), ret);
            return ret;
        }

        cs_msg.mutable_head()->set_session_bus_id(session_key.bus_id);
        cs_msg.mutable_head()->set_session_id(session_key.session_id);

        if (task_manager::me()->is_busy()) {
            cs_msg.mutable_head()->set_error_code(hello::EN_ERR_SYSTEM_BUSY);
            sess->send_msg_to_client(cs_msg);
            WLOGINFO("server busy and send msg back to session [0x%llx, 0x%llx]", static_cast<unsigned long long>(session_key.bus_id),
                      static_cast<unsigned long long>(session_key.session_id));
            break;
        }

        ret = on_recv_msg(start_data.message, start_data.private_data, cs_msg.head().sequence());
        if (ret < 0) {
            WLOGERROR("%s on receive message callback from to 0x%llx, session id:0x%llx failed, res: %d", name(),
                      static_cast<unsigned long long>(session_key.bus_id), static_cast<unsigned long long>(session_key.session_id), ret);
        }
        break;
    }
    case ::atframe::gw::ss_msg_body::kAddSession: {
        const ::atframe::gw::ss_body_session& sess_data = req_msg.body().add_session();

        session::key_t session_key;
        session_key.bus_id     = from_server_id;
        session_key.session_id = req_msg.head().session_id();

        WLOGINFO("create new session [0x%llx, 0x%llx], address: %s:%d", static_cast<unsigned long long>(session_key.bus_id),
                 static_cast<unsigned long long>(session_key.session_id), sess_data.client_ip().c_str(), sess_data.client_port());

        session_manager::sess_ptr_t sess = session_manager::me()->create(session_key);
        if (!sess) {
            WLOGERROR("malloc failed");
            ret = hello::err::EN_SYS_MALLOC;
            send_kickoff(session_key.bus_id, session_key.session_id, ::atframe::gateway::close_reason_t::EN_CRT_SERVER_BUSY);
            break;
        }

        break;
    }
    case ::atframe::gw::ss_msg_body::kRemoveSession: {
        session::key_t session_key;
        session_key.bus_id     = from_server_id;
        session_key.session_id = req_msg.head().session_id();

        WLOGINFO("remove session [0x%llx, 0x%llx]", static_cast<unsigned long long>(session_key.bus_id),
                 static_cast<unsigned long long>(session_key.session_id));

        // logout task
        task_manager::id_t                      logout_task_id = 0;
        task_action_player_logout::ctor_param_t task_param;
        task_param.atgateway_session_id = session_key.session_id;
        task_param.atgateway_bus_id     = session_key.bus_id;

        ret = task_manager::me()->create_task<task_action_player_logout>(logout_task_id, COPP_MACRO_STD_MOVE(task_param));
        if (0 == ret) {
            start_data_t start_data;
            start_data.private_data     = NULL;
            start_data.message.msg_type = 0;
            start_data.message.msg_addr = NULL;

            ret = task_manager::me()->start_task(logout_task_id, start_data);
            if (0 != ret) {
                WLOGERROR("run logout task failed, res: %d", ret);
                session_manager::me()->remove(session_key);
            }
        } else {
            WLOGERROR("create logout task failed, res: %d", ret);
            session_manager::me()->remove(session_key);
        }
        break;
    }
    default:
        WLOGERROR("receive a unsupport atgateway message of invalid cmd:%d", static_cast<int>(req_msg.body().cmd_case()));
        break;
    }

    return ret;
}

int32_t cs_msg_dispatcher::send_kickoff(uint64_t bus_id, uint64_t session_id, int32_t reason) {
    atapp::app *owner = get_app();
    if (NULL == owner) {
        WLOGERROR("not in a atapp");
        return hello::err::EN_SYS_INIT;
    }

    ::atframe::gw::ss_msg msg;
    msg.mutable_head()->set_session_id(session_id);
    msg.mutable_head()->set_error_code(reason);


    msg.mutable_body()->mutable_kickoff_session();

    std::string packed_buffer;
    if(false == msg.SerializeToString(&packed_buffer)) {
        WLOGERROR("try to kickoff %llx with serialize failed: %s", 
        static_cast<unsigned long long>(session_id), msg.InitializationErrorString().c_str());
        return 0;
    }

    return owner->get_bus_node()->send_data(bus_id, 0, packed_buffer.data(), packed_buffer.size());
}

int32_t cs_msg_dispatcher::send_data(uint64_t bus_id, uint64_t session_id, const void *buffer, size_t len) {
    atapp::app *owner = get_app();
    if (NULL == owner) {
        WLOGERROR("not in a atapp");
        return hello::err::EN_SYS_INIT;
    }

    if (NULL == buffer || 0 == len) {
        return 0;
    }

    ::atframe::gw::ss_msg msg;
    msg.mutable_head()->set_session_id(session_id);

    ::atframe::gw::ss_body_post* post = msg.mutable_body()->mutable_post();

    if (NULL == post) {
        if (0 == session_id) {
            WLOGERROR("broadcast %llu bytes data to atgateway 0x%llx failed when malloc post", 
                static_cast<unsigned long long>(len), static_cast<unsigned long long>(bus_id));
        } else {
            WLOGERROR("send %llu bytes data to session [0x%llx, 0x%llx] failed when malloc post", 
                static_cast<unsigned long long>(len), static_cast<unsigned long long>(bus_id),
                static_cast<unsigned long long>(session_id));
        }
        return hello::err::EN_SYS_MALLOC;
    }

    post->set_content(buffer, len);

    std::string packed_buffer;
    if(false == msg.SerializeToString(&packed_buffer)) {
        WLOGERROR("try to send %llu bytes data to 0x%llx with serialize failed: %s",
            static_cast<unsigned long long>(len),
            static_cast<unsigned long long>(session_id), msg.InitializationErrorString().c_str());
        return 0;
    }

    int ret = owner->get_bus_node()->send_data(bus_id, ::atframe::component::service_type::EN_ATST_GATEWAY, packed_buffer.data(), packed_buffer.size());
    if (ret < 0) {
        if (0 == session_id) {
            WLOGERROR("broadcast data to atgateway 0x%llx failed, res: %d", static_cast<unsigned long long>(bus_id), ret);
        } else {
            WLOGERROR("send data to session [0x%llx, 0x%llx] failed, res: %d", static_cast<unsigned long long>(bus_id),
                      static_cast<unsigned long long>(session_id), ret);
        }
    }

    return ret;
}

int32_t cs_msg_dispatcher::broadcast_data(uint64_t bus_id, const void *buffer, size_t len) { return send_data(bus_id, 0, buffer, len); }

int32_t cs_msg_dispatcher::broadcast_data(uint64_t bus_id, const std::vector<uint64_t> &session_ids, const void *buffer, size_t len) {
    atapp::app *owner = get_app();
    if (NULL == owner) {
        WLOGERROR("not in a atapp");
        return hello::err::EN_SYS_INIT;
    }

    if (NULL == buffer || 0 == len) {
        return 0;
    }

    ::atframe::gw::ss_msg msg;
    msg.mutable_head()->set_session_id(0);

    ::atframe::gw::ss_body_post* post = msg.mutable_body()->mutable_post();

    if (NULL == post) {
        WLOGERROR("broadcast %llu bytes data to atgateway 0x%llx failed when malloc post", 
            static_cast<unsigned long long>(len), static_cast<unsigned long long>(bus_id));
        return hello::err::EN_SYS_MALLOC;
    }

    post->set_content(buffer, len);

    std::string packed_buffer;
    if(false == msg.SerializeToString(&packed_buffer)) {
        WLOGERROR("try to broadcast %llu bytes data with serialize failed: %s", 
            static_cast<unsigned long long>(len), msg.InitializationErrorString().c_str());
        return 0;
    }

    int ret = owner->get_bus_node()->send_data(bus_id, ::atframe::component::service_type::EN_ATST_GATEWAY, packed_buffer.data(), packed_buffer.size());
    if (ret < 0) {
        WLOGERROR("broadcast data to atgateway 0x%llx failed, res: %d", static_cast<unsigned long long>(bus_id), ret);
    }

    return ret;
}
