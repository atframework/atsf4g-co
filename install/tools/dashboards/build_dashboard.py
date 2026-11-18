#!/usr/bin/env python3
"""Generate the ATSF4G OpenObserve v8 dashboard for RPC and process analysis."""
import json
from datetime import datetime, timezone
from pathlib import Path

OUTPUT = Path(__file__).with_name("atframework-observability.dashboard.json")
TRACE = "atframework_traces"

COMMON = (
    'deployment_environment_name=~"${deployment_environment:pipe}",'
    'service_area_zone_id=~"${zone_id:pipe}",'
    'service_name=~"${service_name:pipe}",'
    'service_instance_id=~"${process:pipe}"'
)
TRACE_METRIC_COMMON = COMMON + ',span_kind=~"${span_kind:pipe}",rpc_system_name=~"${rpc_system:pipe}",rpc_method=~"${rpc_method:pipe}"'
TRACE_COMMON = (
    "service_deployment_environment_name IN (${deployment_environment:singlequote}) AND "
    "service_service_area_zone_id IN (${zone_id:singlequote}) AND "
    "service_name IN (${service_name:singlequote}) AND "
    "service_service_instance_id IN (${process:singlequote})"
)
RPC_COMMON = (
    TRACE_COMMON + " AND rpc_system_name IN (${rpc_system:singlequote}) AND "
    "rpc_method IN (${rpc_method:singlequote})"
)


def axis(label, column, aggregation=None, alias=None, sort=None, non_timeseries=False):
    item = {"label": label, "alias": alias or column, "column": column}
    if aggregation:
        item["aggregationFunction"] = aggregation
    if sort:
        item["sortBy"] = sort
    if non_timeseries:
        item["treatAsNonTimeseries"] = True
    return item


def panel(panel_id, title, query, query_type="promql", chart="line", unit="numbers",
          x=None, y=None, breakdown=None, stream=None, description="", width=96, height=24):
    if stream is None:
        stream = TRACE if query_type == "sql" else "atframework_trace_calls"
    fields = {
        "stream": stream,
        "stream_type": "traces" if query_type == "sql" else "metrics",
        "x": x or [],
        "y": y or [],
        "z": [],
        "breakdown": breakdown or [],
        "filter": {"filterType": "group", "logicalOperator": "AND", "conditions": []},
    }
    config = {"show_legends": chart != "metric", "legends_position": "bottom", "unit": unit,
              "decimals": 2, "line_thickness": 2, "line_interpolation": "smooth",
              "show_symbol": False, "show_gridlines": True, "axis_border_show": True,
              "top_results": 10, "top_results_others": False, "drilldown": []}
    if chart == "table":
        config.update({"table_pagination": True, "table_pagination_rows_per_page": 50,
                       "table_filtering": True, "wrap_table_cells": True, "sticky_first_column": True})
    return {
        "_width": width,
        "_height": height,
        "id": panel_id,
        "type": chart,
        "title": title,
        "description": description,
        "config": config,
        "queryType": query_type,
        "queries": [{
            "query": query,
            "vrlFunctionQuery": "",
            "customQuery": True,
            "fields": fields,
            "config": {"promql_legend": ""},
        }],
    }


def trace_time_x():
    return axis("Time", "x_axis_1", "histogram", "x_axis_1", "ASC")


def count_y():
    return axis("Count", "y_axis_1", "count", "y_axis_1")


def series_y(alias="y_axis_1", aggregation="count", label="Value"):
    return axis(label, alias, aggregation, alias)


def tab(tab_id, name, panels):
    result = []
    x = 0
    y = 0
    row_height = 0
    for index, item in enumerate(panels):
        width = item.pop("_width")
        height = item.pop("_height")
        if x + width > 192:
            y += row_height
            x = 0
            row_height = 0
        item["layout"] = {"x": x, "y": y, "w": width, "h": height, "i": index}
        result.append(item)
        x += width
        row_height = max(row_height, height)
        if x >= 192:
            y += row_height
            x = 0
            row_height = 0
    return {"tabId": tab_id, "name": name, "panels": result}


def variable(name, label, field, stream=TRACE, stream_type="traces", default="",
             variable_type="query_values", options=None):
    item = {
        "type": variable_type,
        "name": name,
        "label": label,
        "query_data": {"stream_type": stream_type, "stream": stream, "field": field,
                       "max_record_size": 100, "filter": []},
        "value": default,
        "options": [],
        "multiSelect": True,
        "selectAllValueForMultiSelect": "all",
        "escapeSingleQuotes": True,
        "hideOnDashboard": False,
        "scope": "global",
    }
    if variable_type == "custom":
        item.pop("query_data")
        item["options"] = options or []
    return item


def main():
    overview = [
        panel("overview_request_rate", "Inbound RPC span rate by service",
              f"sum by(service_name)(rate(atframework_trace_calls{{{TRACE_METRIC_COMMON}}}[5m]))",
              description="SPAN_KIND_SERVER by default; sampled spans, not a guaranteed total RPC count."),
        panel("overview_error_rate", "Trace error ratio by service",
              f"sum by(service_name)(rate(atframework_trace_calls{{status_code=\"STATUS_CODE_ERROR\",{TRACE_METRIC_COMMON}}}[5m])) / "
              f"sum by(service_name)(rate(atframework_trace_calls{{{TRACE_METRIC_COMMON}}}[5m]))",
              description="STATUS_CODE_ERROR rate divided by total RPC rate; business result codes are shown separately."),
        panel("overview_latency", "RPC p95 duration",
              f"histogram_quantile(0.95, sum by(service_name, le)(rate(atframework_trace_duration_bucket{{{TRACE_METRIC_COMMON}}}[5m])))",
              unit="milliseconds", description="p95 duration in milliseconds; buckets are aggregated before quantile."),
        panel("overview_mean_latency", "Mean RPC duration",
              f"(sum by(service_name)(rate(atframework_trace_duration_sum{{{TRACE_METRIC_COMMON}}}[5m])) / "
              f"sum by(service_name)(rate(atframework_trace_duration_count{{{TRACE_METRIC_COMMON}}}[5m]))) > 0",
              unit="milliseconds", description="Sum of duration sums divided by counts; zero-count groups are excluded."),
        panel("overview_replicas", "Expected replicas",
              f"max by(service_name)(hpa_expect_replicas{{{COMMON}}})",
              description="Maximum expected replica recommendation per service."),
        panel("overview_cpu", "Process CPU usage (per mille)",
              f"avg by(service_instance_id)(hpa_rule_cpu{{{COMMON}}})",
              description="CPU usage in per-mille units; one series per process instance."),
        panel("overview_memory", "Process memory",
              f"max by(service_instance_id)(hpa_rule_memory{{{COMMON}}})",
              unit="bytes", description="Resident set size (RSS) per process instance."),
        panel("overview_tasks", "Coroutine tasks",
              f"max by(service_instance_id)(service_coroutine_task_count{{{COMMON}}})",
              description="Current coroutine task count per process instance."),
    ]

    rpc = [
        panel("rpc_method_rate", "RPC method span rate",
              f"sum by(rpc_method)(rate(atframework_trace_calls{{{TRACE_METRIC_COMMON}}}[5m]))",
              description="SPAN_KIND_SERVER by default to avoid counting client and server spans as separate RPC calls."),
        panel("rpc_error_method", "Top error methods",
              f"topk(10, sum by(rpc_method)(rate(atframework_trace_calls{{status_code=\"STATUS_CODE_ERROR\",{TRACE_METRIC_COMMON}}}[5m])))",
              chart="h-bar", description="Top 10 methods by STATUS_CODE_ERROR rate."),
        panel("rpc_method_latency", "RPC p95 duration by method",
              f"histogram_quantile(0.95, sum by(rpc_method, le)(rate(atframework_trace_duration_bucket{{{TRACE_METRIC_COMMON}}}[5m])))",
              unit="milliseconds", description="p95 by fully qualified RPC method."),
        panel("rpc_error_latency", "Error RPC p95 duration",
              f"histogram_quantile(0.95, sum by(rpc_method, le)(rate(atframework_trace_duration_bucket{{status_code=\"STATUS_CODE_ERROR\",{TRACE_METRIC_COMMON}}}[5m])))",
              unit="milliseconds", description="p95 computed only for STATUS_CODE_ERROR spans, separate from successful traffic."),
        panel("rpc_system_rate", "RPC spans by system",
              f"SELECT histogram(_timestamp) AS x_axis_1, rpc_system_name AS breakdown_1, COUNT(*) AS y_axis_1 "
              f"FROM {TRACE} WHERE {RPC_COMMON} GROUP BY x_axis_1, breakdown_1 ORDER BY x_axis_1",
              query_type="sql", x=[trace_time_x()], y=[count_y()],
              breakdown=[axis("RPC system", "breakdown_1")]),
        panel("rpc_result_codes", "RPC business result codes",
              f"SELECT rpc_system_name, rpc_method, rpc_atrpc_result_code, COUNT(*) AS count "
              f"FROM {TRACE} WHERE {RPC_COMMON} "
              "GROUP BY rpc_system_name, rpc_method, rpc_atrpc_result_code ORDER BY count DESC LIMIT 100",
              query_type="sql", chart="table", x=[axis("System", "rpc_system_name"),
                                                axis("Method", "rpc_method"),
                                                axis("Result code", "rpc_atrpc_result_code")],
              y=[count_y()], description="Distribution only; non-zero business codes are not automatically trace errors.",
              width=192, height=40),
        panel("rpc_error_traces", "Error traces by method",
              f"SELECT histogram(_timestamp) AS x_axis_1, rpc_method AS breakdown_1, COUNT(*) AS y_axis_1 "
              f"FROM {TRACE} WHERE {RPC_COMMON} AND span_status IN ('ERROR') "
              "GROUP BY x_axis_1, breakdown_1 ORDER BY x_axis_1",
              query_type="sql", x=[trace_time_x()], y=[count_y()],
              breakdown=[axis("RPC method", "breakdown_1")]),
        panel("rpc_slow_spans", "Slowest RPC spans",
              f"SELECT start_time, trace_id, service_name, service_service_instance_id, rpc_system_name, "
              f"rpc_method, rpc_atrpc_result_code, span_status, duration FROM {TRACE} "
              f"WHERE {RPC_COMMON} ORDER BY duration DESC LIMIT 50",
              query_type="sql", chart="table", unit="microseconds",
              x=[axis("Start time", "start_time"), axis("Trace ID", "trace_id"),
                 axis("Service", "service_name"), axis("Process", "service_service_instance_id"),
                 axis("RPC system", "rpc_system_name"), axis("RPC method", "rpc_method"),
                 axis("Result code", "rpc_atrpc_result_code"), axis("Span status", "span_status")],
              y=[series_y("duration", "max", "Duration")],
              description="Top 50 spans by duration; use trace_id for drill-down.", width=192, height=40),
    ]

    process = [
        panel("process_cpu", "Process CPU usage (per mille)",
              f"avg by(service_instance_id)(hpa_rule_cpu{{{COMMON}}})"),
        panel("process_main_thread_cpu", "Main-thread CPU usage (per mille)",
              f"avg by(service_instance_id)(hpa_rule_main_thread_cpu{{{COMMON}}})"),
        panel("process_memory", "Process memory (RSS)",
              f"max by(service_instance_id)(hpa_rule_memory{{{COMMON}}})", unit="bytes"),
        panel("process_recent_tasks", "Recent maximum task count",
              f"max by(service_instance_id)(hpa_rule_recent_max_task_count{{{COMMON}}})"),
        panel("process_coroutine_max", "Maximum coroutine task count",
              f"max by(service_instance_id)(service_coroutine_task_max_count{{{COMMON}}})"),
        panel("process_trace_count", "Process trace count",
              f"SELECT histogram(_timestamp) AS x_axis_1, COUNT(*) AS y_axis_1 FROM {TRACE} "
              f"WHERE {RPC_COMMON} GROUP BY x_axis_1 ORDER BY x_axis_1",
              query_type="sql", x=[trace_time_x()], y=[count_y()]),
        panel("process_latency", "Process p95 RPC duration",
              f"histogram_quantile(0.95, sum by(service_instance_id, le)(rate(atframework_trace_duration_bucket{{{TRACE_METRIC_COMMON}}}[5m])))",
              unit="milliseconds"),
        panel("process_stateful_index", "Stateful index",
              f"max by(service_name)(hpa_stateful_index{{{COMMON}}})",
              description="Stateful index per service; expected replicas is available on the Overview tab."),
    ]

    document = {
        "version": 8,
        "dashboardId": "atframework-rpc-observability",
        "title": "ATSF4G RPC & Service Observability",
        "description": "RED for RPC/traces and process-level utilization. Variables support environment, zone, service type, process instance, RPC system, method, and span kind.",
        "role": "",
        "owner": "admin@owent.net",
        "created": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "defaultDatetimeDuration": {"type": "relative", "relativeTimePeriod": "1h"},
        "variables": {
            "list": [
                variable("deployment_environment", "Environment", "service_deployment_environment_name"),
                variable("zone_id", "Zone ID", "service_service_area_zone_id"),
                variable("service_name", "Service type", "service_name"),
                variable("process", "Process instance", "service_service_instance_id"),
                variable("rpc_system", "RPC system", "rpc_system_name"),
                variable("rpc_method", "RPC method", "rpc_method"),
                variable("span_kind", "Span kind", "span_kind", "atframework_trace_calls", "metrics",
                         default="SPAN_KIND_SERVER"),
            ],
            "showDynamicFilters": True,
        },
        "tabs": [tab("overview", "Overview", overview), tab("rpc", "RPC Analysis", rpc),
                 tab("process", "Process", process)],
    }
    OUTPUT.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(OUTPUT)


if __name__ == "__main__":
    main()
