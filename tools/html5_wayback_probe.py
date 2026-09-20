#!/usr/bin/env python3
"""Locate Epic Citadel's archived HTML5 runtime using the Wayback CDX index."""
from __future__ import annotations

import json
import re
import urllib.parse
import urllib.request

CDX = "https://web.archive.org/cdx/search/cdx"
WAYBACK = "https://web.archive.org/web/{stamp}id_/{original}"
ROOT = "http://www.unrealengine.com/html5/"
ROOT_STAMP = "20130504031131"
FILES = ("UDKGame_Data.js", "UDKGame-Browser-Shipping.js")
UA = "Open-Citadel-Archive-Probe/6.0"


def get(url: str, timeout: int = 60) -> tuple[str, int, dict, bytes]:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.geturl(), resp.status, dict(resp.headers), resp.read()


def archived(original: str, stamp: str) -> str:
    return WAYBACK.format(
        stamp=stamp,
        original=urllib.parse.quote(original, safe=":/?=&%"),
    )


def cdx(original: str) -> list[dict[str, str]]:
    params = [
        ("url", original),
        ("output", "json"),
        ("fl", "timestamp,original,mimetype,statuscode,digest,length"),
        ("filter", "statuscode:200"),
        ("from", "2013"),
        ("to", "2016"),
        ("limit", "50"),
    ]
    _, _, _, body = get(CDX + "?" + urllib.parse.urlencode(params), 30)
    rows = json.loads(body.decode("utf-8"))
    if not rows:
        return []
    header, *items = rows
    return [dict(zip(header, item)) for item in items]


def main() -> int:
    _, _, _, landing = get(archived(ROOT, ROOT_STAMP))
    html = landing.decode("utf-8", errors="replace")
    print(f"LANDING bytes={len(landing):,}")
    for filename in FILES:
        pos = html.find(filename)
        if pos >= 0:
            print("HTML", re.sub(r"\s+", " ", html[max(0, pos-300):pos+300]))

    hits = 0
    for filename in FILES:
        print(f"\n=== {filename} ===")
        variants = (
            f"http://www.unrealengine.com/html5/{filename}",
            f"http://unrealengine.com/html5/{filename}",
            f"https://www.unrealengine.com/html5/{filename}",
            f"https://unrealengine.com/html5/{filename}",
        )
        rows: list[dict[str, str]] = []
        for original in variants:
            try:
                found = cdx(original)
            except Exception as exc:
                print(f"CDX-ERR {original}: {exc}")
                continue
            print(f"CDX {original}: {len(found)} captures")
            for row in found[:20]:
                print("  CAPTURE", json.dumps(row, sort_keys=True))
            rows.extend(found)

        seen: set[tuple[str, str]] = set()
        for row in rows:
            key = (row["timestamp"], row["original"])
            if key in seen:
                continue
            seen.add(key)
            try:
                final, status, headers, body = get(
                    archived(row["original"], row["timestamp"]), 90
                )
            except Exception as exc:
                print(f"FETCH-ERR {row['timestamp']} {row['original']}: {exc}")
                continue
            print(
                f"FETCH {row['timestamp']} {row['original']}: status={status} "
                f"bytes={len(body):,} type={headers.get('Content-Type')} final={final}"
            )
            sample = body[:2_000_000].decode("utf-8", errors="replace")
            refs = sorted(set(re.findall(
                r"""[A-Za-z0-9_./-]+\.(?:data|mem|bin|pak|js|json|ogg|mp3|wav)""",
                sample,
                re.I,
            )))
            for ref in refs[:120]:
                print("  REF", ref)
            hits += 1
            break

    return 0 if hits else 2


if __name__ == "__main__":
    raise SystemExit(main())
