#!/usr/bin/env python3
import json
import time
import urllib.parse
import urllib.request

FILES = (
    "http://cdn.unrealengine.com/html5-4c0913f/UDKGame_Data.data",
    "http://cdn.unrealengine.com/html5-4c0913f/UDKGame-Browser-Shipping.js.mem",
)
BASE = "https://archive.org/wayback/available"
UA = "Open-Citadel-Payload-Probe/2.0"

for url in FILES:
    params = urllib.parse.urlencode({"url": url, "timestamp": "20130503"})
    endpoint = BASE + "?" + params
    last = None
    for attempt in range(5):
        try:
            req = urllib.request.Request(endpoint, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=30) as r:
                data = json.loads(r.read().decode("utf-8"))
            snap = data.get("archived_snapshots", {}).get("closest")
            print(url)
            print(json.dumps(snap, sort_keys=True))
            break
        except Exception as exc:
            last = exc
            print(f"retry {attempt + 1}/5: {exc}")
            time.sleep(2 * (attempt + 1))
    else:
        raise SystemExit(f"snapshot lookup failed for {url}: {last}")
