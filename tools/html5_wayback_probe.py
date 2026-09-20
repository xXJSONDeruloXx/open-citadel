#!/usr/bin/env python3
"""Find the archived 2013 Epic Citadel HTML5 landing page and its asset names."""
from __future__ import annotations

import json
import re
import urllib.parse
import urllib.request
from pathlib import Path

AVAILABLE = "https://archive.org/wayback/available"
WAYBACK_RAW = "https://web.archive.org/web/{timestamp}id_/{original}"
TARGET = "http://www.unrealengine.com/html5/"
OUT = Path("html5-probe.json")
UA = "Open-Citadel-Archive-Probe/3.0"


def get(url: str, timeout: int = 12) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def main() -> int:
    params = urllib.parse.urlencode({"url": TARGET, "timestamp": "20130503"})
    data = json.loads(get(f"{AVAILABLE}?{params}").decode("utf-8"))
    snap = data.get("archived_snapshots", {}).get("closest")
    if not snap or not snap.get("available"):
        print("No archived landing page found")
        return 2

    timestamp = snap["timestamp"]
    raw = WAYBACK_RAW.format(
        timestamp=timestamp,
        original=urllib.parse.quote(TARGET, safe=":/?=&%"),
    )
    html = get(raw).decode("utf-8", errors="replace")

    refs: set[str] = set()
    refs.update(re.findall(r"""(?:src|href)\s*=\s*["']([^"'#]+)""", html, flags=re.I))
    refs.update(re.findall(
        r"""["']([^"']+\.(?:js|data|mem|json|css|png|jpg|jpeg|gif|ogg|mp3|wav|bin))(?:\?[^"']*)?["']""",
        html,
        flags=re.I,
    ))

    report = {
        "target": TARGET,
        "snapshot": snap,
        "raw_snapshot": raw,
        "landing_bytes": len(html),
        "refs": sorted(refs),
    }
    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"Snapshot: {timestamp} {snap['url']}")
    print(f"Landing HTML: {len(html):,} bytes")
    for ref in sorted(refs):
        print(f"ASSET {ref}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
