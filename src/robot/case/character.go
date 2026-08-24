package atsf4g_go_robot_case

import (
	"fmt"
	"time"

	task "github.com/atframework/atsf4g-co-robot/task"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_case.RegisterCase("character_create", CharacterCreateCase, time.Second*30)
}

func CharacterCreateCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	user := holder.GetUser()
	if user == nil {
		return nil
	}
	templateId := int32(1002)
	name := fmt.Sprintf("Robot_%d", user.GetUserId())
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		return task.CharacterCreateCmd(taskAction, user, templateId, name)
	}, "CreateCharacter Task"))
}
