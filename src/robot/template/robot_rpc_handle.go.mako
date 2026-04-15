## -*- coding: utf-8 -*-
<%!
import time
import os
import re
%><%
module_name = service.get_extension_field("service_options", lambda x: x.module_name, service.get_name_lower_rule())
%>// Copyright ${time.strftime("%Y", time.localtime()) } atframework
// @brief Created by ${generator} for ${service.get_full_name()}, please don't edit it

package atsf4g_go_robot_rpc

import (
	"fmt"

	"time"
	lu "github.com/atframework/atframe-utils-go/lang_utility"
	pu "github.com/atframework/atframe-utils-go/proto_utility"
	base "github.com/atframework/robot-go/base"
	data "github.com/atframework/robot-go/data"
	public_protocol_extension "github.com/atframework/atsf4g-co/component/public/protocol/extension"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	"google.golang.org/protobuf/proto"
)

func MakeMessageHead(user data.User, rpcName string, typeName string) *public_protocol_extension.CSMsgHead {
	return &public_protocol_extension.CSMsgHead{
		Timestamp:      time.Now().Unix(),
		ClientSequence: user.AllocSequence(),
		RpcType: &public_protocol_extension.CSMsgHead_RpcRequest{
			RpcRequest: &public_protocol_extension.RpcRequestMeta{
				RpcName: rpcName,
				TypeUrl: typeName,
			},
		},
	}
}

% for rpc in rpcs.values():
<%
rpc_name = rpc.get_identify_name(rpc.get_name(), PbConvertRule.CONVERT_NAME_CAMEL_CAMEL)
%>
% if rpc.get_request_descriptor().full_name == "google.protobuf.Empty":
func RegisterMessageHandler${rpc_name}(user data.User, f func(*data.TaskActionUser, *public_protocol_pbdesc.${rpc.get_response().get_name()}, int32) error) {
	user.RegisterMessageHandler("${rpc.descriptor.full_name}", func(action *data.TaskActionUser, msg *pu.LazyUnmarshalProtobufMessage, errCode int32) error {
		body, err := msg.GetMessage()
		if err != nil {
			action.Log("GetMessage to ${rpc.get_response().get_name()} failed")
			return fmt.Errorf("GetMessage to ${rpc.get_response().get_name()} failed")
		}
		bodyTyped, ok := body.(*public_protocol_pbdesc.${rpc.get_response().get_name()})
		if !ok {
			action.Log("type assertion to ${rpc.get_response().get_name()} failed")
			return fmt.Errorf("type assertion to ${rpc.get_response().get_name()} failed")
		}
		return f(action, bodyTyped, errCode)
	})
}
% else:
func Send${rpc_name}(task base.TaskActionImpl, user data.User, reqBody *public_protocol_pbdesc.${rpc.get_request().get_name()}, needLogin bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.${rpc.get_response().get_name()}], error) {
	if lu.IsNil(user) || reqBody == nil {
		return 0, nil, fmt.Errorf("user or request is nil")
	}
	if needLogin {
		if !user.IsLogin() {
			return 0, nil, fmt.Errorf("user not login")
		}
	}
	csMsg := &public_protocol_extension.CSMsg{
		Head: MakeMessageHead(user, "${rpc.descriptor.full_name}", "${rpc.get_request_descriptor().full_name}"),
	}
	csMsg.BodyBin, _ = proto.Marshal(reqBody)
	code, bodyRaw, err := user.SendReq(task, csMsg, csMsg.Head, reqBody,
		csMsg.Head.GetRpcRequest().GetRpcName(), csMsg.Head.GetClientSequence(), true)
	if err != nil {
		return code, nil, fmt.Errorf("SendReq failed: %v", err)
	}
	if code != 0 {
		user.Log("${rpc.get_name()} failed, code: %d, err: %v", code, err)
	}
	return code, pu.CreateLazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.${rpc.get_response().get_name()}](bodyRaw), err
}
% endif
% endfor