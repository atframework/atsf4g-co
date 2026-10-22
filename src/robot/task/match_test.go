package atsf4g_go_robot_task

import (
	"fmt"
	"testing"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	public_protocol_extension "github.com/atframework/atsf4g-co/component/public/protocol/extension"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	user_data "github.com/atframework/robot-go/data"
	"google.golang.org/protobuf/proto"
)

// Exercise the generated CS serialization; replace only the network boundary.
type matchingLevelSelectTestUser struct {
	user_data.User
	selectedRequest *public_protocol_pbdesc.CSMatchingLevelSelectReq
	infoRequest     *public_protocol_pbdesc.CSUserGetInfoReq
	infoResponse    *public_protocol_pbdesc.SCUserGetInfoRsp
	selectCode      int32
	infoCode        int32
}

func (u *matchingLevelSelectTestUser) IsLogin() bool         { return true }
func (u *matchingLevelSelectTestUser) AllocSequence() uint64 { return 19 }
func (u *matchingLevelSelectTestUser) Log(string, ...any)    {}
func (u *matchingLevelSelectTestUser) SendReq(_ base.TaskActionImpl, msg proto.Message, _ proto.Message,
	body proto.Message, _ string, _ uint64, _ bool) (int32, *pu.LazyUnmarshalProtobufMessage, error) {
	var response proto.Message
	var code int32
	switch body.(type) {
	case *public_protocol_pbdesc.CSMatchingLevelSelectReq:
		u.selectedRequest = &public_protocol_pbdesc.CSMatchingLevelSelectReq{}
		if err := proto.Unmarshal(msg.(*public_protocol_extension.CSMsg).GetBodyBin(), u.selectedRequest); err != nil {
			return 0, nil, err
		}
		response = &public_protocol_pbdesc.SCMatchingLevelSelectRsp{}
		code = u.selectCode
	case *public_protocol_pbdesc.CSUserGetInfoReq:
		u.infoRequest = &public_protocol_pbdesc.CSUserGetInfoReq{}
		if err := proto.Unmarshal(msg.(*public_protocol_extension.CSMsg).GetBodyBin(), u.infoRequest); err != nil {
			return 0, nil, err
		}
		response = u.infoResponse
		code = u.infoCode
	default:
		return 0, nil, fmt.Errorf("unexpected request %T", body)
	}
	data, err := proto.Marshal(response)
	return code, pu.CreateLazyUnmarshalProtobufMessage(data, response.ProtoReflect().Type(), nil), err
}

func TestMatchingLevelSelectTaskSendsAndVerifiesSelection(t *testing.T) {
	for _, policy := range []public_protocol_pbdesc.EnMatchingFactionFillPolicy{
		public_protocol_pbdesc.EnMatchingFactionFillPolicy_EN_MATCHING_FACTION_FILL_POLICY_DISABLE,
		public_protocol_pbdesc.EnMatchingFactionFillPolicy_EN_MATCHING_FACTION_FILL_POLICY_ENABLE,
	} {
		t.Run(policy.String(), func(t *testing.T) {
			expected := &public_protocol_pbdesc.DLevelSelect{
				LevelIds: []int32{999904, 999905}, Region: "cn", FactionFillPolicy: policy,
			}
			user := &matchingLevelSelectTestUser{
				infoResponse: &public_protocol_pbdesc.SCUserGetInfoRsp{UserLevelSelect: &public_protocol_pbdesc.DLevelSelect{
					LevelIds: []int32{999905, 999904, 999904}, Region: "cn", FactionFillPolicy: policy,
				}},
			}
			action := &user_data.TaskActionUser{User: user}
			if err := MatchingLevelSelectTask(action, []int32{999904, 999905}, "cn", policy); err != nil {
				t.Fatal(err)
			}
			if user.selectedRequest == nil || !proto.Equal(expected, user.selectedRequest.GetData()) {
				t.Fatalf("wire request lost selection: %v", user.selectedRequest)
			}
			if !proto.Equal(user.infoRequest, &public_protocol_pbdesc.CSUserGetInfoReq{NeedUserLevelSelect: true}) {
				t.Fatalf("selection was not explicitly fetched: %v", user.infoRequest)
			}
		})
	}
}

func TestMatchingLevelSelectTaskRejectsMissingOrDifferentSelection(t *testing.T) {
	policy := public_protocol_pbdesc.EnMatchingFactionFillPolicy_EN_MATCHING_FACTION_FILL_POLICY_ENABLE
	for _, test := range []struct {
		name      string
		selection *public_protocol_pbdesc.DLevelSelect
	}{
		{"omitted", nil},
		{"level", &public_protocol_pbdesc.DLevelSelect{LevelIds: []int32{1103}, Region: "cn", FactionFillPolicy: policy}},
		{"region", &public_protocol_pbdesc.DLevelSelect{LevelIds: []int32{999904}, Region: "default", FactionFillPolicy: policy}},
		{"fill_policy", &public_protocol_pbdesc.DLevelSelect{LevelIds: []int32{999904}, Region: "cn",
			FactionFillPolicy: public_protocol_pbdesc.EnMatchingFactionFillPolicy_EN_MATCHING_FACTION_FILL_POLICY_DISABLE}},
	} {
		t.Run(test.name, func(t *testing.T) {
			user := &matchingLevelSelectTestUser{
				infoResponse: &public_protocol_pbdesc.SCUserGetInfoRsp{UserLevelSelect: test.selection},
			}
			if err := MatchingLevelSelectTask(&user_data.TaskActionUser{User: user}, []int32{999904}, "cn", policy); err == nil {
				t.Fatal("missing or different server selection must fail the case")
			}
		})
	}
}

func TestMatchingLevelSelectTaskPropagatesBusinessErrors(t *testing.T) {
	for _, test := range []struct {
		name       string
		selectCode int32
		infoCode   int32
	}{
		{"selection_failed", -1, 0},
		{"fetch_failed", 0, -1},
	} {
		t.Run(test.name, func(t *testing.T) {
			user := &matchingLevelSelectTestUser{
				infoResponse: &public_protocol_pbdesc.SCUserGetInfoRsp{}, selectCode: test.selectCode, infoCode: test.infoCode,
			}
			if err := MatchingLevelSelectTask(&user_data.TaskActionUser{User: user}, []int32{999904}, "cn",
				public_protocol_pbdesc.EnMatchingFactionFillPolicy_EN_MATCHING_FACTION_FILL_POLICY_DISABLE); err == nil {
				t.Fatal("business failure must fail the case")
			}
			if test.selectCode != 0 && user.infoRequest != nil {
				t.Fatal("must not fetch selection after failed update")
			}
		})
	}
}
