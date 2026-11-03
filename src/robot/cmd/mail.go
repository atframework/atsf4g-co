package atsf4g_go_robot_cmd

import (
	"fmt"
	"strconv"
	"strings"

	task "github.com/atframework/atsf4g-co-robot/task"
	base "github.com/atframework/robot-go/base"
	robot_cmd "github.com/atframework/robot-go/cmd"
	user_data "github.com/atframework/robot-go/data"
)

func init() {
	robot_cmd.RegisterUserCommand([]string{"mail", "get_all"}, MailGetAllCmd,
		"<major_type> [page_no] [page_count]", "分页拉取邮件(page_no 默认 1, page_count 默认 30)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"mail", "read"}, MailReadCmd,
		"<mail_id> [mail_id...] [need_remove]", "标记指定邮件已读(need_remove=true/false, 默认 true)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"mail", "read_all"}, MailReadAllCmd,
		"<major_type> [minor_type] [need_remove]", "按类型标记全部邮件已读(minor_type 默认 0, need_remove 默认 true)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"mail", "receive_attachment"}, MailReceiveAttachmentCmd,
		"<mail_id> [mail_id...] [need_remove]", "领取指定邮件附件(need_remove=true/false, 默认 true)", nil, cmdDefaultTimeout)
	robot_cmd.RegisterUserCommand([]string{"mail", "receive_attachment_all"}, MailReceiveAttachmentAllCmd,
		"<major_type> [minor_type] [need_remove]", "按类型领取全部邮件附件(minor_type 默认 0, need_remove 默认 true)", nil, cmdDefaultTimeout)
}

func parseMailInt32Arg(value, name string, minimum int64) (int32, error) {
	parsed, err := strconv.ParseInt(value, 10, 32)
	if err != nil || parsed < minimum {
		return 0, fmt.Errorf("invalid %s %q", name, value)
	}
	return int32(parsed), nil
}

func parseMailIdsAndNeedRemove(args []string) ([]int64, bool, error) {
	needRemove := true
	idArgs := args
	if len(args) > 0 {
		switch strings.ToLower(args[len(args)-1]) {
		case "true", "false", "yes", "no", "on", "off":
			value, err := parseBoolArg(args, len(args)-1)
			if err != nil {
				return nil, false, err
			}
			needRemove = value
			idArgs = args[:len(args)-1]
		}
	}
	if len(idArgs) == 0 {
		return nil, false, fmt.Errorf("need at least one mail_id")
	}

	mailIds := make([]int64, 0, len(idArgs))
	for _, arg := range idArgs {
		mailId, err := strconv.ParseInt(arg, 10, 64)
		if err != nil || mailId <= 0 {
			return nil, false, fmt.Errorf("invalid mail_id %q", arg)
		}
		mailIds = append(mailIds, mailId)
	}
	return mailIds, needRemove, nil
}

func MailGetAllCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 || len(cmd) > 3 {
		return fmt.Errorf("usage: mail get_all <major_type> [page_no] [page_count]")
	}
	majorType, err := parseMailInt32Arg(cmd[0], "major_type", 1)
	if err != nil {
		return err
	}
	pageNo := int32(1)
	pageCount := int32(30)
	if len(cmd) > 1 {
		pageNo, err = parseMailInt32Arg(cmd[1], "page_no", 1)
		if err != nil {
			return err
		}
	}
	if len(cmd) > 2 {
		pageCount, err = parseMailInt32Arg(cmd[2], "page_count", 0)
		if err != nil {
			return err
		}
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.MailGetAllTask(t, majorType, pageNo, pageCount)
	}, "Mail Get All Task"))
}

func MailReadCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	mailIds, needRemove, err := parseMailIdsAndNeedRemove(cmd)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.MailReadTask(t, mailIds, needRemove)
	}, "Mail Read Task"))
}

func MailReadAllCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 || len(cmd) > 3 {
		return fmt.Errorf("usage: mail read_all <major_type> [minor_type] [need_remove]")
	}
	majorType, err := parseMailInt32Arg(cmd[0], "major_type", 1)
	if err != nil {
		return err
	}
	minorType := int32(0)
	if len(cmd) > 1 {
		minorType, err = parseMailInt32Arg(cmd[1], "minor_type", 0)
		if err != nil {
			return err
		}
	}
	needRemove := true
	if len(cmd) > 2 {
		needRemove, err = parseBoolArg(cmd, 2)
		if err != nil {
			return err
		}
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.MailReadAllTask(t, majorType, minorType, needRemove)
	}, "Mail Read All Task"))
}

func MailReceiveAttachmentCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	mailIds, needRemove, err := parseMailIdsAndNeedRemove(cmd)
	if err != nil {
		return err
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.MailReceiveAttachmentTask(t, mailIds, needRemove)
	}, "Mail Receive Attachment Task"))
}

func MailReceiveAttachmentAllCmd(action base.TaskActionImpl, user user_data.User, cmd []string) error {
	if len(cmd) < 1 || len(cmd) > 3 {
		return fmt.Errorf("usage: mail receive_attachment_all <major_type> [minor_type] [need_remove]")
	}
	majorType, err := parseMailInt32Arg(cmd[0], "major_type", 1)
	if err != nil {
		return err
	}
	minorType := int32(0)
	if len(cmd) > 1 {
		minorType, err = parseMailInt32Arg(cmd[1], "minor_type", 0)
		if err != nil {
			return err
		}
	}
	needRemove := true
	if len(cmd) > 2 {
		needRemove, err = parseBoolArg(cmd, 2)
		if err != nil {
			return err
		}
	}
	return action.AwaitTask(user.RunTaskDefaultTimeout(func(t *user_data.TaskActionUser) error {
		return task.MailReceiveAttachmentAllTask(t, majorType, minorType, needRemove)
	}, "Mail Receive Attachment All Task"))
}
