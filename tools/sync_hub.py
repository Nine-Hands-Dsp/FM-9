#!/usr/bin/env python3
"""FM-9 - remet a jour les paquets .fwsc du catalogue d'apres celui de thefirmwarehub.com.

Le hub ne garde que la derniere version de chaque paquet : une ancienne adresse (fw/<nom>-<version>.fwsc) finit en 404.
Pour chaque firmware du catalogue dont le paquet vient du hub (ou compatible sans paquet), reprend l'adresse et la
version actuelles du hub (meme depot GitHub, sinon meme nom). Le plugin voit alors la nouvelle version : sa fiche
passe en orange (UPDATE) pour qui avait installe l'ancienne.

Usage : sync_hub.py <catalogue json> [catalog.json du hub, sinon telecharge]
"""
import json, re, sys, urllib.request, datetime
TODAY = datetime.date.today().isoformat()
HUB = "https://thefirmwarehub.com/"
CAT = sys.argv[1]
if len(sys.argv) > 2: hub = json.load(open(sys.argv[2]))
else:
    with urllib.request.urlopen(urllib.request.Request(HUB + "catalog.json", headers={"User-Agent": "FM-9 catalog"}), timeout=60) as r: hub = json.load(r)
by_repo = {(h.get("repo") or "").lower(): h for h in hub["firmwares"] if h.get("pkg")}
by_name = {(h.get("name") or "").lower(): h for h in hub["firmwares"] if h.get("pkg")}
cat = json.load(open(CAT, encoding="utf-8"))
changed = 0
import collections
repo_count = collections.Counter((e.get("repo") or "").lower() for e in cat["firmwares"])   # un depot peut porter plusieurs firmwares
# date d'ajout sur le hub (filtre New de FM-9) : pour tous les firmwares listes par le hub
all_repo = {(h.get("repo") or "").lower(): h for h in hub["firmwares"] if h.get("repo")}
all_name = {(h.get("name") or "").lower(): h for h in hub["firmwares"]}
for e in cat["firmwares"]:
    h = all_repo.get((e.get("repo") or "").lower()) or all_name.get(e["name"].lower())
    if h and h.get("added") and e.get("added") != h["added"]: e["added"] = h["added"]; changed += 1
    if h and h.get("updated") and e.get("updated", "") < h["updated"]: e["updated"] = h["updated"]; changed += 1
for e in cat["firmwares"]:
    if e.get("vendor"): continue
    on_hub = HUB in (e.get("pkg") or "")
    h = by_name.get(e["name"].lower()) or (by_repo.get((e.get("repo") or "").lower()) if repo_count.get((e.get("repo") or "").lower(), 0) == 1 else None)
    if h and "(" in h["name"] and h["name"].lower() != e["name"].lower(): h = None     # variante (Bluetooth...) : un autre firmware
    if not h: continue
    if not on_hub and e.get("pkg") and "Nine-Hands-Dsp/FM-9" in e["pkg"]: continue          # versions compilees par FM-9
    url = HUB + h["pkg"]
    def ver(u):
        m = re.findall(r"(\d+(?:\.\d+)+)", (u or "").rsplit("/", 1)[-1])
        return tuple(int(x) for x in m[-1].split(".")) if m else None
    if not on_hub and e.get("pkg") and ver(e["pkg"]) and ver(url) and ver(url) < ver(e["pkg"]): continue   # jamais une version plus ancienne
    if e.get("pkg") != url:
        print("%-28s %s -> %s" % (e["name"][:28], (e.get("pkg") or "-").rsplit("/", 1)[-1], h["pkg"].rsplit("/", 1)[-1]))
        e["pkg"] = url; e["pkgVersion"] = h.get("version") or ""; changed += 1
        e["updated"] = h.get("updated") or TODAY        # tri "Last modified" de FM-9
# date d'ajout sur le hub (filtre New du catalogue)
hub_by_repo = {(h.get("repo") or "").lower(): h for h in hub["firmwares"]}
hub_by_name = {(h.get("name") or "").lower(): h for h in hub["firmwares"]}
for e in cat["firmwares"]:
    h = hub_by_name.get(e["name"].lower()) or (hub_by_repo.get((e.get("repo") or "").lower()) if repo_count.get((e.get("repo") or "").lower(), 0) == 1 else None)
    if h and h.get("added") and e.get("added") != h["added"]: e["added"] = h["added"]; changed += 1
# firmwares publies sur le hub et absents du catalogue : ajoutes (a verifier au demarrage par gen_previews.py)
known_r = {(e.get("repo") or "").lower() for e in cat["firmwares"] if repo_count.get((e.get("repo") or "").lower(), 0) == 1}
known_n = {e["name"].lower() for e in cat["firmwares"]}
added = 0
for h in hub["firmwares"]:
    if not h.get("pkg") or h["name"].lower() in known_n: continue
    if (h.get("repo") or "").lower() in known_r and "(" not in h["name"]: continue           # meme depot, meme firmware
    what = (h.get("what") or "").strip()
    if len(what) > 150: what = what[:149].rstrip() + "…"
    cat["firmwares"].append({"name": h["name"], "author": h.get("author") or "", "group": "thefirmwarehub.com", "section": "Sound & groovebox",
                             "desc": what, "repo": h.get("repo") or "", "page": h.get("site") or h.get("play") or "", "compatible": True, "reason": "",
                             "url": "", "abi": "", "size": h.get("size") or 0, "pkg": HUB + h["pkg"], "pkgVersion": h.get("version") or "",
                             "license": h.get("lic") or "", "added": h.get("added") or "", "updated": h.get("updated") or h.get("added") or TODAY, "unverified": True})
    added += 1; changed += 1
    print("+ %s (%s)" % (h["name"], h.get("added")))
print(changed, "changements,", added, "firmwares ajoutes")
if changed:
    json.dump(cat, open(CAT, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
