#!/usr/bin/env python3
"""Recover Epic Citadel's original 2013 HTML5 deployment from a web archive.

Recovered Epic files are written only to a caller-selected local directory.
Nothing from the original deployment is intended to be committed to this repo.
"""
from __future__ import annotations

import argparse
import gzip
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = "http://www.unrealengine.com/html5/"
STAMP = "20130504031131"
CDN_ROOT = "http://cdn.unrealengine.com/html5-4c0913f/"
UA = "Open-Citadel-HTML5-Recovery/1.1"
CORE = (
    "UDKGame_Data.js",
    "UDKGame_Data.data",
    "UDKGame-Browser-Shipping.js",
    "UDKGame-Browser-Shipping.js.mem",
)

# The landing page was archived a few hours after Epic's CDN deployment.
# These timestamps bracket the known captures for the two loader scripts.
CDN_STAMPS = {
    "UDKGame_Data.js": "20130503182945",
    "UDKGame_Data.data": "20130503182945",
    "UDKGame-Browser-Shipping.js": "20130503182946",
    "UDKGame-Browser-Shipping.js.mem": "20130503182946",
}


def archived(original: str, stamp: str) -> str:
    return f"https://web.archive.org/web/{stamp}id_/" + urllib.parse.quote(
        original, safe=":/?=&%"
    )


def _decode_archived_body(body: bytes) -> bytes:
    # Raw Wayback replay can preserve an original gzip entity while omitting
    # Content-Encoding metadata needed when the files are served locally.
    if body.startswith(b"\x1f\x8b"):
        return gzip.decompress(body)
    return body


def fetch(original: str, timeout: int = 300) -> tuple[bytes, str]:
    parsed = urllib.parse.urlparse(original)
    relative = parsed.path.removeprefix("/html5/") if original.startswith(ROOT) else ""

    candidates: list[tuple[str, str]] = [(original, STAMP)]
    if relative:
        cdn_original = urllib.parse.urljoin(CDN_ROOT, relative)
        candidates.insert(0, (cdn_original, CDN_STAMPS.get(relative, "20130503182946")))

    last_error: Exception | None = None
    for candidate, stamp in candidates:
        url = archived(candidate, stamp)
        for attempt in range(3):
            try:
                request = urllib.request.Request(url, headers={"User-Agent": UA})
                with urllib.request.urlopen(request, timeout=timeout) as response:
                    body = response.read()
                    final = response.geturl()
                return _decode_archived_body(body), final
            except Exception as exc:
                last_error = exc
                print(
                    f"archive fetch retry {attempt + 1}/3 for {candidate}: {exc}",
                    file=sys.stderr,
                )
                time.sleep(2 * (attempt + 1))

    assert last_error is not None
    raise last_error


def safe_relative(ref: str) -> Path | None:
    parsed = urllib.parse.urlparse(ref)
    if parsed.scheme or parsed.netloc:
        return None
    path = parsed.path
    if path.startswith("/"):
        return None
    if not path or path.endswith("/"):
        return None
    candidate = Path(path)
    if any(part in ("", ".", "..") for part in candidate.parts):
        return None
    return candidate


def page_assets(html: str) -> set[str]:
    refs: set[str] = set(CORE)
    refs.update(re.findall(r"""(?:src|href)\s*=\s*["']([^"'#]+)""", html, re.I))
    # The original page assigns the engine/data URLs in JavaScript rather than
    # using static script tags.
    refs.update(re.findall(r"""(?:DataURL|EngineURL)\s*=\s*["']([^"']+)""", html))
    return refs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", nargs="?", default="html5-donor")
    args = parser.parse_args()

    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)

    # Keep the page itself on the historical unrealengine.com capture.
    request = urllib.request.Request(archived(ROOT, STAMP), headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=120) as response:
        html_bytes = _decode_archived_body(response.read())
        final = response.geturl()
    html = html_bytes.decode("utf-8", errors="replace")
    (out / "index.html").write_text(html, encoding="utf-8")
    print(f"recovered index.html ({len(html_bytes):,} bytes) <- {final}")

    failed: list[str] = []
    recovered: list[Path] = []
    for ref in sorted(page_assets(html)):
        rel = safe_relative(ref)
        if rel is None:
            continue
        original = urllib.parse.urljoin(ROOT, ref)
        try:
            body, final = fetch(original)
        except Exception as exc:
            # Cosmetic assets are best-effort, but the UE3 runtime is required.
            if ref in CORE:
                raise
            failed.append(ref)
            print(f"warning: failed optional asset {ref}: {exc}", file=sys.stderr)
            continue
        target = out / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
        recovered.append(rel)
        print(f"recovered {rel} ({len(body):,} bytes) <- {final}")

    for required in CORE:
        if not (out / required).is_file():
            raise SystemExit(f"required runtime asset missing: {required}")

    print(f"recovered {len(recovered) + 1} files; optional failures={len(failed)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
