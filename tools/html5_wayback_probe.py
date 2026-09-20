#!/usr/bin/env python3
"""Recover metadata for Epic Citadel's original 2013 HTML5 deployment.

No Epic game data is committed. The probe asks the Internet Archive for the
closest historical landing page, then checks the resources referenced by it.
"""
from __future__ import annotations

import json
import re
import sys
import urllib.parse
import urllib.request
from pathlib import Path

AVAILABLE = "https://archive.org/wayback/available"
WAYBACK_RAW = "https://web.archive.org/web/{timestamp}id_/{original}"
TARGETS = (
    "http://www.unrealengine.com/html5/",
    "http://unrealengine.com/html5/",
    "http://epic.gm/html5/",
)
OUT = Path("html5-probe.json")
UA = "Open-Citadel-Archive-Probe/2.0 (+https://github.com/xXJSONDeruloXx/open-citadel)"


def get(url: str, timeout: int = 20) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return response.read()


def closest(url: str, timestamp: str = "20130503") -> dict[str, str] | None:
    params = urllib.parse.urlencode({"url": url, "timestamp": timestamp})
    data = json.loads(get(f"{AVAILABLE}?{params}").decode("utf-8"))
    snap = data.get("archived_snapshots", {}).get("closest")
    if not snap or not snap.get("available"):
        return None
    return {
        "timestamp": snap["timestamp"],
        "url": snap["url"],
        "status": snap.get("status", ""),
        "original": url,
    }


def raw_snapshot(snapshot: dict[str, str]) -> str:
    original = urllib.parse.quote(snapshot["original"], safe=":/?=&%")
    return WAYBACK_RAW.format(timestamp=snapshot["timestamp"], original=original)


def extract_refs(html: str) -> list[str]:
    refs: set[str] = set()
    for pattern in (
        r"""(?:src|href)\s*=\s*["']([^"'#]+)""",
        r"""["']([^"']+\.(?:js|data|mem|json|css|png|jpg|jpeg|gif|ogg|mp3|wav|bin))(?:\?[^"']*)?["']""",
    ):
        refs.update(re.findall(pattern, html, flags=re.I))
    return sorted(refs)


def main() -> int:
    roots: list[dict[str, str]] = []
    for target in TARGETS:
        try:
            snap = closest(target)
        except Exception as exc:
            print(f"{target}: archive lookup failed: {exc}", file=sys.stderr)
            continue
        print(f"{target}: {snap or 'no snapshot'}")
        if snap:
            roots.append(snap)

    if not roots:
        OUT.write_text(json.dumps({"roots": []}, indent=2) + "\n", encoding="utf-8")
        return 2

    roots.sort(key=lambda s: (
        not s["original"].startswith("http://www.unrealengine.com"),
        abs(int(s["timestamp"][:8]) - 20130503),
    ))
    root = roots[0]
    print(f"Selected: {root['timestamp']} {root['original']}")

    html = get(raw_snapshot(root)).decode("utf-8", errors="replace")
    refs = extract_refs(html)
    print(f"Landing HTML: {len(html):,} bytes; refs={len(refs)}")

    resources: list[dict[str, object]] = []
    for ref in refs:
        original = urllib.parse.urljoin(root["original"], ref)
        try:
            snap = closest(original, root["timestamp"])
        except Exception as exc:
            snap = None
            print(f"  ERR  {original}: {exc}")
        resources.append({"ref": ref, "original": original, "snapshot": snap})
        print(("  HIT  " if snap else "  MISS ") + original)

    report = {
        "selected_root": root,
        "landing_bytes": len(html),
        "refs": refs,
        "resources": resources,
    }
    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
