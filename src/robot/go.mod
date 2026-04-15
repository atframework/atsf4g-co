module github.com/atframework/atsf4g-co-robot

go 1.25.3

replace github.com/atframework/robot-go => ./atframework/robot-go

replace github.com/xresloader/xresloader => ./atframework/robot-go/third_party/protocols/core

replace github.com/xresloader/xres-code-generator => ./atframework/robot-go/third_party/protocols/code

replace github.com/atframework/atframe-utils-go => ./atframework/atframe-utils-go

replace github.com/atframework/atsf4g-co/component/public => ./protocol

require (
	github.com/atframework/atframe-utils-go v1.0.5-0.20260415092013-23b411f59869
	github.com/atframework/atsf4g-co/component/public v0.0.0-00010101000000-000000000000
	github.com/atframework/robot-go v0.0.0-00010101000000-000000000000
	github.com/shirou/gopsutil/v4 v4.26.2
	github.com/xresloader/xresloader v0.0.0-00010101000000-000000000000
	google.golang.org/protobuf v1.36.11
)

require (
	github.com/cespare/xxhash/v2 v2.3.0 // indirect
	github.com/chzyer/readline v1.5.1 // indirect
	github.com/dgryski/go-rendezvous v0.0.0-20200823014737-9f7001d12a5f // indirect
	github.com/ebitengine/purego v0.10.0 // indirect
	github.com/go-ole/go-ole v1.2.6 // indirect
	github.com/golang/snappy v0.0.4 // indirect
	github.com/google/flatbuffers v25.12.19+incompatible // indirect
	github.com/gorilla/websocket v1.5.3 // indirect
	github.com/klauspost/compress v1.17.10 // indirect
	github.com/lufia/plan9stats v0.0.0-20211012122336-39d0f177ccd0 // indirect
	github.com/panjf2000/ants/v2 v2.12.0 // indirect
	github.com/pierrec/lz4/v4 v4.1.22 // indirect
	github.com/power-devops/perfstat v0.0.0-20240221224432-82ca36839d55 // indirect
	github.com/redis/go-redis/v9 v9.18.0 // indirect
	github.com/tklauser/go-sysconf v0.3.16 // indirect
	github.com/tklauser/numcpus v0.11.0 // indirect
	github.com/xresloader/xres-code-generator v0.0.0-00010101000000-000000000000 // indirect
	github.com/yusufpapurcu/wmi v1.2.4 // indirect
	go.uber.org/atomic v1.11.0 // indirect
	golang.org/x/crypto v0.47.0 // indirect
	golang.org/x/sync v0.11.0 // indirect
	golang.org/x/sys v0.41.0 // indirect
	gopkg.in/yaml.v3 v3.0.1 // indirect
)
