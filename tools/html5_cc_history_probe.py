#!/usr/bin/env python3
"""Search all relevant Common Crawl indexes for Epic Citadel HTML5 runtime assets."""
from __future__ import annotations
import concurrent.futures, json, urllib.parse, urllib.request

UA="Open-Citadel-CC-History/1.0"
BASE_NAMES=(
 "UDKGame_Data.data",
 "UDKGame-Browser-Shipping.js.mem",
 "UDKGame_Data.js",
 "UDKGame-Browser-Shipping.js",
)
HOST_PATHS=(
 "cdn.unrealengine.com/html5-4c0913f/",
 "www.unrealengine.com/html5/",
 "unrealengine.com/html5/",
)

def get(url,timeout=15):
    req=urllib.request.Request(url,headers={"User-Agent":UA})
    with urllib.request.urlopen(req,timeout=timeout) as r:
        return r.read()

def indexes():
    rows=json.loads(get("https://index.commoncrawl.org/collinfo.json",20))
    out=[]
    for row in rows:
        ident=row.get("id","")
        # CC indexes are reverse chronological; scan through the period when
        # Epic's HTML5 demo was public and some time after removal.
        if any(str(y) in ident for y in (2013,2014,2015,2016)):
            out.append((ident,row["cdx-api"]))
    return out

def one(args):
    ident, api, url=args
    q=api+"?"+urllib.parse.urlencode({"url":url,"output":"json"})
    try:
        body=get(q,12).decode("utf-8","replace")
    except Exception as e:
        return ident,url,[],f"{type(e).__name__}: {e}"
    rows=[]
    for line in body.splitlines():
        try: rows.append(json.loads(line))
        except Exception: pass
    return ident,url,rows,None

def main():
    idx=indexes()
    urls=[]
    for hp in HOST_PATHS:
        for name in BASE_NAMES:
            for scheme in ("http://","https://"):
                urls.append(scheme+hp+name)
    jobs=[(ident,api,url) for ident,api in idx for url in urls]
    print("INDEXES",len(idx),"URLS",len(urls),"QUERIES",len(jobs))
    hits=[]
    errors=0
    with concurrent.futures.ThreadPoolExecutor(max_workers=24) as ex:
        for ident,url,rows,err in ex.map(one,jobs):
            if err:
                errors+=1
                continue
            for row in rows:
                item={k:row.get(k) for k in ("url","timestamp","status","mime","digest","length","offset","filename")}
                hits.append((ident,url,item))
                print("HIT",ident,url,json.dumps(item,sort_keys=True))
    print("TOTAL_HITS",len(hits),"QUERY_ERRORS",errors)
    open("html5-cc-history.json","w").write(json.dumps(
       [{"index":i,"query":u,**r} for i,u,r in hits],indent=2
    )+"\n")
    return 0

if __name__=="__main__": raise SystemExit(main())
