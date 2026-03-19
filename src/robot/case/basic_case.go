package atsf4g_go_robot_case

import (
	"fmt"
	"time"

	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	task "github.com/atframework/atsf4g-co-robot/task"
	protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	robot_case "github.com/atframework/robot-go/case"
	cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_case.RegisterCase("connect", ConnectCase, time.Second*5)
	robot_case.RegisterCase("login", LoginCase, time.Second*5)
	robot_case.RegisterCase("logout", LogoutCase, time.Second*5)
	robot_case.RegisterCase("await_close", AwaitCloseCase, time.Second*5)
	robot_case.RegisterCase("delay_second", DelayCase, 0)
	robot_case.RegisterCase("run_cmd", RunCmdCase, time.Second*5)
}

func ConnectCase(action *robot_case.TaskActionCase, openId string, args []string) error {
	u := user_data.CreateUser(openId, nil, false)
	if u == nil {
		return fmt.Errorf("Failed to create user")
	}

	return nil
}

func DelayCase(action *robot_case.TaskActionCase, openId string, args []string) error {
	if len(args) < 1 {
		return fmt.Errorf("invalid args")
	}
	duration, err := time.ParseDuration(args[0] + "s")
	if err != nil {
		return err
	}
	time.Sleep(duration)
	return nil
}

func LoginCase(action *robot_case.TaskActionCase, openId string, args []string) error {
	// 创建角色
	u := user_data.CreateUser(openId, nil, false)
	if u == nil {
		return fmt.Errorf("Failed to create user")
	}

	lobbysvr_rpc_handle.RegisterMessageHandlerPlayerDirtyChgSync(u,
		func(action *user_data.TaskActionUser, msg *protocol_pbdesc.SCPlayerDirtyChgSync, errCode int32) error {
			// 处理脏数据变更通知
			return nil
		})

	err := action.AwaitTask(u.RunTaskDefaultTimeout(task.LoginTask, "Login Task"))
	if err != nil {
		return err
	}

	err = action.AwaitTask(u.RunTaskDefaultTimeout(func(tau *user_data.TaskActionUser) error {
		user_data.UserContainerAddUser(tau.User)
		return nil
	}, "AddUser Task"))
	if err != nil {
		return err
	}

	return nil
}

func LogoutCase(action *robot_case.TaskActionCase, openId string, args []string) error {
	u := user_data.UserContainerGetUser(openId)
	if u == nil {
		return fmt.Errorf("User Not Found")
	}

	err := action.AwaitTask(u.RunTaskDefaultTimeout(task.LogoutTask, "Logout Task"))
	if err != nil {
		return err
	}

	u.AwaitReceiveHandlerClose()
	return nil
}

func AwaitCloseCase(action *robot_case.TaskActionCase, openId string, args []string) error {
	u := user_data.UserContainerGetUser(openId)
	if u == nil {
		return nil
	}

	u.AwaitReceiveHandlerClose()
	return nil
}

func RunCmdCase(action *robot_case.TaskActionCase, openId string, args []string) error {
	u := user_data.UserContainerGetUser(openId)
	if u == nil {
		return fmt.Errorf("User Not Found")
	}

	cmdArgs, fn := cmd.GetUserCommandFunc(args)
	if fn == nil {
		return fmt.Errorf("Command Not Found")
	}

	result := fn(action, u, cmdArgs)
	if result != "" {
		return fmt.Errorf(result)
	}

	return nil
}
