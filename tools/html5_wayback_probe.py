#!/usr/bin/env python3
"""Inspect archived Epic Citadel HTML5 assets, normalizing archived gzip bodies."""
from __future__ import annotations

import gzip
import re
import urllib.parse
import urllib.request

ROOT = "http://www.unrealengine.com/html5/"
STAMP = "20130504031131"
FILES = ("UDKGame_Data.js", "UDKGame-Browser-Shipping.js")
UA = "Open-Citadel-Archive-Probe/9.0"


def archived(original: str) -> str:
    return f"https://web.archive.org/web/{STAMP}id_/" + urllib.parse.quote(
        original, safe=":/?=&%"
    )


def fetch(original: str, timeout: int = 120):
    req = urllib.request.Request(archived(original), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        body = resp.read()
        final = resp.geturl()
        headers = dict(resp.headers)
    compressed = body.startswith(b"\x1f\x8b")
    if compressed:
        body = gzip.decompress(body)
    return final, headers, body, compressed


def refs(text: str) -> list[str]:
    found: set[str] = set()
    patterns = (
        r"""[A-Za-z0-9_./:+?=&%-]+\.(?:data|mem|bin|pak|js|json|ogg|mp3|wav)(?:\?[^"'\s;)]*)?""",
        r"""https?://[^"'\s)]+""",
        r"""(?:packageName|remote_package_size|memoryInitializer|filePackagePrefixURL)[^;\n]{0,500}""",
        r"""(?:GET|open)\s*\([^\n]{0,500}""",
    )
    for pattern in patterns:
        found.update(re.findall(pattern, text, re.I))
    return sorted(found)


def main() -> int:
    final, _, landing, gz = fetch(ROOT)
    html = landing.decode("utf-8", errors="replace")
    print(f"LANDING bytes={len(landing):,} gzip={gz} final={final}")
    for filename in FILES:
        original = urllib.parse.urljoin(ROOT, filename)
        final, headers, body, gz = fetch(original)
        text = body.decode("utf-8", errors="replace")
        print(
            f"\nFILE {filename} raw-gzip={gz} decoded-bytes={len(body):,} "
            f"type={headers.get('Content-Type')} final={final}"
        )
        for ref in refs(text)[:500]:
            print("REF", ref)
        if filename == "UDKGame_Data.js":
            literals = sorted(set(
                value for _, value in re.findall(r"""(['"])(.*?)(?<!\\)\1""", text, re.S)
                if 2 < len(value) < 500
            ))
            for value in literals[:500]:
                print("LITERAL", value.replace("\n", "\\n"))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
