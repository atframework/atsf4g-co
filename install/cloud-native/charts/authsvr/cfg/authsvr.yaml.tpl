{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

authsvr:
  version_conf: ../cfg/cfg_version.xml               # version file
  strategy_conf: ../cfg/cfg_strategy.loginsvr.xml    # strategy file
  # cdn_url:                                         # cdn url
  # debug_platform: 0                                # debug platform mode
  start_time: 0                                      # service start time
  end_time: 0                                        # service end time
  # white_openid_list:                               # white openid list, can ignore start time and end time
  lobbysvr:
    router:
      type_id: 12
      type_name: lobbysvr
      policy: hash # random/round_robin/hash
      # policy_selector:
      #   api_version: apps/v1
      #   kind: StatefulSet
      #   name: authsvr
      #   namespace_name: hello
      #   service_subset: ""
      #   labels:
      #     district: cn
    relogin_expire: 14400                             # relogin to the same gamesvr in 4 hours relogin
