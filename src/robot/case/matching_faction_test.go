package atsf4g_go_robot_case

import (
	"os"
	"path/filepath"
	"testing"

	public_protocol_pbdesc "github.com/atframework/atsf4g-co/component/public/protocol/pbdesc"
	robot_case "github.com/atframework/robot-go/case"
)

func TestMatchingWaitSuccessStatus(t *testing.T) {
	for _, test := range []struct {
		status public_protocol_pbdesc.EnMatchingUnitLifecycleStatus
		done   bool
		failed bool
	}{
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_INVALID, false, false},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_SEARCHING, false, false},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CONFIRMING, false, false},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CREATING_BATTLE, false, false},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FINISHED, true, false},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_CANCELLED, false, true},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_TIMEOUT, false, true},
		{public_protocol_pbdesc.EnMatchingUnitLifecycleStatus_EN_MATCHING_UNIT_LIFECYCLE_STATUS_FAILED, false, true},
	} {
		t.Run(test.status.String(), func(t *testing.T) {
			done, err := matchingWaitSuccessStatus(test.status)
			if done != test.done || (err != nil) != test.failed {
				t.Fatalf("got done=%v err=%v; want done=%v failed=%v", done, err, test.done, test.failed)
			}
		})
	}
}

func TestMatchingFactionCaseConfigs(t *testing.T) {
	registered := make(map[string]bool)
	for _, name := range robot_case.AutoCompleteCaseName("") {
		registered[name] = true
	}
	for _, filename := range []string{
		"matching_balanced_solo_3v3.conf",
		"matching_priority_solo_3v3.conf",
		"matching_priority_mixed_2v3.conf",
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
				t.Fatal("case must initialize users with login")
			}
			login := lines[0].Stress
			checkedAllUsers := false
			for _, line := range lines {
				if line.IsControl || line.BackgroundRunning {
					t.Fatal("case must use sequential registered actions")
				}
				params := line.Stress
				if !registered[params.CaseName] {
					t.Errorf("case %q is not registered", params.CaseName)
				}
				if !params.ErrorBreak || params.OpenIDPrefix != login.OpenIDPrefix ||
					params.OpenIDStart < login.OpenIDStart || params.OpenIDEnd > login.OpenIDEnd ||
					params.OpenIDStart >= params.OpenIDEnd {
					t.Errorf("invalid user range or error handling for %s", params.CaseName)
				}
				if params.CaseName == "matching_wait_success" {
					checkedAllUsers = params.OpenIDStart == login.OpenIDStart && params.OpenIDEnd == login.OpenIDEnd
				}
			}
			if !checkedAllUsers {
				t.Fatal("case must require matching success for every user, including team members")
			}
		})
	}
}
