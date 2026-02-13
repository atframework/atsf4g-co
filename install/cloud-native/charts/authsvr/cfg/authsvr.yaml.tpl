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
    relogin_expire: 7200                             # relogin to the same gamesvr in 2 hours relogin
