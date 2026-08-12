package atsf4g_go_robot_case

import (
	"fmt"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	task "github.com/atframework/atsf4g-co-robot/task"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_case.RegisterCase("login", LoginCase, time.Second*30)
	robot_case.RegisterCase("logout", LogoutCase, time.Second*10)
}

func LoginCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	user := holder.GetUser()
	if user == nil {
		user = user_data.CreateUser(holder.OpenId, action.Log, true, false)
		if user == nil {
			return fmt.Errorf("failed to create user: %s", holder.OpenId)
		}
		holder.InitUser(user)
	}
	if user.IsLogin() {
		protocol.RegisterMatchingLogSyncHandler(user)
		return nil
	}
	err := action.AwaitTask(user.RunTaskDefaultTimeout(task.LoginTask, "Login Task"))
	if err != nil {
		user.Logout()
		return err
	}
	protocol.RegisterMatchingLogSyncHandler(user)
	return nil
}

func LogoutCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	user := holder.GetUser()
	if user == nil {
		return nil
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(task.LogoutTask, "Logout Task"))
}
