# ATSF4G OpenObserve dashboard

`atframework-observability.dashboard.json` is an OpenObserve **v8** dashboard with Overview, RPC Analysis, and Process tabs.
It uses RED for RPC/traces and per-process utilization metrics. All query variables support multi-select and **All**.
Panels are 96×24, the two detail tables are 192×40 with 50 rows per page, and chart legends are placed at the bottom without a fixed pixel size so they do not create empty space.

## Generate and validate

```bash
python install/tools/dashboards/build_dashboard.py
python install/tools/dashboards/validate_dashboard.py install/tools/dashboards/atframework-observability.dashboard.json
```

The generator rewrites the JSON file; make changes in `build_dashboard.py` rather than editing generated JSON by hand.
The validator checks panel IDs, layout, and stream/filter structure. With `--live`, it executes every query with
selected, multi-value, and All substitutions; when an RPC trace sample exists, it also requires the slow-span panel to
return data in all three modes:

```bash
export OPENOBSERVE_URL=http://127.0.0.1:5080
export OPENOBSERVE_ORG=<organization-id>
export OPENOBSERVE_USER=admin@owent.net
export OPENOBSERVE_PASSWORD=<password>
python validate_dashboard.py atframework-observability.dashboard.json --live
```

Do not store credentials in the dashboard JSON or scripts.

## Import

Use **Dashboards → Import** and select `atframework-observability.dashboard.json`. The API equivalent is
`POST /api/<organization-id>/dashboards?folder=<folder-id>` with the JSON body.

## Filters and fields

| Variable | Trace field | Metric field |
| --- | --- | --- |
| Environment | `service_deployment_environment_name` | `deployment_environment_name` |
| Zone ID | `service_service_area_zone_id` | `service_area_zone_id` |
| Service type | `service_name` | `service_name` |
| Process instance | `service_service_instance_id` | `service_instance_id` |
| RPC system | `rpc_system_name` | `rpc_system_name` |
| RPC method | `rpc_method` | `rpc_method` |
| Span kind | `span_kind` | `span_kind` |

Filters are applied only where the label exists: HPA and coroutine panels use environment, zone, service type, and
process; `span_kind` and `rpc_method` apply to the `atframework_trace_*` family. Trace SQL uses
`IN (${...:singlequote})` for exact multi-value matching and All-placeholder handling; PromQL uses regular-expression
matchers with `${...:pipe}`. RPC System uses the stable `rpc.system.name` attribute (`rpc_system_name` in PromQL) in
both SQL and PromQL. The removed `rpc.service` attribute is not used by any panel.

`atframework_trace_calls` is a Counter for request rates. `atframework_trace_duration_*` is a millisecond histogram;
aggregate bucket rates with `sum by (le)` before `histogram_quantile`. The default `span_kind=SPAN_KIND_SERVER` avoids
counting client and server spans as separate RPC calls; choose other kinds or All intentionally. Counts are sampled
recording spans, not guaranteed RPC totals. `span_status`/`status_code` identifies trace errors, while
`rpc_atrpc_result_code` is shown only as a business result distribution and is not automatically an error signal.
`atframework_inserver_rpc_*` metrics are Gauges and are not used with `rate()`.
