module github.com/atframework/atsf4g-co/component/public

go 1.25.3

replace github.com/xresloader/xresloader => ../../atframework/robot-go/third_party/protocols/core

replace github.com/xresloader/xres-code-generator => ../../atframework/robot-go/third_party/protocols/code

require (
	github.com/atframework/atframe-utils-go v1.0.3
	github.com/xresloader/xres-code-generator v0.0.0-00010101000000-000000000000
	github.com/xresloader/xresloader v0.0.0-00010101000000-000000000000
	google.golang.org/protobuf v1.36.11
)
