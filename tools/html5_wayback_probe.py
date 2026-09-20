#!/usr/bin/env python3
"""Inspect the archived 2013 Epic Citadel HTML5 deployment.

The script fetches archived metadata/runtime JavaScript only for inspection.
It never writes Epic runtime/game payloads into the repository.
"""
from __future__ import annotations

import json
import re
import urllib.parse
import urllib.request
from pathlib import Path

AVAILABLE = "https://archive.org/wayback/available"
WAYBACK_RAW = "https://web.archive.org/web/{timestamp}id_/{original}"
ROOT = "http://www.unrealengine.com/html5/"
STAMP_HINT = "20130503"
OUT = Path("html5-probe.json")
UA = "Open-Citadel-Archive-Probe/4.0"


def get(url: str, timeout: int = 45) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def closest(original: str, timestamp: str) -> dict:
    params = urllib.parse.urlencode({"url": original, "timestamp": timestamp})
    data = json.loads(get(f"{AVAILABLE}?{params}", 15).decode("utf-8"))
    snap = data.get("archived_snapshots", {}).get("closest")
    if not snap or not snap.get("available"):
        raise RuntimeError(f"no snapshot for {original}")
    return snap


def raw_url(original: str, timestamp: str) -> str:
    return WAYBACK_RAW.format(
        timestamp=timestamp,
        original=urllib.parse.quote(original, safe=":/?=&%"),
    )


def asset_names(text: str) -> list[str]:
    patterns = (
        r"""[A-Za-z0-9_./-]+\.(?:data|mem|bin|pak|js|json|ogg|mp3|wav)""",
        r"""(?:memoryInitializer|filePackagePrefixURL|REMOTE_PACKAGE_BASE|PACKAGE_NAME)[^\n;]{0,300}""",
    )
    found: set[str] = set()
    for pattern in patterns:
        found.update(re.findall(pattern, text, flags=re.I))
    return sorted(found)


def main() -> int:
    root_snap = closest(ROOT, STAMP_HINT)
    stamp = root_snap["timestamp"]
    html = get(raw_url(ROOT, stamp)).decode("utf-8", errors="replace")
    page_refs = sorted(set(re.findall(
        r"""(?:src|href)\s*=\s*["']([^"'#]+)""", html, flags=re.I
    )))

    report: dict[str, object] = {
        "root_snapshot": root_snap,
        "landing_bytes": len(html),
        "page_refs": page_refs,
        "runtime": {},
    }

    print(f"ROOT {stamp} bytes={len(html):,}")
    for name in ("UDKGame_Data.js", "UDKGame-Browser-Shipping.js"):
        original = urllib.parse.urljoin(ROOT, name)
        snap = closest(original, stamp)
        body = get(raw_url(original, snap["timestamp"]), 90)
        text = body.decode("utf-8", errors="replace")
        names = asset_names(text)
        print(f"RUNTIME {name} stamp={snap['timestamp']} bytes={len(body):,}")
        for item in names[:150]:
            print(f"REF {name}: {item}")
        report["runtime"][name] = {
            "snapshot": snap,
            "bytes": len(body),
            "refs": names,
        }

    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
