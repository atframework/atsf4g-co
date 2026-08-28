package atsf4g_go_robot_task

import (
	"fmt"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	user_data "github.com/atframework/robot-go/data"
)

func PingTask(u user_data.User) error {
	u.RunTaskDefaultTimeout(func(action *user_data.TaskActionUser) error {
		return protocol.PingRpc(action, action.User)
	}, "PingTask")
	return nil
}

func LoginTask(task *user_data.TaskActionUser) (err error) {
	errCode, rspHolder, rpcErr := protocol.LoginAuthRpc(task, task.User)
	if rpcErr != nil {
		err = rpcErr
		return
	}
	if errCode < 0 {
		err = fmt.Errorf("login auth failed, errCode: %d", errCode)
		return
	}

	user := task.User
	rsp, err := rspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get login auth response message: %v", err)
	}

	if rsp.GetAccessTokenCode() != "" {
		user.SetExtralData("AccessTokenCode", rsp.GetAccessTokenCode())
	}
	if rsp.GetUserId() != 0 {
		user.SetUserId(rsp.GetUserId())
	}

	errCode, loginRspHolder, rpcErr := protocol.LoginRpc(task, user)
	if rpcErr != nil {
		task.Log("user login failed, error: %v, open_id: %s, user_id: %d", err, user.GetOpenId(), user.GetUserId())
		err = rpcErr
		return
	}
	if errCode < 0 {
		err = fmt.Errorf("login req failed, errCode: %d", errCode)
		return
	}

	loginRsp, err := loginRspHolder.GetMessage()
	if err != nil {
		return fmt.Errorf("failed to get login response message: %v", err)
	}

	user.SetZoneId(uint32(loginRsp.GetZoneId()))
	user.Login()

	if loginRsp.GetHeartbeatInterval() > 0 {
		user.SetHeartbeatInterval(time.Duration(loginRsp.GetHeartbeatInterval()) * time.Second)
	}

	// 创建Ping流程
	user.InitHeartbeatFunc(PingTask)

	// 登入成功后的自动聊天流程：注册频道推送处理器，拉取所有可用频道并逐个拉取一次快照以触发订阅推送。
	// 聊天流程失败不回滚已成功的登入，仅记录日志。
	protocol.RegisterChatChannelSyncPushHandler(user)
	if subscribeErr := ChatAutoSubscribeTask(task); subscribeErr != nil {
		task.Log("[chat] auto subscribe failed: %v", subscribeErr)
	}
	return
}

func LogoutTask(task *user_data.TaskActionUser) (err error) {
	task.User.Logout()
	return nil
}
