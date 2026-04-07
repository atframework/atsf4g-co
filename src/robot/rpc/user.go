package atsf4g_go_robot_rpc

import (
	"fmt"
	"runtime"
	"strings"
	"time"

	auth_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/authsvr"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"

	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/mem"
	"google.golang.org/protobuf/reflect/protoreflect"
)

func LoginAuthRpc(action *user_data.TaskActionUser) (int32, *public_protocol_pbdesc.SCLoginAuthRsp, error) {
	user := action.User
	loginCode := user.GetExtralData("LoginCode").(string)
	if loginCode != "" {
		return 0, nil, fmt.Errorf("already login auth")
	}

	csBody := &public_protocol_pbdesc.CSLoginAuthReq{
		OpenId: user.GetOpenId(),
		Account: &public_protocol_pbdesc.DAccountData{
			AccountType: uint32(public_protocol_pbdesc.EnAccountTypeID_EN_ATI_ACCOUNT_INNER),
			Access:      user.GetAccessToken(),
			ChannelId:   uint32(public_protocol_pbdesc.EnPlatformChannelID_EN_PCI_NONE),
		},
		SystemId:        public_protocol_pbdesc.EnSystemID_EN_OS_WINDOWS,
		PackageVersion:  "0.0.0.1",
		ResourceVersion: "0.0.0.1",
	}
	return auth_rpc_handle.SendLoginAuth(action, csBody, false)
}

func LoginRpc(action *user_data.TaskActionUser) (int32, *public_protocol_pbdesc.SCLoginRsp, error) {
	user := action.User
	loginCode := user.GetExtralData("LoginCode").(string)
	if loginCode == "" {
		return 0, nil, fmt.Errorf("need login auth")
	}

	if user.GetLogined() {
		return 0, nil, fmt.Errorf("already login")
	}

	vmem, _ := mem.VirtualMemory()
	cpuInfo, _ := cpu.Info()

	csBody := &public_protocol_pbdesc.CSLoginReq{
		LoginCode: loginCode,
		OpenId:    user.GetOpenId(),
		UserId:    user.GetUserId(),
		Account: &public_protocol_pbdesc.DAccountData{
			AccountType: uint32(public_protocol_pbdesc.EnAccountTypeID_EN_ATI_ACCOUNT_INNER),
			Access:      user.GetAccessToken(),
			ChannelId:   uint32(public_protocol_pbdesc.EnPlatformChannelID_EN_PCI_NONE),
		},
		ClientInfo: &public_protocol_pbdesc.DClientDeviceInfo{
			SystemId:       uint32(public_protocol_pbdesc.EnSystemID_EN_OS_WINDOWS),
			ClientVersion:  "0.0.0.1",
			SystemSoftware: runtime.GOOS,
			SystemHardware: runtime.GOARCH,
			CpuInfo: func() string {
				if len(cpuInfo) > 0 {
					return fmt.Sprintf("%s - %gMHz", strings.TrimSpace(cpuInfo[0].ModelName), cpuInfo[0].Mhz)
				}
				return "unknown"
			}(),
			Memory: uint32(vmem.Total / (1024 * 1024)),
		},
	}

	return lobbysvr_rpc_handle.SendLogin(action, csBody, false)
}

func PingRpc(action *user_data.TaskActionUser) error {
	csBody := &public_protocol_pbdesc.CSPingReq{}

	errCode, _, err := lobbysvr_rpc_handle.SendPing(action, csBody, true)
	if err != nil {
		return err
	}
	if errCode < 0 {
		return fmt.Errorf("ping failed, errCode: %d", errCode)
	}
	action.User.SetLastPingTime(time.Now())
	return nil
}

func GetInfoRpc(action *user_data.TaskActionUser, args []string) (int32, *public_protocol_pbdesc.SCPlayerGetInfoRsp, error) {
	csBody := &public_protocol_pbdesc.CSPlayerGetInfoReq{}

	ref := csBody.ProtoReflect()
	fields := ref.Descriptor().Fields()

	needFields := make(map[string]struct{})
	for _, arg := range args {
		needFields[arg] = struct{}{}
	}

	for i := 0; i < fields.Len(); i++ {
		field := fields.Get(i)
		fieldName := string(field.Name())

		// 检查字段名前缀和类型
		if strings.HasPrefix(fieldName, "need_") && field.Kind() == protoreflect.BoolKind {
			if len(needFields) > 0 {
				_, ok := needFields[fieldName]
				if !ok {
					_, ok = needFields[strings.TrimPrefix(fieldName, "need_")]
					if !ok {
						continue
					}
				}
			}

			ref.Set(field, protoreflect.ValueOfBool(true))
		}
	}

	return lobbysvr_rpc_handle.SendPlayerGetInfo(action, csBody, true)
}
