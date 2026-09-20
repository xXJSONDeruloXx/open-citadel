#!/usr/bin/env python3
"""Recover the Common Crawl Epic Citadel HTML page and probe every asset URL it names."""
from __future__ import annotations
import gzip, json, re, urllib.parse, urllib.request
from pathlib import Path

INDEX = "CC-MAIN-2013-48"
ROOT = "http://www.unrealengine.com/html5/"
ROOT_ROW = {
    "filename": "crawl-data/CC-MAIN-2013-48/segments/1386163046758/warc/CC-MAIN-20131204131726-00032-ip-10-33-133-15.ec2.internal.warc.gz",
    "offset": "609872226",
    "length": "4763",
}
UA="Open-Citadel-CommonCrawl-Probe/3.0"
OUT=Path("html5-commoncrawl-probe.json")

def get(url, headers=None, timeout=45):
    h={"User-Agent":UA}; h.update(headers or {})
    req=urllib.request.Request(url,headers=h)
    with urllib.request.urlopen(req,timeout=timeout) as r: return r.read()

def payload(row):
    off=int(row["offset"]); ln=int(row["length"])
    raw=get("https://data.commoncrawl.org/"+row["filename"],
            {"Range":f"bytes={off}-{off+ln-1}"},60)
    warc=gzip.decompress(raw)
    a=warc.find(b"\r\n\r\n"); b=warc.find(b"\r\n\r\n",a+4)
    return warc[b+4:] if b>=0 else b""

def query(url):
    p=urllib.parse.urlencode({"url":url,"output":"json"})
    try: body=get(f"https://index.commoncrawl.org/{INDEX}-index?{p}",timeout=25).decode("utf-8","replace")
    except Exception as e:
        return {"error":f"{type(e).__name__}: {e}","rows":[]}
    rows=[]
    for line in body.splitlines():
        try: rows.append(json.loads(line))
        except: pass
    return {"rows":rows}

def main():
    html=payload(ROOT_ROW).decode("utf-8","replace")
    print("HTML-BEGIN")
    print(html)
    print("HTML-END")
    refs=set()
    for m in re.finditer(r"""(?:src|href)\s*=\s*["']([^"'#]+)""",html,re.I):
        refs.add(m.group(1))
    for m in re.finditer(r"""https?://[^"'<>\s)]+""",html,re.I):
        refs.add(m.group(0))
    for m in re.finditer(r"""[A-Za-z0-9_./:-]+\.(?:js|data|mem|css|png|jpg|jpeg|gif|ogg|mp3|wav|bin)(?:\?[^"'<>\s]*)?""",html,re.I):
        refs.add(m.group(0))
    urls=[]
    for ref in sorted(refs):
        if ref.startswith(("javascript:","mailto:","#")): continue
        url=urllib.parse.urljoin(ROOT,ref)
        urls.append(url)
        print("REF",url)
    results={}
    for url in urls:
        q=query(url)
        rows=q.get("rows",[])
        print("QUERY",url,"ROWS",len(rows),q.get("error",""))
        if rows:
            for row in rows[:5]:
                print(" HIT",json.dumps({k:row.get(k) for k in ("url","timestamp","status","mime","digest","length","offset","filename")},sort_keys=True))
        results[url]=q
    OUT.write_text(json.dumps({"refs":urls,"queries":results},indent=2)+"\n")
    return 0

if __name__=="__main__": raise SystemExit(main())
