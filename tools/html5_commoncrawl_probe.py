#!/usr/bin/env python3
"""Enumerate Common Crawl's 2013 Epic Citadel HTML5 deployment.

Metadata and small textual samples only; recovered Epic payloads are never
written as repository files or workflow artifacts.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import re
import urllib.parse
import urllib.request
from pathlib import Path

INDEXES = ("CC-MAIN-2013-20", "CC-MAIN-2013-48")
PREFIXES = (
    "www.unrealengine.com/html5/",
    "unrealengine.com/html5/",
    "cdn.unrealengine.com/html5-4c0913f/",
    "cdn.unrealengine.com/html5-",
)
OUT = Path("html5-commoncrawl-probe.json")
UA = "Open-Citadel-CommonCrawl-Probe/2.0"


def get(url: str, *, headers: dict[str, str] | None = None, timeout: int = 45) -> bytes:
    h = {"User-Agent": UA}
    if headers:
        h.update(headers)
    req = urllib.request.Request(url, headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def query(index: str, target: str, *, prefix: bool = False) -> list[dict]:
    params = {"url": target, "output": "json"}
    if prefix:
        params["matchType"] = "prefix"
    url = f"https://index.commoncrawl.org/{index}-index?" + urllib.parse.urlencode(params)
    try:
        body = get(url, timeout=45).decode("utf-8", errors="replace")
    except Exception as exc:
        print(f"QUERY-ERR {index} {target}: {type(exc).__name__}: {exc}")
        return []
    rows = []
    for line in body.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            rows.append(json.loads(line))
        except json.JSONDecodeError:
            print(f"BAD-JSON {index} {target}: {line[:300]}")
    return rows


def recover_payload(row: dict) -> bytes:
    filename = row.get("filename")
    offset = int(row.get("offset", "0"))
    length = int(row.get("length", "0"))
    if not filename or length <= 0:
        return b""
    url = f"https://data.commoncrawl.org/{filename}"
    raw = get(
        url,
        headers={"Range": f"bytes={offset}-{offset + length - 1}"},
        timeout=60,
    )
    warc = gzip.decompress(raw)
    first = warc.find(b"\r\n\r\n")
    second = warc.find(b"\r\n\r\n", first + 4) if first >= 0 else -1
    return warc[second + 4 :] if second >= 0 else b""


def summarize(row: dict, *, inspect_text: bool = False) -> dict:
    item = {
        k: row.get(k)
        for k in (
            "url", "timestamp", "status", "mime", "digest",
            "length", "offset", "filename"
        )
    }
    if inspect_text:
        try:
            payload = recover_payload(row)
            text = payload.decode("utf-8", errors="replace")
            refs = sorted(set(
                re.findall(
                    r"""(?:src|href)=["']([^"'#]+)|https?://[^"'<>\s]+""",
                    text,
                    flags=re.I,
                )
            ))
            # re.findall with an alternation/group can return empty group values;
            # also scan known UE3/Emscripten names directly.
            names = sorted(set(re.findall(
                r"""[A-Za-z0-9_./:-]+\.(?:js|data|mem|css|png|jpg|jpeg|gif|ogg|mp3|wav|bin)(?:\?[^"'<>\s]*)?""",
                text,
                flags=re.I,
            )))
            item["payload_bytes"] = len(payload)
            item["payload_sha256"] = hashlib.sha256(payload).hexdigest()
            item["html_asset_names"] = names
            item["html_excerpt"] = text[:2000]
        except Exception as exc:
            item["inspect_error"] = f"{type(exc).__name__}: {exc}"
    return item


def main() -> int:
    report: dict[str, object] = {"indexes": {}}
    for index in INDEXES:
        idx: dict[str, object] = {}
        for prefix in PREFIXES:
            rows = query(index, prefix, prefix=True)
            # Deduplicate captures by URL, retaining the earliest row for each URL.
            by_url: dict[str, dict] = {}
            for row in rows:
                url = row.get("url", "")
                if url and url not in by_url:
                    by_url[url] = row
            urls = sorted(by_url)
            print(f"PREFIX {index} {prefix}: {len(rows)} captures / {len(urls)} unique URLs")
            compact = []
            for url in urls[:500]:
                row = by_url[url]
                inspect = row.get("mime") == "text/html" and url.rstrip("/") in {
                    "http://www.unrealengine.com/html5",
                    "https://www.unrealengine.com/html5",
                    "http://unrealengine.com/html5",
                    "https://unrealengine.com/html5",
                }
                item = summarize(row, inspect_text=inspect)
                compact.append(item)
                print("  URL", url)
                if inspect and item.get("html_asset_names"):
                    for name in item["html_asset_names"]:
                        print("    PAGE-REF", name)
            idx[prefix] = compact
        report["indexes"][index] = idx

    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
