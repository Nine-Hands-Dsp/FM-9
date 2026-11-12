#!/usr/bin/env python3
"""FM-9 - remet a jour les paquets .fwsc du catalogue d'apres celui de thefirmwarehub.com.

Le hub ne garde que la derniere version de chaque paquet : une ancienne adresse (fw/<nom>-<version>.fwsc) finit en 404.
Pour chaque firmware du catalogue dont le paquet vient du hub (ou compatible sans paquet), reprend l'adresse et la
version actuelles du hub (meme depot GitHub, sinon meme nom). Le plugin voit alors la nouvelle version : sa fiche
passe en orange (UPDATE) pour qui avait installe l'ancienne.

Usage : sync_hub.py <catalogue json> [catalog.json du hub, sinon telecharge]
"""
import json, sys, urllib.request
HUB = "https://thefirmwarehub.com/"
CAT = sys.argv[1]
if len(sys.argv) > 2: hub = json.load(open(sys.argv[2]))
else:
    with urllib.request.urlopen(urllib.request.Request(HUB + "catalog.json", headers={"User-Agent": "FM-9 catalog"}), timeout=60) as r: hub = json.load(r)
by_repo = {(h.get("repo") or "").lower(): h for h in hub["firmwares"] if h.get("pkg")}
by_name = {(h.get("name") or "").lower(): h for h in hub["firmwares"] if h.get("pkg")}
cat = json.load(open(CAT, encoding="utf-8"))
changed = 0
for e in cat["firmwares"]:
    if e.get("vendor"): continue
    on_hub = HUB in (e.get("pkg") or "")
    if not on_hub and not (e.get("compatible") and not e.get("pkg")): continue
    h = by_repo.get((e.get("repo") or "").lower()) or by_name.get(e["name"].lower())
    if not h: continue
    url = HUB + h["pkg"]
    if e.get("pkg") != url:
        print("%-28s %s -> %s" % (e["name"][:28], (e.get("pkg") or "-").rsplit("/", 1)[-1], h["pkg"].rsplit("/", 1)[-1]))
        e["pkg"] = url; e["pkgVersion"] = h.get("version") or ""; changed += 1
print(changed, "paquets mis a jour")
if changed:
    json.dump(cat, open(CAT, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
