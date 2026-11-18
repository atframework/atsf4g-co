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
    "rpc_system": "rpc.db",
    "rpc_method": ".*",
    "span_kind": "SPAN_KIND_SERVER",
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


def substitute(query):
    for name, value in SUBSTITUTIONS.items():
        query = query.replace("${" + name + ":pipe}", value)
    return query


def request_json(url, token, payload=None):
    request = urllib.request.Request(url, data=payload)
    request.add_header("Authorization", "Basic " + token)
    if payload is not None:
        request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def validate_live(document, base_url, organization, email, password):
    token = base64.b64encode(f"{email}:{password}".encode()).decode()
    end = int(time.time() * 1_000_000)
    start = end - 3600 * 1_000_000
    for tab, panel in panels(document):
        for query in panel["queries"]:
            sql = substitute(query["query"])
            if panel["queryType"] == "promql":
                url = f"{base_url}/api/{organization}/prometheus/api/v1/query?" + urllib.parse.urlencode({"query": sql})
                result = request_json(url, token)
                assert result.get("status") == "success", f"{panel['title']}: {result}"
            else:
                body = json.dumps({"query": {"start_time": start, "end_time": end, "sql": sql}}).encode()
                result = request_json(f"{base_url}/api/{organization}/_search?type=traces", token, body)
                assert "hits" in result, f"{panel['title']}: {result}"
        print(f"OK {tab['name']} / {panel['title']}")


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
