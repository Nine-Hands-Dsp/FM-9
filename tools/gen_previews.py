#!/usr/bin/env python3
"""FM-9 - ecrans des firmwares du catalogue (vignettes de la galerie).

Pour chaque firmware compatible sans vignette dans catalog/previews/ : telecharge sa version web (ou son paquet
materiel), le demarre ~3 s dans l'hote de FM-9 (outil tools/render --fwshot) et enregistre l'ecran en PNG
sous le nom de fichier du firmware (meme regle que CatalogEntry::fileName dans le plugin).

Usage : gen_previews.py <catalogue json> <outil render> <dossier previews>
"""
import base64, json, os, re, subprocess, sys, tempfile, urllib.request

CAT, RENDER, OUT = sys.argv[1:4]
os.makedirs(OUT, exist_ok=True)

def legal(n):   # juce::File::createLegalFileName
    return re.sub(r'["#@,;:<>*^|?\\/]', "", n).strip()

def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "FM-9 previews"})
    with urllib.request.urlopen(req, timeout=60) as r: return r.read()

def wasm_of(data):
    if data[:4] == b"\0asm": return data
    m = re.search(rb"AGFzbQ[A-Za-z0-9+/=]{1000,}", data)
    return base64.b64decode(m.group(0) + b"=" * (-len(m.group(0)) % 4)) if m else None

cat = json.load(open(CAT, encoding="utf-8"))
ok = fail = 0
for e in cat["firmwares"]:
    if not e.get("compatible"): continue
    name = legal(e["name"] or e.get("repo", "").split("/")[-1])
    dst = os.path.join(OUT, name + ".png")
    if os.path.exists(dst): continue
    data = None
    for url, web in ((e.get("url"), True), (e.get("pkg"), False)):
        if not url: continue
        try:
            d = get(url); d = wasm_of(d) if web else d
            if d: data = d; break
        except Exception as x: print("  download", url, x)
    if not data: fail += 1; print("NO FILE", e["name"]); continue
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as t: t.write(data); path = t.name
    try:
        r = subprocess.run([RENDER, "--fwshot", path, dst], capture_output=True, text=True, timeout=120)
        good = "fwshot ok" in r.stdout and os.path.exists(dst)
    except subprocess.TimeoutExpired: good = False
    os.unlink(path)
    ok += good; fail += not good
    print(("OK  " if good else "FAIL"), e["name"], flush=True)
print("previews :", ok, "ok,", fail, "echecs")
