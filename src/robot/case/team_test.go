package atsf4g_go_robot_case

import (
	"os"
	"path/filepath"
	"testing"

	robot_case "github.com/atframework/robot-go/case"
)

func TestMatchingTeam3v3ConfigCasesAreRegistered(t *testing.T) {
	content, err := os.ReadFile(filepath.Join("..", "case_config", "matching_team_3v3.conf"))
	if err != nil {
		t.Fatalf("read matching team 3v3 config: %v", err)
	}
	lines, err := robot_case.ParseCaseFileContent(string(content))
	if err != nil {
		t.Fatalf("parse matching team 3v3 config: %v", err)
	}
	registered := make(map[string]struct{})
	for _, name := range robot_case.AutoCompleteCaseName("") {
		registered[name] = struct{}{}
	}
	for _, line := range lines {
		if line.IsControl {
			continue
		}
		if _, ok := registered[line.Stress.CaseName]; !ok {
			t.Errorf("case %q is not registered", line.Stress.CaseName)
		}
	}
}
