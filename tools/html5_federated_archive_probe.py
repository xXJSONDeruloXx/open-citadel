#!/usr/bin/env python3
"""Probe independent web archives for the missing Epic Citadel HTML5 payloads."""
from __future__ import annotations
import json, urllib.parse, urllib.request

TARGETS=(
 "http://cdn.unrealengine.com/html5-4c0913f/UDKGame_Data.data",
 "http://cdn.unrealengine.com/html5-4c0913f/UDKGame-Browser-Shipping.js.mem",
 "http://cdn.unrealengine.com/html5-4c0913f/UDKGame_Data.js",
 "http://cdn.unrealengine.com/html5-4c0913f/UDKGame-Browser-Shipping.js",
)
UA="Open-Citadel-Federated-Archive-Probe/1.0"

def get(url,timeout=35):
    req=urllib.request.Request(url,headers={"User-Agent":UA,"Accept":"*/*"})
    with urllib.request.urlopen(req,timeout=timeout) as r:
        return r.status,r.geturl(),dict(r.headers),r.read()

def probe_arquivo(url):
    endpoints=[
      "https://arquivo.pt/wayback/cdx?"+urllib.parse.urlencode({"url":url,"output":"json"}),
      "https://arquivo.pt/wayback/timemap/json/"+url,
    ]
    out=[]
    for ep in endpoints:
      try:
        st,final,h,b=get(ep)
        out.append({"endpoint":ep,"status":st,"final":final,"bytes":len(b),"body":b[:20000].decode("utf-8","replace")})
      except Exception as e:
        out.append({"endpoint":ep,"error":f"{type(e).__name__}: {e}"})
    return out

def probe_memento(url):
    # LANL's Time Travel aggregator historically federated multiple archives.
    eps=[
      "https://timetravel.mementoweb.org/api/json/20130503182945/"+url,
      "http://timetravel.mementoweb.org/api/json/20130503182945/"+url,
    ]
    out=[]
    for ep in eps:
      try:
        st,final,h,b=get(ep)
        out.append({"endpoint":ep,"status":st,"final":final,"bytes":len(b),"body":b[:20000].decode("utf-8","replace")})
      except Exception as e:
        out.append({"endpoint":ep,"error":f"{type(e).__name__}: {e}"})
    return out

def probe_internet_archive_catalog():
    queries=(
      '"UDKGame_Data.data"',
      '"UDKGame-Browser-Shipping.js.mem"',
      '"UDKGame-Browser-Shipping.js"',
      '"Epic Citadel" AND mediatype:software',
      '"Epic Citadel" AND mediatype:web',
    )
    out={}
    for q in queries:
      params=[
        ("q",q),("fl[]","identifier"),("fl[]","title"),("fl[]","description"),
        ("fl[]","mediatype"),("fl[]","date"),("rows","100"),("output","json")
      ]
      ep="https://archive.org/advancedsearch.php?"+urllib.parse.urlencode(params)
      try:
        st,final,h,b=get(ep,45)
        obj=json.loads(b.decode("utf-8","replace"))
        docs=obj.get("response",{}).get("docs",[])
        out[q]=docs
        print("IA-QUERY",q,"RESULTS",len(docs))
        for d in docs[:100]: print(" IA-HIT",json.dumps(d,sort_keys=True))
      except Exception as e:
        out[q]={"error":f"{type(e).__name__}: {e}"}
        print("IA-ERR",q,out[q])
    return out

def main():
    report={"internet_archive_catalog":probe_internet_archive_catalog()}
    for u in TARGETS:
      print("\nTARGET",u)
      a=probe_arquivo(u)
      m=probe_memento(u)
      for row in a+m:
        print(json.dumps(row,sort_keys=True))
      report[u]={"arquivo":a,"memento":m}
    open("html5-federated-archive-probe.json","w").write(json.dumps(report,indent=2)+"\n")
    return 0

if __name__=="__main__": raise SystemExit(main())
