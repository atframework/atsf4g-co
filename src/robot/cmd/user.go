package atsf4g_go_robot_cmd

import (
	"fmt"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
	utils "github.com/atframework/robot-go/utils"
)

var cmdDefaultTimeout = time.Second * 12

// ========================= 注册指令 =========================
func init() {
	utils.RegisterCommand(robot_cmd.MutableCommandRoot(), []string{"user", "login"}, LoginCmd, "<openid>", "登录协议", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "logout"}, LogoutCmd, "", "登出协议", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "getInfo"}, GetInfoCmd, "", "拉取用户信息", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "ping"}, PingCmd, "", "Ping包", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"user", "gm"}, GMCmd, "", "GM指令", nil, cmdDefaultTimeout)
}

func LogoutCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	err := action.AwaitTask(user.RunTaskDefaultTimeout(task.LogoutTask, "Logout Task"))
	if err != nil {
		return err
	}
	return nil
}

func LoginCmd(action base.TaskActionImpl, cmd []string) string {
	if len(cmd) < 1 {
		return "Need OpenId"
	}

	openId := cmd[0]
	// u := user_data.CreateUser(openId, user_data.CreateDefaultUserLogHandler(openId), true, false)
	u := user_data.CreateUser(openId, action.Log, true, false)
	if u == nil {
		return "Create User Failed"
	}
	err := action.AwaitTask(u.RunTaskDefaultTimeout(task.LoginTask, "Login Task"))
	if err != nil {
		u.Logout()
		return err.Error()
	}
	robot_cmd.SetCurrentUser(u)
	return ""
}

func GetInfoCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	// 发送登录请求
	err := action.AwaitTask(user.RunTaskDefaultTimeout(func(task *user_data.TaskActionUser) error {
		errCode, _, rpcErr := protocol.GetInfoRpc(task, user, cmd)
		if rpcErr != nil {
			return rpcErr
		}
		if errCode < 0 {
			return fmt.Errorf("get info failed, errCode: %d", errCode)
		}
		task.User.SetHasGetInfo(true)
		return nil
	}, "GetInfo Task"))
	if err != nil {
		return err
	}
	return nil
}

func PingCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	// 发送登录请求
	err := action.AwaitTask(user.RunTaskDefaultTimeout(func(task *user_data.TaskActionUser) error {
		return protocol.PingRpc(task, user)
	}, "Ping Task"))
	if err != nil {
		return err
	}
	return nil
}

func GMCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	// 发送登录请求
	err := action.AwaitTask(user.RunTaskDefaultTimeout(func(task *user_data.TaskActionUser) error {
		errCode, rsp, rpcErr := protocol.GMRpc(task, user, cmd)
		if rpcErr != nil {
			return rpcErr
		}
		if errCode < 0 {
			return fmt.Errorf("gm command failed, errCode: %d", errCode)
		}
		rspBody, err := rsp.GetMessage()
		if err != nil {
			return fmt.Errorf("gm command get Message failed, err: %v", err)
		}
		task.Log("Gm Result Code: %d, Message: %s", rspBody.GetResultCode(), rspBody.GetResultMessage())
		return nil
	}, "GM Task"))
	if err != nil {
		return err
	}
	return nil
}
