package atsf4g_go_robot_cmd

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
	utils "github.com/atframework/robot-go/utils"
)

// ========================= 注册指令 =========================
func init() {
	utils.RegisterCommandDefaultTimeout(robot_cmd.MutableCommandRoot(), []string{"user", "login"}, LoginCmd, "<openid>", "登录协议", nil)
	robot_cmd.RegisterUserCommand([]string{"user", "logout"}, LogoutCmd, "", "登出协议", nil)
	robot_cmd.RegisterUserCommand([]string{"user", "getInfo"}, GetInfoCmd, "", "拉取用户信息", nil)
	robot_cmd.RegisterUserCommand([]string{"user", "ping"}, PingCmd, "", "Ping包", nil)
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
	u := user_data.CreateUser(openId, user_data.CreateDefaultUserLogHandler(openId), true, false)
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
