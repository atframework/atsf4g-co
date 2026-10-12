package atsf4g_go_robot_rpc

import (
	"testing"

	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	user_data "github.com/atframework/robot-go/data"
)

type matchingViewTestUser struct {
	user_data.User
	data map[string]any
}

func (u *matchingViewTestUser) SetExtralData(key string, value any) { u.data[key] = value }
func (u *matchingViewTestUser) GetExtralData(key string) any        { return u.data[key] }
func (u *matchingViewTestUser) Log(string, ...any)                  {}

func TestSaveMatchingViewUpdatesFactionAcrossAttempts(t *testing.T) {
	user := &matchingViewTestUser{data: make(map[string]any)}
	SaveMatchingView(user, &public_protocol_pbdesc.DMatchingClientView{
		UnitId: 91, FactionId: 7,
		Status: public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED,
	})
	if UnitIdFromUser(user) != 91 || MatchingFactionIdFromUser(user) != 7 {
		t.Fatalf("completed view lost Unit/faction: %d/%d", UnitIdFromUser(user), MatchingFactionIdFromUser(user))
	}
	SaveMatchingView(user, &public_protocol_pbdesc.DMatchingClientView{
		UnitId: 92,
		Status: public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING,
	})
	if UnitIdFromUser(user) != 92 || MatchingFactionIdFromUser(user) != 0 ||
		MatchingStatusFromUser(user) != public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING {
		t.Fatalf("new search retained old Unit/faction: %d/%d", UnitIdFromUser(user), MatchingFactionIdFromUser(user))
	}
}
