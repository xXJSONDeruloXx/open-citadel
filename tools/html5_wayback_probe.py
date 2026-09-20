#!/usr/bin/env python3
"""Extract deterministic metadata from the archived Epic Citadel Emscripten loaders."""
from __future__ import annotations
import gzip, hashlib, re, urllib.parse, urllib.request

ROOT="http://cdn.unrealengine.com/html5-4c0913f/"
STAMP={"UDKGame_Data.js":"20130503182945","UDKGame-Browser-Shipping.js":"20130503182946"}
UA="Open-Citadel-Archive-Probe/10.0"

def fetch(name):
    original=ROOT+name
    url=f"https://web.archive.org/web/{STAMP[name]}id_/"+urllib.parse.quote(original,safe=":/?=&%")
    req=urllib.request.Request(url,headers={"User-Agent":UA})
    with urllib.request.urlopen(req,timeout=180) as r: body=r.read()
    if body.startswith(b"\x1f\x8b"): body=gzip.decompress(body)
    return body

def hits(text, patterns):
    for label,pat in patterns:
        vals=re.findall(pat,text,re.I|re.S)
        print(label, vals[:50])

def main():
    data=fetch("UDKGame_Data.js")
    text=data.decode("utf-8","replace")
    print("DATA_JS bytes",len(data),"sha256",hashlib.sha256(data).hexdigest())
    hits(text,[
      ("packageName",r"""packageName\s*=\s*['"]([^'"]+)"""),
      ("remote_package_size",r"""remote_package_size\s*=\s*(\d+)"""),
      ("package_size",r"""package(?:Data)?Size\s*=\s*(\d+)"""),
      ("all_sizes",r"""\b(\d{7,12})\b"""),
      ("data_urls",r"""[^'"\s]{0,100}UDKGame_Data\.data[^'"\s]{0,100}"""),
    ])
    engine=fetch("UDKGame-Browser-Shipping.js")
    et=engine.decode("utf-8","replace")
    print("ENGINE_JS bytes",len(engine),"sha256",hashlib.sha256(engine).hexdigest())
    hits(et,[
      ("TOTAL_MEMORY",r"""TOTAL_MEMORY\s*[=:]\s*(\d+)"""),
      ("mem_refs",r"""[^'"\s]{0,100}UDKGame-Browser-Shipping\.js\.mem[^'"\s]{0,100}"""),
      ("memory_init_calls",r"""MemoryInitializer\([^\n]{0,300}"""),
    ])
    return 0
if __name__=="__main__": raise SystemExit(main())
