package atsf4g_go_robot_rpc

import (
	pu "github.com/atframework/atframe-utils-go/proto_utility"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"
)

func MailGetAllRpc(action base.TaskActionImpl, user user_data.User, majorType, pageNo, pageCount int32) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMailGetAllRsp], error) {
	csBody := &public_protocol_pbdesc.CSMailGetAllReq{
		MajorType: majorType,
		PageNo:    pageNo,
		PageCount: pageCount,
	}
	return lobbysvr_rpc_handle.SendMailGetAll(action, user, csBody, true)
}

func MailReadRpc(action base.TaskActionImpl, user user_data.User, mailIds []int64, needRemove bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMailReadRsp], error) {
	csBody := &public_protocol_pbdesc.CSMailReadReq{
		MailIds:    mailIds,
		NeedRemove: needRemove,
	}
	return lobbysvr_rpc_handle.SendMailRead(action, user, csBody, true)
}

func MailReadAllRpc(action base.TaskActionImpl, user user_data.User, majorType, minorType int32, needRemove bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMailReadAllRsp], error) {
	csBody := &public_protocol_pbdesc.CSMailReadAllReq{
		MajorType:  majorType,
		MinorType:  minorType,
		NeedRemove: needRemove,
	}
	return lobbysvr_rpc_handle.SendMailReadAll(action, user, csBody, true)
}

func MailReceiveAttachmentRpc(action base.TaskActionImpl, user user_data.User, mailIds []int64, needRemove bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMailReceiveAttachmentsRsp], error) {
	csBody := &public_protocol_pbdesc.CSMailReceiveAttachmentsReq{
		MailIds:    mailIds,
		NeedRemove: needRemove,
	}
	return lobbysvr_rpc_handle.SendMailReceiveAttachment(action, user, csBody, true)
}

func MailReceiveAttachmentAllRpc(action base.TaskActionImpl, user user_data.User, majorType, minorType int32,
	needRemove bool) (
	int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCMailReceiveAttachmentsAllRsp], error) {
	csBody := &public_protocol_pbdesc.CSMailReceiveAttachmentsAllReq{
		MajorType:  majorType,
		MinorType:  minorType,
		NeedRemove: needRemove,
	}
	return lobbysvr_rpc_handle.SendMailReceiveAttachmentAll(action, user, csBody, true)
}
