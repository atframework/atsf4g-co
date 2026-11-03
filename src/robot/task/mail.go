package atsf4g_go_robot_task

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

func MailGetAllTask(task *user_data.TaskActionUser, majorType, pageNo, pageCount int32) error {
	errCode, rspHolder, rpcErr := protocol.MailGetAllRpc(task, task.User, majorType, pageNo, pageCount)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("mail get all failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return fmt.Errorf("mail get all response missing")
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get mail get all response message: %w", err)
	}

	task.Log("[mail] get_all major_type=%d page_no=%d page_count=%d mail_count=%d returned=%d",
		rsp.GetMajorType(), rsp.GetPageNo(), rsp.GetPageCount(), rsp.GetMailCount(), len(rsp.GetMails()))
	for _, mail := range rsp.GetMails() {
		task.Log("[mail] mail_id=%d major_type=%d minor_type=%d status=%d attachments=%d title=%s",
			mail.GetMailId(), mail.GetMajorType(), mail.GetMinorType(), mail.GetStatus(), len(mail.GetAttachments()), mail.GetTitle())
	}
	return nil
}

func MailReadTask(task *user_data.TaskActionUser, mailIds []int64, needRemove bool) error {
	errCode, rspHolder, rpcErr := protocol.MailReadRpc(task, task.User, mailIds, needRemove)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("mail read failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return fmt.Errorf("mail read response missing")
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get mail read response message: %w", err)
	}
	logMailOperationResults(task, "read", rsp.GetMails())
	return nil
}

func MailReadAllTask(task *user_data.TaskActionUser, majorType, minorType int32, needRemove bool) error {
	errCode, rspHolder, rpcErr := protocol.MailReadAllRpc(task, task.User, majorType, minorType, needRemove)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("mail read all failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return fmt.Errorf("mail read all response missing")
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get mail read all response message: %w", err)
	}
	logMailOperationResults(task, "read_all", rsp.GetMails())
	return nil
}

func MailReceiveAttachmentTask(task *user_data.TaskActionUser, mailIds []int64, needRemove bool) error {
	errCode, rspHolder, rpcErr := protocol.MailReceiveAttachmentRpc(task, task.User, mailIds, needRemove)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("mail receive attachment failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return fmt.Errorf("mail receive attachment response missing")
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get mail receive attachment response message: %w", err)
	}
	logMailOperationResults(task, "receive_attachment", rsp.GetMails())
	return nil
}

func MailReceiveAttachmentAllTask(task *user_data.TaskActionUser, majorType, minorType int32, needRemove bool) error {
	errCode, rspHolder, rpcErr := protocol.MailReceiveAttachmentAllRpc(task, task.User, majorType, minorType, needRemove)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("mail receive attachment all failed, errCode: %d", errCode)
	}
	if rspHolder == nil {
		return fmt.Errorf("mail receive attachment all response missing")
	}
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get mail receive attachment all response message: %w", err)
	}
	logMailOperationResults(task, "receive_attachment_all", rsp.GetMails())
	return nil
}

func logMailOperationResults(task *user_data.TaskActionUser, operation string,
	mails []*public_protocol_pbdesc.DMailOperationResult) {
	task.Log("[mail] %s returned=%d", operation, len(mails))
	for _, result := range mails {
		record := result.GetRecord()
		task.Log("[mail] %s mail_id=%d result=%d status=%d", operation, record.GetMailId(), result.GetResult(), record.GetStatus())
	}
}
