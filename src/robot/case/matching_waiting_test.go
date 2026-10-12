package atsf4g_go_robot_case

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"testing"
	"time"

	pu "github.com/atframework/atframe-utils-go/proto_utility"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	base "github.com/atframework/robot-go/base"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
	"google.golang.org/protobuf/proto"
)

type matchingWaitGuardTestUser struct {
	user_data.User
	owner         base.TaskActionImpl
	caseAction    base.TaskActionImpl
	heartbeatRan  bool
	onCaseRelease func()
}

func (u *matchingWaitGuardTestUser) TakeActionGuard(action base.TaskActionImpl) error {
	if u.owner != nil {
		return fmt.Errorf("user guard already held")
	}
	u.owner = action
	return nil
}

func (u *matchingWaitGuardTestUser) ReleaseActionGuard(action base.TaskActionImpl) error {
	if u.owner != action {
		return fmt.Errorf("user guard held by another task")
	}
	u.owner = nil
	if action == u.caseAction && u.onCaseRelease != nil {
		u.onCaseRelease()
	}
	return nil
}

func (u *matchingWaitGuardTestUser) IsTakenActionGuard(action base.TaskActionImpl) bool {
	return u.owner == action
}

func TestMatchingWaitUntilAllowsHeartbeatWhileWaiting(t *testing.T) {
	user := &matchingWaitGuardTestUser{}
	holder := &user_data.UserHolder{}
	holder.InitUser(user)
	action := &robot_case.TaskActionCase{UserHolder: holder}
	user.caseAction = action
	// TaskActionCase.HookRun enters each case with this guard already held.
	if err := user.TakeActionGuard(action); err != nil {
		t.Fatal(err)
	}
	user.onCaseRelease = func() {
		// Deterministic handoff: queued heartbeat work must be allowed before the wait completes.
		heartbeat := &user_data.TaskActionUser{User: user}
		if err := user.TakeActionGuard(heartbeat); err != nil {
			t.Fatalf("heartbeat cannot acquire user during deadline wait: %v", err)
		}
		user.heartbeatRan = true
		if err := user.ReleaseActionGuard(heartbeat); err != nil {
			t.Fatal(err)
		}
	}
	deadline := time.Now().Add(time.Second).Unix()
	if err := MatchingWaitUntilCase(action, holder, []string{strconv.FormatInt(deadline, 10)}); err != nil {
		t.Fatal(err)
	}
	if !user.heartbeatRan {
		t.Fatal("absolute deadline wait held the user guard, blocking heartbeat work")
	}
	if !user.IsTakenActionGuard(action) {
		t.Fatal("case must reacquire the guard before returning to TaskActionCase.HookRun")
	}
}

// Keep the generated CS request/response path; substitute only its network transport.
type orbitInfoTestUser struct {
	user_data.User
	response *public_protocol_pbdesc.SCUserGetInfoRsp
	request  *public_protocol_pbdesc.CSUserGetInfoReq
}

func (u *orbitInfoTestUser) IsLogin() bool         { return true }
func (u *orbitInfoTestUser) AllocSequence() uint64 { return 17 }
func (u *orbitInfoTestUser) SendReq(_ base.TaskActionImpl, _ proto.Message, _ proto.Message,
	body proto.Message, _ string, _ uint64, _ bool) (int32, *pu.LazyUnmarshalProtobufMessage, error) {
	u.request = body.(*public_protocol_pbdesc.CSUserGetInfoReq)
	data, err := proto.Marshal(u.response)
	return 0, pu.CreateLazyUnmarshalProtobufMessage(data, u.response.ProtoReflect().Type(), nil), err
}

func TestWaitingOrbitReadRejectsOmittedFieldButAcceptsExplicitEmptyRoom(t *testing.T) {
	user := &orbitInfoTestUser{response: &public_protocol_pbdesc.SCUserGetInfoRsp{}}
	action := &user_data.TaskActionUser{User: user}
	if _, err := readOrbitRoom(action); err == nil {
		t.Fatal("omitted requested field must not prove DS exit")
	}
	if user.request == nil || !user.request.GetNeedUserOrbitRoom() {
		t.Fatal("Orbit state was not explicitly requested")
	}
	user.response.UserOrbitRoom = &public_protocol_pbdesc.DOrbitRoomUserData{}
	room, err := readOrbitRoom(action)
	if err != nil || room == nil || room.GetRoomKey().GetClientId() != "" {
		t.Fatalf("explicit empty room should be observable: room=%v err=%v", room, err)
	}
}

func TestWaitingAdmissionRequiresMatchingAndOrbitSuccess(t *testing.T) {
	view := &public_protocol_pbdesc.DMatchingClientView{
		UnitId: 901, FactionId: 3, SelectedLevelId: 999904,
		Status: public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED,
	}
	room := &public_protocol_pbdesc.DOrbitRoomUserData{
		RoomKey: &public_protocol_pbdesc.DOrbitRoomKey{ClientId: "waiting-room-17"},
		Init:    true, Token: "test-token", ClientAddress: "127.0.0.1:7777",
	}
	for _, test := range []struct {
		name   string
		change func(*public_protocol_pbdesc.DMatchingClientView, *public_protocol_pbdesc.DOrbitRoomUserData)
		done   bool
		failed bool
	}{
		{"admitted", func(*public_protocol_pbdesc.DMatchingClientView, *public_protocol_pbdesc.DOrbitRoomUserData) {}, true, false},
		{"confirming_is_not_admitted", func(v *public_protocol_pbdesc.DMatchingClientView, _ *public_protocol_pbdesc.DOrbitRoomUserData) {
			v.Status = public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING
		}, false, false},
		{"initializing_is_not_admitted", func(v *public_protocol_pbdesc.DMatchingClientView, _ *public_protocol_pbdesc.DOrbitRoomUserData) {
			v.Status = public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE
		}, false, false},
		{"failed_does_not_wait", func(v *public_protocol_pbdesc.DMatchingClientView, _ *public_protocol_pbdesc.DOrbitRoomUserData) {
			v.Status = public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED
		}, false, true},
		{"wrong_level", func(v *public_protocol_pbdesc.DMatchingClientView, _ *public_protocol_pbdesc.DOrbitRoomUserData) {
			v.SelectedLevelId++
		}, false, true},
		{"nonzero_result", func(v *public_protocol_pbdesc.DMatchingClientView, _ *public_protocol_pbdesc.DOrbitRoomUserData) {
			v.Result = -1
		}, false, true},
		{"missing_faction", func(v *public_protocol_pbdesc.DMatchingClientView, _ *public_protocol_pbdesc.DOrbitRoomUserData) {
			v.FactionId = 0
		}, false, true},
		{"orbit_not_initialized", func(_ *public_protocol_pbdesc.DMatchingClientView, r *public_protocol_pbdesc.DOrbitRoomUserData) {
			r.Init = false
		}, false, false},
		{"missing_room", func(_ *public_protocol_pbdesc.DMatchingClientView, r *public_protocol_pbdesc.DOrbitRoomUserData) {
			r.RoomKey = nil
		}, false, false},
		{"missing_token", func(_ *public_protocol_pbdesc.DMatchingClientView, r *public_protocol_pbdesc.DOrbitRoomUserData) {
			r.Token = ""
		}, false, false},
		{"missing_address", func(_ *public_protocol_pbdesc.DMatchingClientView, r *public_protocol_pbdesc.DOrbitRoomUserData) {
			r.ClientAddress = ""
		}, false, false},
	} {
		t.Run(test.name, func(t *testing.T) {
			actualView := proto.Clone(view).(*public_protocol_pbdesc.DMatchingClientView)
			actualRoom := proto.Clone(room).(*public_protocol_pbdesc.DOrbitRoomUserData)
			test.change(actualView, actualRoom)
			admission, done, err := completedMatchingAdmission(actualView, actualRoom, 999904)
			if done != test.done || (err != nil) != test.failed {
				t.Fatalf("got done=%t err=%v; want done=%t failed=%t", done, err, test.done, test.failed)
			}
			if done && admission != (matchingAdmission{UnitID: 901, RoomID: "waiting-room-17", FactionID: 3, LevelID: 999904}) {
				t.Fatalf("lost observed identifiers: %+v", admission)
			}
		})
	}
}

func TestWaitingAdmissionRelationsRejectWrongRoomOrFaction(t *testing.T) {
	reference := matchingAdmission{UnitID: 101, RoomID: "existing-ds", FactionID: 3, LevelID: 999904}
	for _, test := range []struct {
		name    string
		actual  matchingAdmission
		room    string
		faction string
		failed  bool
	}{
		{"fill_existing_faction", matchingAdmission{102, "existing-ds", 3, 999904}, "same", "same", false},
		{"wrong_faction", matchingAdmission{102, "existing-ds", 4, 999904}, "same", "same", true},
		{"wrong_ds", matchingAdmission{102, "new-ds", 3, 999904}, "same", "same", true},
		{"new_faction", matchingAdmission{102, "existing-ds", 4, 999904}, "same", "different", false},
		{"forbidden_fill", matchingAdmission{102, "existing-ds", 3, 999904}, "same", "different", true},
		{"closed_room_new_ds", matchingAdmission{102, "new-ds", 3, 999904}, "different", "any", false},
		{"closed_room_reused", matchingAdmission{102, "existing-ds", 4, 999904}, "different", "any", true},
		{"same_unit_not_backfill", reference, "same", "same", true},
	} {
		t.Run(test.name, func(t *testing.T) {
			if err := compareMatchingAdmissions(test.actual, reference, test.room, test.faction); (err != nil) != test.failed {
				t.Fatalf("got err=%v, want failed=%t", err, test.failed)
			}
		})
	}
}

func TestWaitingCaseConfigsAreExecutableAndCheckEveryUser(t *testing.T) {
	registered := make(map[string]bool)
	for _, name := range robot_case.AutoCompleteCaseName("") {
		registered[name] = true
	}
	for _, filename := range []string{
		"matching_waiting_fill.conf", "matching_waiting_no_fill.conf", "matching_waiting_disabled.conf",
	} {
		t.Run(filename, func(t *testing.T) {
			content, err := os.ReadFile(filepath.Join("..", "case_config", filename))
			if err != nil {
				t.Fatal(err)
			}
			lines, err := robot_case.ParseCaseFileContent(string(content))
			if err != nil {
				t.Fatal(err)
			}
			if len(lines) == 0 || lines[0].Stress.CaseName != "login" {
				t.Fatal("must initialize all scenario users")
			}
			login := lines[0].Stress
			admitted := make(map[int64]bool)
			relationChecked := false
			for _, line := range lines {
				p := line.Stress
				if line.IsControl || line.BackgroundRunning || !registered[p.CaseName] || !p.ErrorBreak ||
					p.OpenIDPrefix != login.OpenIDPrefix || p.OpenIDStart < login.OpenIDStart ||
					p.OpenIDEnd > login.OpenIDEnd || p.OpenIDStart >= p.OpenIDEnd || p.RunTime != 1 {
					t.Fatalf("invalid action or account range: %+v", p)
				}
				if p.CaseName == "matching_wait_admitted" {
					for id := p.OpenIDStart; id < p.OpenIDEnd; id++ {
						admitted[id] = true
					}
				}
				if p.CaseName == "matching_assert_admission" {
					relationChecked = true
				}
			}
			for id := login.OpenIDStart; id < login.OpenIDEnd; id++ {
				if !admitted[id] {
					t.Errorf("user %d never verifies matching and Orbit success", id)
				}
			}
			if !relationChecked {
				t.Fatal("scenario never verifies room/faction relations")
			}
		})
	}
}
