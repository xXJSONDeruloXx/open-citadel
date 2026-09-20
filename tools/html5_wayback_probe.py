#!/usr/bin/env python3
"""Probe public web archives for Epic Citadel's 2013 HTML5 deployment.

This tool does not store or redistribute the recovered game. It inventories
captures and fetches only the archived landing HTML to discover deployment
filenames needed for a donor-style local recovery workflow.
"""
from __future__ import annotations

import collections
import json
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

CDX = "https://web.archive.org/cdx/search/cdx"
WAYBACK = "https://web.archive.org/web/{timestamp}id_/{original}"
TARGETS = (
    "http://www.unrealengine.com/html5/",
    "http://unrealengine.com/html5/",
    "http://epic.gm/html5/",
)
OUT = Path("html5-probe.json")
UA = "Open-Citadel-Archive-Probe/1.0 (+https://github.com/xXJSONDeruloXx/open-citadel)"


def get(url: str, timeout: int = 60) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return response.read()


def cdx(url: str, wildcard: bool) -> list[dict[str, str]]:
    query_url = url + ("*" if wildcard else "")
    params = [
        ("url", query_url),
        ("output", "json"),
        ("fl", "timestamp,original,mimetype,statuscode,digest,length"),
        ("filter", "statuscode:200"),
        ("from", "2013"),
        ("to", "2016"),
        ("collapse", "urlkey"),
        ("limit", "10000"),
    ]
    raw = get(CDX + "?" + urllib.parse.urlencode(params)).decode("utf-8")
    rows = json.loads(raw)
    if not rows:
        return []
    header, *data = rows
    return [dict(zip(header, row)) for row in data]


def archived_url(row: dict[str, str]) -> str:
    return WAYBACK.format(
        timestamp=row["timestamp"],
        original=urllib.parse.quote(row["original"], safe=":/?=&%"),
    )


def extract_refs(html: str) -> list[str]:
    refs: set[str] = set()
    for pattern in (
        r"""(?:src|href)\s*=\s*["']([^"'#]+)""",
        r"""["']([^"']+\.(?:js|data|mem|json|css|png|jpg|jpeg|gif|ogg|mp3|wav|bin))(?:\?[^"']*)?["']""",
    ):
        refs.update(re.findall(pattern, html, flags=re.I))
    return sorted(refs)


def main() -> int:
    report: dict[str, object] = {"targets": {}, "selected_root": None, "html_refs": []}
    all_rows: list[dict[str, str]] = []

    for target in TARGETS:
        roots = cdx(target, wildcard=False)
        files = cdx(target, wildcard=True)
        all_rows.extend(files)
        report["targets"][target] = {
            "root_captures": roots,
            "file_count": len(files),
        }
        print(f"{target}: {len(roots)} root captures, {len(files)} unique archived URLs")

    # Prefer a 2013 HTML capture from the canonical unrealengine.com host.
    candidates: list[dict[str, str]] = []
    for target in TARGETS:
        candidates.extend(report["targets"][target]["root_captures"])  # type: ignore[index]
    candidates = [
        row for row in candidates
        if row.get("mimetype", "").startswith("text/html")
    ]
    candidates.sort(
        key=lambda row: (
            not row["original"].startswith("http://www.unrealengine.com"),
            not row["timestamp"].startswith("2013"),
            row["timestamp"],
        )
    )

    if not candidates:
        print("No archived HTML landing page found.", file=sys.stderr)
        OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return 2

    root = candidates[0]
    report["selected_root"] = root
    html_url = archived_url(root)
    html = get(html_url).decode("utf-8", errors="replace")
    refs = extract_refs(html)
    report["html_refs"] = refs

    suffixes = collections.Counter()
    for row in all_rows:
        path = urllib.parse.urlparse(row["original"]).path
        suffix = Path(path).suffix.lower() or "<none>"
        suffixes[suffix] += 1

    print(f"Selected landing page: {root['timestamp']} {root['original']}")
    print(f"Landing HTML: {len(html):,} bytes; {len(refs)} referenced resources")
    print("Referenced resources:")
    for ref in refs:
        print(f"  {ref}")
    print("Archive extension counts:")
    for suffix, count in suffixes.most_common(30):
        print(f"  {suffix:12} {count}")

    report["extension_counts"] = dict(suffixes)
    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
