package atsf4g_go_robot_rpc

import (
	"fmt"
	"runtime"
	"strings"
	"time"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	auth_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/authsvr"
	lobbysvr_rpc_handle "github.com/atframework/atsf4g-co-robot/rpc_handle/lobbysvr"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"

	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/mem"
	"google.golang.org/protobuf/reflect/protoreflect"
)

func LoginAuthRpc(action base.TaskActionImpl, user user_data.User) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCLoginAuthRsp], error) {
	accessTokenCode, ok := user.GetExtralData("AccessTokenCode").(string)
	if ok && accessTokenCode != "" {
		return 0, nil, fmt.Errorf("already login auth")
	}

	csBody := &public_protocol_pbdesc.CSLoginAuthReq{
		OpenId: user.GetOpenId(),
		Account: &public_protocol_pbdesc.DAccountData{
			AccountType: uint32(public_protocol_pbdesc.EnAccountTypeID_EN_ATI_ACCOUNT_INTERNAL),
			Access:      user.GetAccessToken(),
			ChannelId:   uint32(public_protocol_pbdesc.EnPlatformChannelID_EN_PCI_NONE),
		},
		SystemId:        public_protocol_pbdesc.EnSystemID_EN_OS_WINDOWS,
		PackageVersion:  "0.0.0.1",
		ResourceVersion: "0.0.0.1",
	}
	return auth_rpc_handle.SendLoginAuth(action, user, csBody, false)
}

func LoginRpc(action base.TaskActionImpl, user user_data.User) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCLoginRsp], error) {
	accessTokenCode, ok := user.GetExtralData("AccessTokenCode").(string)
	if !ok || accessTokenCode == "" {
		return 0, nil, fmt.Errorf("need login auth")
	}

	if user.GetLogined() {
		return 0, nil, fmt.Errorf("already login")
	}

	vmem, _ := mem.VirtualMemory()
	cpuInfo, _ := cpu.Info()

	csBody := &public_protocol_pbdesc.CSLoginReq{
		AccessTokenCode: accessTokenCode,
		OpenId:          user.GetOpenId(),
		UserId:          user.GetUserId(),
		Account: &public_protocol_pbdesc.DAccountData{
			AccountType: uint32(public_protocol_pbdesc.EnAccountTypeID_EN_ATI_ACCOUNT_INTERNAL),
			Access:      user.GetAccessToken(),
			ChannelId:   uint32(public_protocol_pbdesc.EnPlatformChannelID_EN_PCI_NONE),
		},
		ClientInfo: &public_protocol_pbdesc.DClientDeviceInfo{
			SystemId:        public_protocol_pbdesc.EnSystemID_EN_OS_WINDOWS,
			ClientVersion:   "0.0.0.1",
			OperatingSystem: runtime.GOOS,
			ProcessorInfo: func() string {
				if len(cpuInfo) > 0 {
					return cpuInfo[0].ModelName
				}
				return "unknown"
			}(),
			ProcessorCount: int32(len(cpuInfo)),
			ProcessorFrequency: func() int32 {
				if len(cpuInfo) > 0 {
					return int32(cpuInfo[0].Mhz)
				}
				return 0
			}(),
			SystemMemorySize: int32(vmem.Total / (1024 * 1024)),
		},
	}

	return lobbysvr_rpc_handle.SendLogin(action, user, csBody, false)
}

func PingRpc(action base.TaskActionImpl, user user_data.User) error {
	csBody := &public_protocol_pbdesc.CSPingReq{}

	errCode, _, err := lobbysvr_rpc_handle.SendPing(action, user, csBody, true)
	if err != nil {
		return err
	}
	if errCode < 0 {
		return fmt.Errorf("ping failed, errCode: %d", errCode)
	}
	user.SetLastPingTime(time.Now())
	return nil
}

func GetInfoRpc(action base.TaskActionImpl, user user_data.User, args []string) (int32, *pu.LazyUnmarshalProtobufMessageSpecific[*public_protocol_pbdesc.SCUserGetInfoRsp], error) {
	csBody := &public_protocol_pbdesc.CSUserGetInfoReq{}

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

	return lobbysvr_rpc_handle.SendUserGetInfo(action, user, csBody, true)
}
