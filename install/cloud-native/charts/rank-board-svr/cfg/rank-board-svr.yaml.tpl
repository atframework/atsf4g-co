{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

ranksvr-ranking:
  rank_slave_num: {{ .Values.rank_slave_num }}
  rank_dunmp_task_max_num: 100 # settle 100 users in one loop unit
  rank_refresh_limit_second_interval: {{ .Values.rank_settlement_frequency_limit }} # apollo 请求限频 大于 settle_loop_count
  rank_max_batch_get_num: {{ .Values.rank_max_batch_get_num}}
  max_mirror_count: {{ .Values.max_mirror_count}}
  rank_history_version_max_count: {{ .Values.rank_history_version_max_count}}
  rank_slice_max_count: {{ .Values.rank_slice_max_count}}
  rank_tree_degree: {{ .Values.rank_tree_degree}}
  rank_save_interval: {{ .Values.rank_save_interval}}
  rank_btree_degree: {{ .Values.rank_btree_degree}}
  pushlisher_congihure:
    gc_expire_duration:  {{ .Values.pushlisher_congihure.rank_save_interval}}
    gc_log_size:  {{ .Values.pushlisher_congihure.rank_save_interval}}
    max_log_size:  {{ .Values.pushlisher_congihure.rank_save_interval}}