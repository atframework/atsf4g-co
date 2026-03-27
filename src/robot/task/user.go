package atsf4g_go_robot_task

import (
	"fmt"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	lobbysvr_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

func PingTask(u user_data.User) error {
	u.RunTaskDefaultTimeout(func(action *user_data.TaskActionUser) error {
		return protocol.PingRpc(action)
	}, "PingTask")
	return nil
}

func LoginTask(task *user_data.TaskActionUser) (err error) {
	errCode, rsp, rpcErr := protocol.LoginAuthRpc(task)
	if rpcErr != nil {
		err = rpcErr
		return
	}
	if errCode < 0 {
		err = fmt.Errorf("login auth failed, errCode: %d", errCode)
		return
	}

	user := task.User

	if rsp.GetLoginCode() != "" {
		user.SetLoginCode(rsp.GetLoginCode())
	}
	if rsp.GetUserId() != 0 {
		user.SetUserId(rsp.GetUserId())
	}

	var loginRsp *lobbysvr_protocol_pbdesc.SCLoginRsp
	errCode, loginRsp, rpcErr = protocol.LoginRpc(task)
	if rpcErr != nil {
		task.Log("user login failed, error: %v, open_id: %s, user_id: %d", err, user.GetOpenId(), user.GetUserId())
		err = rpcErr
		return
	}
	if errCode < 0 {
		err = fmt.Errorf("login req failed, errCode: %d", errCode)
		return
	}

	user.SetZoneId(uint32(loginRsp.GetZoneId()))
	user.SetLogined(true)

	if loginRsp.GetHeartbeatInterval() > 0 {
		user.SetHeartbeatInterval(time.Duration(loginRsp.GetHeartbeatInterval()) * time.Second)
	}

	// 创建Ping流程
	user.InitHeartbeatFunc(PingTask)
	return
}

func LogoutTask(task *user_data.TaskActionUser) (err error) {
	task.User.Logout()
	task.Log("user %s logout", task.User.GetOpenId())
	return nil
}
