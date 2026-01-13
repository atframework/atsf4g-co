{{ include "atapp.yaml" . }}
{{ include "atapp.logic.yaml" . }}

dtcoordsvr:
  lru_expired_duration: 1800s    # 30min for lru cache expired
  lru_max_cache_count: 30000     # max count of transaction cache
  transaction_default_timeout: 10s