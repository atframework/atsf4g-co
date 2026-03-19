// client.go
package main

import (
	"fmt"
	"os"

	component_config "github.com/atframework/atsf4g-co-robot/config"
	public_protocol_extension "github.com/atframework/atsf4g-co/component/public/protocol/extension"
	"google.golang.org/protobuf/proto"

	_ "github.com/atframework/atsf4g-co-robot/case"
	_ "github.com/atframework/atsf4g-co-robot/cmd"
	robot "github.com/atframework/robot-go"
)

func UnpackMessage(msg proto.Message) (rpcName string, typeName string, errorCode int32,
	msgHead proto.Message, bodyBin []byte, sequence uint64, err error) {
	csMsg, ok := msg.(*public_protocol_extension.CSMsg)
	if !ok {
		err = fmt.Errorf("message type invalid: %T", msg)
		return
	}
	switch csMsg.Head.GetRpcType().(type) {
	case *public_protocol_extension.CSMsgHead_RpcResponse:
		rpcName = csMsg.Head.GetRpcResponse().GetRpcName()
		typeName = csMsg.Head.GetRpcResponse().GetTypeUrl()
	case *public_protocol_extension.CSMsgHead_RpcStream:
		rpcName = csMsg.Head.GetRpcStream().GetRpcName()
		typeName = csMsg.Head.GetRpcStream().GetTypeUrl()
	default:
		err = fmt.Errorf("unsupport RpcType: %T", csMsg.Head.GetRpcType())
		return
	}
	errorCode = csMsg.Head.GetErrorCode()
	msgHead = csMsg.Head
	bodyBin = csMsg.BodyBin
	sequence = csMsg.Head.GetClientSequence()
	return
}

func main() {
	flagSet := robot.NewRobotFlagSet()
	flagSet.String("resource", "", "resource directory")
	if err := robot.LoadFlagSetFromYAML(flagSet, "", os.Args[1:]); err != nil {
		fmt.Println(err)
		return
	}

	if flagSet.Lookup("resource").Value.String() != "" {
		resourceDir := flagSet.Lookup("resource").Value.String()
		if resourceDir != "" {
			component_config.GetConfigManager().Init(resourceDir, nil, nil)
		}
	}

	robot.StartRobot(flagSet, UnpackMessage, func() proto.Message {
		return &public_protocol_extension.CSMsg{}
	})
}
