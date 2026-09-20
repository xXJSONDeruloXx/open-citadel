#!/usr/bin/env python3
"""Inspect the archived Epic Citadel HTML5 data loader and engine dependencies."""
from __future__ import annotations

import json
import re
import urllib.parse
import urllib.request

ROOT = "http://www.unrealengine.com/html5/"
STAMP = "20130504031131"
FILES = ("UDKGame_Data.js", "UDKGame-Browser-Shipping.js")
UA = "Open-Citadel-Archive-Probe/8.0"


def archived(original: str) -> str:
    return (
        f"https://web.archive.org/web/{STAMP}id_/"
        + urllib.parse.quote(original, safe=":/?=&%")
    )


def fetch(original: str, timeout: int = 90):
    req = urllib.request.Request(archived(original), headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.geturl(), resp.status, dict(resp.headers), resp.read()


def strings(text: str) -> list[str]:
    values: set[str] = set()
    for quote, value in re.findall(r"""(['"])(.*?)(?<!\\)\1""", text, re.S):
        value = value.strip()
        if 3 <= len(value) <= 500:
            values.add(value)
    return sorted(values)


def main() -> int:
    final, status, _, landing = fetch(ROOT)
    html = landing.decode("utf-8", errors="replace")
    print(f"LANDING status={status} bytes={len(landing):,} final={final}")
    for key in ("DataURL", "EngineURL", "TOTAL_MEMORY", "Commandline", "Module"):
        for match in re.findall(rf"[^\n]{{0,120}}{key}[^\n]{{0,300}}", html):
            print("HTML-CONFIG", re.sub(r"\s+", " ", match))

    for filename in FILES:
        original = urllib.parse.urljoin(ROOT, filename)
        final, status, headers, body = fetch(original)
        text = body.decode("utf-8", errors="replace")
        print(
            f"\nFILE {filename} status={status} bytes={len(body):,} "
            f"type={headers.get('Content-Type')} final={final}"
        )

        if filename == "UDKGame_Data.js":
            print("DATA-LOADER-BEGIN")
            print(text)
            print("DATA-LOADER-END")
        else:
            patterns = (
                r"""[A-Za-z0-9_./:-]+\.(?:data|mem|bin|pak|js|json|ogg|mp3|wav)""",
                r"""memoryInitializer[^;]{0,500}""",
                r"""filePackagePrefixURL[^;]{0,500}""",
                r"""TOTAL_MEMORY[^;]{0,200}""",
            )
            refs: set[str] = set()
            for pattern in patterns:
                refs.update(re.findall(pattern, text, re.I))
            for ref in sorted(refs)[:250]:
                print("ENGINE-REF", ref)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
