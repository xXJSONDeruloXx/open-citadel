#!/usr/bin/env python3
"""Extract deterministic metadata from Epic Citadel's archived Emscripten data loader."""
from __future__ import annotations
import gzip, hashlib, re, urllib.parse, urllib.request

ROOT="http://cdn.unrealengine.com/html5-4c0913f/"
NAME="UDKGame_Data.js"
STAMP="20130503182945"
UA="Open-Citadel-Archive-Probe/11.0"

def fetch():
    original=ROOT+NAME
    url=f"https://web.archive.org/web/{STAMP}id_/"+urllib.parse.quote(original,safe=":/?=&%")
    req=urllib.request.Request(url,headers={"User-Agent":UA})
    with urllib.request.urlopen(req,timeout=45) as r: body=r.read()
    if body.startswith(b"\x1f\x8b"): body=gzip.decompress(body)
    return body

def main():
    data=fetch()
    text=data.decode("utf-8","replace")
    nums=[int(x) for x in re.findall(r"\b\d{7,12}\b",text)]
    print("DATA_JS bytes",len(data),"sha256",hashlib.sha256(data).hexdigest())
    print("candidate_data_size",max(nums) if nums else None)
    for n in sorted(set(nums)):
        if n >= 50_000_000:
            pos=text.find(str(n))
            context=re.sub(r"\s+"," ",text[max(0,pos-180):pos+180])
            print("LARGE_OFFSET",n,"CONTEXT",context)
    for pat in (
        r"""packageName\s*=\s*['"]([^'"]+)""",
        r"""remote_package_size\s*=\s*(\d+)""",
        r"""[^'"\s]{0,100}UDKGame_Data\.data[^'"\s]{0,100}""",
    ):
        print("MATCH",pat,re.findall(pat,text,re.I|re.S)[:100])
    return 0

if __name__=="__main__": raise SystemExit(main())
