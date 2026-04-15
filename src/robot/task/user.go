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

	if rsp.GetLoginCode() != "" {
		user.SetExtralData("LoginCode", rsp.GetLoginCode())
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
	return
}

func LogoutTask(task *user_data.TaskActionUser) (err error) {
	task.User.Logout()
	return nil
}
