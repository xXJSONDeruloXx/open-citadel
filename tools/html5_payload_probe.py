#!/usr/bin/env python3
import json
import urllib.parse
import urllib.request

FILES = (
    "http://cdn.unrealengine.com/html5-4c0913f/UDKGame_Data.data",
    "http://cdn.unrealengine.com/html5-4c0913f/UDKGame-Browser-Shipping.js.mem",
)
BASE = "https://web.archive.org/cdx/search/cdx"

for url in FILES:
    params = [
        ("url", url),
        ("output", "json"),
        ("fl", "timestamp,original,statuscode,mimetype,length,digest"),
        ("filter", "statuscode:200"),
        ("from", "2013"),
        ("to", "2014"),
        ("limit", "20"),
    ]
    req = urllib.request.Request(
        BASE + "?" + urllib.parse.urlencode(params),
        headers={"User-Agent": "Open-Citadel-Payload-Probe/1.0"},
    )
    with urllib.request.urlopen(req, timeout=45) as r:
        rows = json.loads(r.read().decode("utf-8"))
    print("\n" + url)
    if not rows:
        print("NO CAPTURES")
        continue
    header, *data = rows
    for row in data:
        print(json.dumps(dict(zip(header, row)), sort_keys=True))
