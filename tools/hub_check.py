import json, re, urllib.request, urllib.parse, os
def get(u):
    try:
        with urllib.request.urlopen(urllib.request.Request(u, headers={"User-Agent": "Mozilla/5.0 FM-9"}), timeout=40) as r: return r.status, r.read()
    except urllib.error.HTTPError as e: return e.code, b""
    except Exception as e: return 0, str(e).encode()
os.makedirs("hub_out", exist_ok=True)
links = set(); pages = ["https://thefirmwarehub.com/"]
seen = set()
while pages and len(seen) < 60:
    p = pages.pop(0)
    if p in seen: continue
    seen.add(p)
    st, d = get(p); t = d.decode("utf-8", "replace")
    open("hub_out/page_%d.html" % len(seen), "w").write(p + "\n" + t[:200000])
    for m in re.findall(r"""["'(]([^"'()\s]+?\.(?:fwsc|json))["')]""", t): links.add(urllib.parse.urljoin(p, m))
    for m in re.findall(r"""(?:href|src)=["']([^"'#]+)["']""", t):
        u = urllib.parse.urljoin(p, m)
        if u.startswith("https://thefirmwarehub.com") and not re.search(r"\.(png|jpg|svg|css|ico|woff2?|fwsc)$", u): pages.append(u)
for j in [l for l in links if l.endswith(".json")]:
    st, d = get(j); open("hub_out/" + re.sub(r"\W+", "_", j)[-80:], "wb").write(d)
    for m in re.findall(rb'"([^"]+?\.fwsc)"', d): links.add(urllib.parse.urljoin(j, m.decode()))
cat = json.load(open("catalog/firmwares.json"))
rows = []
for e in cat["firmwares"]:
    if e.get("compatible") and "thefirmwarehub.com" in (e.get("pkg") or ""):
        st, _ = get(e["pkg"]) if False else (None, None)
        rows.append(e["pkg"])
status = {u: get(u)[0] for u in rows}
json.dump({"links": sorted(links), "status": status, "pages": sorted(seen)}, open("hub_out/hub.json", "w"), indent=1)
print(len(links), "links", sum(1 for v in status.values() if v == 200), "/", len(status), "ok")
