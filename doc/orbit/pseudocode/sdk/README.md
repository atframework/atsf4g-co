# sdk

用于编写两侧 SDK 的伪代码：

- Service 侧 SDK 接口与回调，当前主文件见 `external/external_client.pseudo.h` 与 `external/external_client.pseudo.cpp`
- DS SDK 的心跳、退出、转发封装，当前主文件见 `ds/ds_client.pseudo.h` 与 `ds/ds_client.pseudo.cpp`

当前约定：

- Service 侧 SDK 负责连接 DSC，并提供启动 DS 的 RPC 接口。
- DS 侧 SDK 负责通过本地 channel 与 DSA 通信，并提供发送消息、接收消息接口。