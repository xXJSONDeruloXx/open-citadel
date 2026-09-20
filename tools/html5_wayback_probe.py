#!/usr/bin/env python3
"""Probe archived Epic Citadel HTML5 runtime locations without committing assets."""
from __future__ import annotations

import re
import urllib.error
import urllib.parse
import urllib.request

STAMP = "20130504031131"
ROOTS = (
    "http://www.unrealengine.com/html5/",
    "http://unrealengine.com/html5/",
    "https://www.unrealengine.com/html5/",
    "https://unrealengine.com/html5/",
)
FILES = ("UDKGame_Data.js", "UDKGame-Browser-Shipping.js")
UA = "Open-Citadel-Archive-Probe/5.0"


def fetch(url: str, timeout: int = 45):
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.geturl(), response.status, dict(response.headers), response.read()


def archived(original: str, stamp: str = STAMP) -> str:
    quoted = urllib.parse.quote(original, safe=":/?=&%")
    return f"https://web.archive.org/web/{stamp}id_/{quoted}"


def main() -> int:
    landing_url = archived(ROOTS[0])
    final, status, _, body = fetch(landing_url)
    html = body.decode("utf-8", errors="replace")
    print(f"LANDING status={status} final={final} bytes={len(body):,}")
    for filename in FILES:
        pos = html.find(filename)
        if pos >= 0:
            snippet = re.sub(r"\s+", " ", html[max(0, pos-500):pos+500])
            print(f"HTML-CONTEXT {filename}: {snippet}")

    success = False
    for filename in FILES:
        print(f"\n=== {filename} ===")
        for root in ROOTS:
            original = urllib.parse.urljoin(root, filename)
            url = archived(original)
            try:
                final, status, headers, body = fetch(url, 60)
            except Exception as exc:
                print(f"MISS {original}: {type(exc).__name__}: {exc}")
                continue
            print(
                f"HIT {original}: status={status} bytes={len(body):,} "
                f"type={headers.get('Content-Type')} final={final}"
            )
            text = body[:1000000].decode("utf-8", errors="replace")
            refs = sorted(set(re.findall(
                r"""[A-Za-z0-9_./-]+\.(?:data|mem|bin|pak|js|json|ogg|mp3|wav)""",
                text,
                flags=re.I,
            )))
            for ref in refs[:80]:
                print(f"  REF {ref}")
            success = True
            break

    return 0 if success else 2


if __name__ == "__main__":
    raise SystemExit(main())
