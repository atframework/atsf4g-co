#!/usr/bin/env python3
"""Validate dashboard structure and, optionally, execute every panel query on OpenObserve."""
import argparse
import base64
import json
import os
from pathlib import Path
import time
import urllib.parse
import urllib.request

SUBSTITUTIONS = {
    "deployment_environment": "production",
    "zone_id": "1",
    "service_name": ".*",
    "process": ".*",
    "rpc_system": "atrpc.db",
    "rpc_method": ".*",
    "span_kind": "SPAN_KIND_SERVER",
}
ALL_SUBSTITUTIONS = {name: "_o2_all_" for name in SUBSTITUTIONS}
MISSING_VALUE = "__atsf4g_validation_missing__"
SQL_SAMPLE_COLUMNS = {
    "deployment_environment": "service_deployment_environment_name",
    "zone_id": "service_service_area_zone_id",
    "service_name": "service_name",
    "process": "service_service_instance_id",
    "rpc_system": "rpc_system_name",
    "rpc_method": "rpc_method",
}


def panels(document):
    for tab in document["tabs"]:
        for panel in tab["panels"]:
            yield tab, panel


def validate_structure(document):
    assert document["version"] == 8
    assert document["dashboardId"] and document["title"]
    identifiers = [panel["id"] for tab in document["tabs"] for panel in tab["panels"]]
    assert len(identifiers) == len(set(identifiers)), "duplicate panel id"
    for _tab, panel in panels(document):
        layout = panel["layout"]
        assert 0 <= layout["x"] and 0 < layout["w"] and layout["x"] + layout["w"] <= 192
        assert layout["y"] >= 0 and layout["h"] > 0
        for query in panel["queries"]:
            fields = query["fields"]
            assert fields["filter"]["filterType"] == "group"
            assert fields["stream"] and fields["stream_type"]
            assert query["query"]


def substitute(query, values):
    for name, value in values.items():
        selected = value if isinstance(value, list) else [value]
        query = query.replace("${" + name + ":pipe}", "|".join(selected))
        quoted = ",".join("'" + item.replace("'", "''") + "'" for item in selected)
        query = query.replace("${" + name + ":singlequote}", quoted)
    return query


def multi_values(values):
    return {name: [value, MISSING_VALUE] for name, value in values.items()}


def request_json(url, token, payload=None):
    request = urllib.request.Request(url, data=payload)
    request.add_header("Authorization", "Basic " + token)
    if payload is not None:
        request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def search_sql(base_url, organization, token, sql, start, end):
    body = json.dumps({"query": {"start_time": start, "end_time": end, "sql": sql}}).encode()
    result = request_json(f"{base_url}/api/{organization}/_search?type=traces", token, body)
    assert "hits" in result, result
    return result


def find_sql_substitutions(base_url, organization, token, start, end):
    columns = ", ".join(f"{column} AS {name}_value" for name, column in SQL_SAMPLE_COLUMNS.items())
    required = " AND ".join(f"{column} IS NOT NULL" for column in SQL_SAMPLE_COLUMNS.values())
    result = search_sql(base_url, organization, token,
                        f"SELECT {columns} FROM atframework_traces WHERE {required} LIMIT 1", start, end)
    if not result["hits"]:
        return None
    sample = result["hits"][0]
    values = {name: str(sample[f"{name}_value"]) for name in SQL_SAMPLE_COLUMNS}
    values["span_kind"] = SUBSTITUTIONS["span_kind"]
    return values


def validate_live(document, base_url, organization, email, password):
    token = base64.b64encode(f"{email}:{password}".encode()).decode()
    end = int(time.time() * 1_000_000)
    start = end - 3600 * 1_000_000
    sql_values = find_sql_substitutions(base_url, organization, token, start, end)
    for tab, panel in panels(document):
        selected_values = sql_values if panel["queryType"] == "sql" and sql_values else SUBSTITUTIONS
        profiles = (("selected", selected_values), ("multi", multi_values(selected_values)),
                    ("all", ALL_SUBSTITUTIONS))
        for query in panel["queries"]:
            for mode, values in profiles:
                rendered = substitute(query["query"], values)
                if panel["queryType"] == "promql":
                    url = (f"{base_url}/api/{organization}/prometheus/api/v1/query?" +
                           urllib.parse.urlencode({"query": rendered}))
                    result = request_json(url, token)
                    assert result.get("status") == "success", f"{panel['title']} ({mode}): {result}"
                else:
                    result = search_sql(base_url, organization, token, rendered, start, end)
                    if panel["id"] == "rpc_slow_spans" and sql_values:
                        assert result["hits"], f"{panel['title']} returned no RPC spans in {mode} mode"
        print(f"OK {tab['name']} / {panel['title']} [selected, multi, all]")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dashboard", type=Path)
    parser.add_argument("--live", action="store_true")
    args = parser.parse_args()
    document = json.loads(args.dashboard.read_text(encoding="utf-8"))
    validate_structure(document)
    print(f"Structure OK: {sum(len(tab['panels']) for tab in document['tabs'])} panels")
    if args.live:
        validate_live(document, os.environ.get("OPENOBSERVE_URL", "http://127.0.0.1:5080"),
                      os.environ["OPENOBSERVE_ORG"], os.environ.get("OPENOBSERVE_USER", "admin@owent.net"),
                      os.environ["OPENOBSERVE_PASSWORD"])


if __name__ == "__main__":
    main()
