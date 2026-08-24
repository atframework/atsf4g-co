package atsf4g_go_robot_task

import (
	"fmt"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	user_data "github.com/atframework/robot-go/data"
)

func CharacterCreateCmd(task *user_data.TaskActionUser, user user_data.User, templateId int32, name string) error {
	errCode, _, rpcErr := protocol.CreateCharacterRpc(task, user, int32(templateId), name)
	if rpcErr != nil {
		return rpcErr
	}
	if errCode < 0 {
		return fmt.Errorf("create character failed, errCode: %d", errCode)
	}
	return nil
}
