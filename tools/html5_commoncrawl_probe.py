#!/usr/bin/env python3
"""Probe Common Crawl's 2013 index for Epic Citadel HTML5 deployment objects.

This records archive metadata only. It does not persist recovered Epic payloads.
"""
from __future__ import annotations

import gzip
import hashlib
import json
import urllib.parse
import urllib.request
from pathlib import Path

INDEXES = ("CC-MAIN-2013-20", "CC-MAIN-2013-48")
TARGETS = (
    "cdn.unrealengine.com/html5-4c0913f/UDKGame_Data.data",
    "cdn.unrealengine.com/html5-4c0913f/UDKGame-Browser-Shipping.js.mem",
    "www.unrealengine.com/html5/UDKGame_Data.js",
    "www.unrealengine.com/html5/UDKGame-Browser-Shipping.js",
    "www.unrealengine.com/html5/",
)
OUT = Path("html5-commoncrawl-probe.json")
UA = "Open-Citadel-CommonCrawl-Probe/1.0"


def get(url: str, *, headers: dict[str, str] | None = None, timeout: int = 45) -> bytes:
    h = {"User-Agent": UA}
    if headers:
        h.update(headers)
    req = urllib.request.Request(url, headers=h)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def query(index: str, target: str) -> list[dict]:
    params = urllib.parse.urlencode({"url": target, "output": "json"})
    url = f"https://index.commoncrawl.org/{index}-index?{params}"
    try:
        body = get(url, timeout=30).decode("utf-8", errors="replace")
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


def sample_record(row: dict, limit: int = 65536) -> dict:
    filename = row.get("filename")
    offset = int(row.get("offset", "0"))
    length = int(row.get("length", "0"))
    if not filename or length <= 0:
        return {"error": "missing WARC location"}

    url = f"https://data.commoncrawl.org/{filename}"
    headers = {"Range": f"bytes={offset}-{offset + length - 1}"}
    try:
        raw = get(url, headers=headers, timeout=60)
        warc = gzip.decompress(raw)
    except Exception as exc:
        return {"error": f"{type(exc).__name__}: {exc}"}

    sep = warc.find(b"\r\n\r\n")
    http_start = sep + 4 if sep >= 0 else 0
    http_sep = warc.find(b"\r\n\r\n", http_start)
    payload = warc[http_sep + 4 :] if http_sep >= 0 else b""
    return {
        "warc_bytes": len(raw),
        "decoded_record_bytes": len(warc),
        "payload_bytes": len(payload),
        "payload_sha256": hashlib.sha256(payload).hexdigest(),
        "payload_prefix_hex": payload[:32].hex(),
        "payload_sample_text": payload[:limit].decode("utf-8", errors="replace")[:500],
    }


def main() -> int:
    report: dict[str, object] = {"indexes": {}}
    hits = 0
    for index in INDEXES:
        idx: dict[str, object] = {}
        for target in TARGETS:
            rows = query(index, target)
            print(f"{index} {target}: {len(rows)} record(s)")
            compact = []
            for row in rows[:20]:
                item = {
                    k: row.get(k)
                    for k in (
                        "url", "timestamp", "status", "mime", "digest",
                        "length", "offset", "filename"
                    )
                }
                print("  HIT", json.dumps(item, sort_keys=True))
                # Inspect a small recovered response to verify the record really
                # contains the expected object, without saving it as an artifact.
                item["sample"] = sample_record(row)
                compact.append(item)
                hits += 1
            idx[target] = compact
        report["indexes"][index] = idx

    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"TOTAL-HITS {hits}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
