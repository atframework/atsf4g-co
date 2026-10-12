package atsf4g_go_robot_case

import (
	"fmt"
	"strconv"
	"time"

	protocol "github.com/atframework/atsf4g-co-robot/rpc"
	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	robot_case "github.com/atframework/robot-go/case"
	user_data "github.com/atframework/robot-go/data"
)

const matchingAdmissionKey = "MatchingTestAdmission"

// Store identifiers only: DS tokens are not needed in test output or comparisons.
type matchingAdmission struct {
	UnitID    uint64
	RoomID    string
	FactionID int32
	LevelID   int32
}

func init() {
	robot_case.RegisterCase("matching_wait_admitted", MatchingWaitAdmittedCase, 5*time.Minute)
	robot_case.RegisterCase("matching_assert_admission", MatchingAssertAdmissionCase, 30*time.Second)
	robot_case.RegisterCase("matching_assert_unchanged", MatchingAssertUnchangedCase, 30*time.Second)
	robot_case.RegisterCase("matching_assert_searching", MatchingAssertSearchingCase, 30*time.Second)
	robot_case.RegisterCase("matching_assert_new_unit", MatchingAssertNewUnitCase, 30*time.Second)
	robot_case.RegisterCase("matching_wait_orbit_left", MatchingWaitOrbitLeftCase, 5*time.Minute)
	robot_case.RegisterCase("matching_wait_until", MatchingWaitUntilCase, 15*time.Minute)
}

func matchingCaseTimeout(value string) (time.Duration, error) {
	seconds, err := strconv.Atoi(value)
	if err != nil || seconds <= 0 || seconds > 240 {
		return 0, fmt.Errorf("timeout must be 1..240 seconds, got %q", value)
	}
	return time.Duration(seconds) * time.Second, nil
}

// Read current CS responses instead of allowing a previous attempt's cached FINISHED to pass.
func readMatchingAdmission(action *robot_case.TaskActionCase, user user_data.User) (
	*public_protocol_pbdesc.DMatchingClientView, *public_protocol_pbdesc.DOrbitRoomUserData, error) {
	var view *public_protocol_pbdesc.DMatchingClientView
	var room *public_protocol_pbdesc.DOrbitRoomUserData
	err := action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
		code, response, err := protocol.MatchingCheckRpc(taskAction, user)
		if err != nil {
			return err
		}
		if code != 0 || response == nil {
			return fmt.Errorf("matching check failed: code=%d response_present=%t", code, response != nil)
		}
		message, err := response.GetMessage()
		if err != nil {
			return err
		}
		view = message.GetView()
		if view == nil || view.GetUnitId() == 0 {
			return fmt.Errorf("matching check has no current Unit")
		}
		protocol.SaveMatchingView(user, view)
		room, err = readOrbitRoom(taskAction)
		return err
	}, "Read Matching Admission"))
	return view, room, err
}

func readOrbitRoom(action *user_data.TaskActionUser) (*public_protocol_pbdesc.DOrbitRoomUserData, error) {
	code, response, err := protocol.GetInfoRpc(action, action.User, []string{"need_user_orbit_room"})
	if err != nil {
		return nil, err
	}
	if code != 0 || response == nil {
		return nil, fmt.Errorf("get orbit room failed: code=%d response_present=%t", code, response != nil)
	}
	message, err := response.GetMessage()
	if err != nil {
		return nil, err
	}
	if message.GetUserOrbitRoom() == nil {
		return nil, fmt.Errorf("get info response omitted requested user_orbit_room")
	}
	return message.GetUserOrbitRoom(), nil
}

func completedMatchingAdmission(view *public_protocol_pbdesc.DMatchingClientView,
	room *public_protocol_pbdesc.DOrbitRoomUserData, levelID int32) (matchingAdmission, bool, error) {
	result := matchingAdmission{}
	if view == nil || view.GetUnitId() == 0 {
		return result, false, fmt.Errorf("missing current Unit")
	}
	finished, err := matchingWaitSuccessStatus(view.GetStatus())
	if err != nil || !finished {
		return result, false, err
	}
	if view.GetResult() != 0 || view.GetSelectedLevelId() != levelID || view.GetFactionId() <= 0 {
		return result, false, fmt.Errorf("invalid completed matching: result=%d level=%d (want %d) faction=%d",
			view.GetResult(), view.GetSelectedLevelId(), levelID, view.GetFactionId())
	}
	// FINISHED can precede the Orbit subscription's first snapshot. Wait for its observable result.
	if room.GetRoomKey().GetClientId() == "" || !room.GetInit() || room.GetToken() == "" || room.GetClientAddress() == "" {
		return result, false, nil
	}
	return matchingAdmission{view.GetUnitId(), room.GetRoomKey().GetClientId(), view.GetFactionId(), levelID}, true, nil
}

// MatchingWaitAdmittedCase requires Match success and Orbit initialization data, not an actual DS network connection.
func MatchingWaitAdmittedCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 2 {
		return fmt.Errorf("need timeout_seconds expected_level_id")
	}
	timeout, err := matchingCaseTimeout(args[0])
	if err != nil {
		return err
	}
	level, err := strconv.ParseInt(args[1], 10, 32)
	if err != nil || level <= 0 {
		return fmt.Errorf("invalid level ID %q", args[1])
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	deadline := time.Now().Add(timeout)
	var unitID uint64
	for {
		view, room, err := readMatchingAdmission(action, user)
		if err != nil {
			return err
		}
		if unitID != 0 && unitID != view.GetUnitId() {
			return fmt.Errorf("Unit changed while awaiting admission: %d -> %d", unitID, view.GetUnitId())
		}
		unitID = view.GetUnitId()
		admission, done, err := completedMatchingAdmission(view, room, int32(level))
		if err != nil {
			return err
		}
		if done {
			user.SetExtralData(matchingAdmissionKey, admission)
			action.Log("admission verified: unit=%d room=%s faction=%d level=%d", admission.UnitID,
				admission.RoomID, admission.FactionID, admission.LevelID)
			return nil
		}
		if !time.Now().Before(deadline) {
			return fmt.Errorf("admission timeout: unit=%d status=%s room=%s initialized=%t",
				unitID, view.GetStatus(), room.GetRoomKey().GetClientId(), room.GetInit())
		}
		time.Sleep(250 * time.Millisecond)
	}
}

func savedMatchingAdmission(user user_data.User) (matchingAdmission, error) {
	value, ok := user.GetExtralData(matchingAdmissionKey).(matchingAdmission)
	if !ok || value.UnitID == 0 || value.RoomID == "" {
		return matchingAdmission{}, fmt.Errorf("%s has no verified admission; run matching_wait_admitted first", user.GetOpenId())
	}
	return value, nil
}

func assertMatchingAdmissionUnchanged(action *robot_case.TaskActionCase, user user_data.User) (matchingAdmission, error) {
	expected, err := savedMatchingAdmission(user)
	if err != nil {
		return expected, err
	}
	view, room, err := readMatchingAdmission(action, user)
	if err != nil {
		return expected, err
	}
	actual, done, err := completedMatchingAdmission(view, room, expected.LevelID)
	if err != nil {
		return expected, err
	}
	if !done || actual != expected {
		return expected, fmt.Errorf("admission changed: expected %+v, got %+v (complete=%t)", expected, actual, done)
	}
	return actual, nil
}

func compareMatchingAdmissions(actual, reference matchingAdmission, roomRelation, factionRelation string) error {
	if roomRelation != "same" && roomRelation != "different" {
		return fmt.Errorf("room relation must be same or different")
	}
	if factionRelation != "same" && factionRelation != "different" && factionRelation != "any" {
		return fmt.Errorf("faction relation must be same, different or any")
	}
	if actual.UnitID == reference.UnitID {
		return fmt.Errorf("expected distinct Units, both have %d", actual.UnitID)
	}
	if (actual.RoomID == reference.RoomID) != (roomRelation == "same") {
		return fmt.Errorf("room relation %s failed: actual=%s reference=%s", roomRelation, actual.RoomID, reference.RoomID)
	}
	if factionRelation != "any" && (actual.FactionID == reference.FactionID) != (factionRelation == "same") {
		return fmt.Errorf("faction relation %s failed: actual=%d reference=%d", factionRelation, actual.FactionID, reference.FactionID)
	}
	return nil
}

func MatchingAssertAdmissionCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 3 {
		return fmt.Errorf("need reference_open_id room_relation faction_relation")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	referenceHolder := user_data.UserContainerTryGetUser(args[0])
	if referenceHolder == nil || !referenceHolder.IsUserVaildLogin() {
		return fmt.Errorf("reference user %q is not logged in", args[0])
	}
	actual, err := assertMatchingAdmissionUnchanged(action, user)
	if err != nil {
		return err
	}
	reference, err := assertMatchingAdmissionUnchanged(action, referenceHolder.GetUser())
	if err != nil {
		return err
	}
	if err := compareMatchingAdmissions(actual, reference, args[1], args[2]); err != nil {
		return err
	}
	action.Log("admission relation verified: actual=%+v reference=%+v room=%s faction=%s", actual, reference, args[1], args[2])
	return nil
}

func MatchingAssertUnchangedCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("matching_assert_unchanged accepts no arguments")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	_, err = assertMatchingAdmissionUnchanged(action, user)
	return err
}

func MatchingAssertSearchingCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("matching_assert_searching accepts no arguments")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	view, room, err := readMatchingAdmission(action, user)
	if err != nil {
		return err
	}
	if view.GetStatus() != public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING ||
		room.GetRoomKey().GetClientId() != "" {
		return fmt.Errorf("expected searching without Orbit room, got status=%s room=%s", view.GetStatus(), room.GetRoomKey().GetClientId())
	}
	return nil
}

func MatchingAssertNewUnitCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 0 {
		return fmt.Errorf("matching_assert_new_unit accepts no arguments")
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	previous, err := savedMatchingAdmission(user)
	if err != nil {
		return err
	}
	view, _, err := readMatchingAdmission(action, user)
	if err != nil {
		return err
	}
	if view.GetUnitId() == previous.UnitID {
		return fmt.Errorf("new match reused completed Unit %d", previous.UnitID)
	}
	return nil
}

func MatchingWaitOrbitLeftCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 1 {
		return fmt.Errorf("need timeout_seconds; trigger real DS exit before or during this action")
	}
	timeout, err := matchingCaseTimeout(args[0])
	if err != nil {
		return err
	}
	user, err := requireCaseUser(holder)
	if err != nil {
		return err
	}
	if _, err := savedMatchingAdmission(user); err != nil {
		return err
	}
	deadline := time.Now().Add(timeout)
	for {
		var room *public_protocol_pbdesc.DOrbitRoomUserData
		err := action.AwaitTask(user.RunTaskDefaultTimeout(func(taskAction *user_data.TaskActionUser) error {
			var err error
			room, err = readOrbitRoom(taskAction)
			return err
		}, "Wait Orbit Exit"))
		if err != nil {
			return err
		}
		if room.GetRoomKey().GetClientId() == "" {
			return nil
		}
		if !time.Now().Before(deadline) {
			return fmt.Errorf("Orbit exit timeout: room=%s", room.GetRoomKey().GetClientId())
		}
		time.Sleep(250 * time.Millisecond)
	}
}

// This schedules a boundary action only; later admission assertions determine the business outcome.
// Supply the actual end_join_timepoint from room_ready, not an estimated room lifetime.
func MatchingWaitUntilCase(action *robot_case.TaskActionCase, holder *user_data.UserHolder, args []string) error {
	if len(args) != 1 {
		return fmt.Errorf("need actual end_join_timepoint as Unix seconds")
	}
	seconds, err := strconv.ParseInt(args[0], 10, 64)
	if err != nil || seconds <= 0 {
		return fmt.Errorf("invalid absolute deadline %q", args[0])
	}
	deadline := time.Unix(seconds, 0)
	if time.Until(deadline) > 14*time.Minute {
		return fmt.Errorf("absolute deadline is more than 14 minutes away")
	}
	// TaskActionCase owns the user's action guard. Release it for the whole wait so
	// heartbeat tasks keep running instead of accumulating and bursting at the deadline.
	if err := action.BeforeYield(); err != nil {
		return err
	}
	for time.Now().Before(deadline) {
		time.Sleep(min(time.Until(deadline), time.Second))
	}
	// HookRun expects the same guard to be held when the case returns.
	if err := action.AfterYield(); err != nil {
		return err
	}
	action.Log("actual waiting-room deadline reached: %d", seconds)
	return nil
}
