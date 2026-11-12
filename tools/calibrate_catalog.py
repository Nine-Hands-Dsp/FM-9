#!/usr/bin/env python3
"""FM-9 - calibrage des firmwares materiels du catalogue (horloge Auto et canal MIDI Auto du plugin).

Pour chaque firmware compatible dont le paquet .fwsc n'a pas encore ete calibre (champ calibPkg different de pkg) :
telecharge le paquet et lance `FM9Render --calibrate`, qui fait tourner l'emulation hors temps reel et mesure
- clock : la plus petite horloge (MHz) ou le firmware remplit tous ses demi-buffers audio a temps sous des accords
  de 4 notes (0 : en retard meme a 480 MHz) ; ne depend que du firmware, pas de l'ordinateur ;
- midiCh : les canaux MIDI qui le font jouer (bit k = canal k+1).
Plusieurs calibrages en parallele, limites en nombre et en temps a chaque passage (la suite la nuit d'apres).

Usage : calibrate_catalog.py <catalogue json> <outil render> [max] [minutes]
"""
import json, os, subprocess, sys, tempfile, time, urllib.request
from concurrent.futures import ThreadPoolExecutor

CAT, RENDER = sys.argv[1:3]
MAX = int(sys.argv[3]) if len(sys.argv) > 3 else 120
BUDGET = float(sys.argv[4]) * 60 if len(sys.argv) > 4 else 50 * 60
t0 = time.time()
cat = json.load(open(CAT, encoding="utf-8"))
todo = [e for e in cat["firmwares"] if e.get("compatible") and e.get("pkg") and e.get("calibPkg") != e["pkg"]][:MAX]
print(len(todo), "firmwares a calibrer")

def one(e):
    if time.time() - t0 > BUDGET: return e, None
    try:
        data = urllib.request.urlopen(urllib.request.Request(e["pkg"], headers={"User-Agent": "FM-9 calibrate"}), timeout=60).read()
    except Exception as x:
        return e, "download " + str(x)
    with tempfile.NamedTemporaryFile(suffix=".fwsc", delete=False) as t: t.write(data); path = t.name
    try:
        r = subprocess.run([RENDER, "--calibrate", path], capture_output=True, text=True, timeout=600)
        out = [l for l in r.stdout.splitlines() if l.startswith("calib")]
        return e, out[-1] if out else "no result"
    except subprocess.TimeoutExpired:
        return e, "timeout"
    finally:
        os.unlink(path)

done = 0
with ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
    for e, res in pool.map(one, todo):
        if res is None: continue
        p = res.split()
        if len(p) == 3 and p[0] == "calib":
            e["clock"], e["midiCh"], e["calibPkg"] = int(p[1]), int(p[2]), e["pkg"]
            done += 1
            print("%-30s %4s MHz  canaux %s" % (e["name"][:30], p[1], [k + 1 for k in range(16) if int(p[2]) >> k & 1]), flush=True)
        else:
            print("%-30s %s" % (e["name"][:30], res), flush=True)
print("calibres :", done)
json.dump(cat, open(CAT, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
