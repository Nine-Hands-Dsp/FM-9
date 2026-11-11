#!/usr/bin/env python3
"""FM-9 - ajoute au catalogue les paquets materiels (.fwsc) des firmwares de la FM-1.

Sources :
  1. la liste de fm1-emu (web/firmwares.json : paquets testes dans l'emulateur par son auteur) ;
  2. les .fwsc publies dans les depots du hub (clones de tools/gen_fw_catalog.py), demarres ici dans
     l'emulateur (fm1hw-check) avant d'etre retenus.
Un firmware est compatible s'il a un paquet materiel (emulateur de la puce) ou une version web (.wasm).

Usage : merge_hw_catalog.py <firmwares.json de fm1-emu> <dossier des clones> <fm1hw-check>
"""
import json, os, re, subprocess, sys, tempfile

EMU, CLONES, CHECK = sys.argv[1], sys.argv[2], sys.argv[3]
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
cat = json.load(open(os.path.join(ROOT, "catalog", "firmwares.json")))
emu = json.load(open(EMU))["firmwares"]

def repo_of(url):
    m = re.search(r"github\.com/([^/\s]+)/([^/\s#?]+)", url or "")
    return ("%s/%s" % (m.group(1), m.group(2).removesuffix(".git"))).lower() if m else ""

def norm(n):
    return re.sub(r"[^a-z0-9]", "", (n or "").lower())

by_repo, by_name = {}, {}
for f in cat["firmwares"]:
    if f["repo"]: by_repo.setdefault(f["repo"].lower(), []).append(f)
    by_name.setdefault(norm(f["name"]), []).append(f)

added = matched = 0
for e in emu:
    pkg = e.get("package") or e.get("download") or ""
    if not pkg: continue
    r = repo_of(e.get("repo", ""))
    cands = by_repo.get(r, []) if r else []
    if len(cands) > 1:
        cands = [c for c in cands if norm(c["name"]) == norm(e["name"])] or cands[:1]
    if not cands:
        cands = by_name.get(norm(e["name"]), [])[:1]
    if cands:
        for c in cands:
            c["pkg"] = pkg; c["pkgVersion"] = e.get("version", ""); c["license"] = e.get("license", "")
            if e.get("byo"): c["vendor"] = True
        matched += 1
    else:
        cat["firmwares"].append(dict(name=e["name"], author=e.get("author", ""), group="Also in the FM-1 emulator list", section="Sound & groovebox",
                                     desc=e.get("about", "").split(". ")[0].rstrip(".")[:150], repo=r if r and "fm1-emu" not in r else "",
                                     page=e.get("installer", ""), compatible=True, reason="", url="", abi="", size=0,
                                     pkg=pkg, pkgVersion=e.get("version", ""), license=e.get("license", ""), vendor=bool(e.get("byo"))))
        added += 1
print("fm1-emu :", matched, "retrouves dans le hub,", added, "ajoutes")

# 2. .fwsc publies dans les depots (pour les firmwares encore sans paquet)
tmp = tempfile.mkdtemp()
tested = ok = 0
for f in cat["firmwares"]:
    if f.get("pkg") or not f["repo"]: continue
    d = os.path.join(CLONES, f["repo"].replace("/", "__"))
    if not os.path.isdir(d): continue
    ls = subprocess.run(["git", "ls-tree", "-r", "FETCH_HEAD"], cwd=d, capture_output=True, text=True).stdout.splitlines()
    fw = [(l.split("\t", 1)[1], l.split()[2]) for l in ls if l.lower().endswith(".fwsc")]
    # pas les firmwares d'origine de M-VAVE qu'un depot embarque pour la restauration
    fw = [x for x in fw if not re.search(r"fm-1[-_]?v?0?1[0-9]|original|stock|restore", x[0], re.I)]
    if not fw: continue
    fw.sort(key=lambda x: x[0])
    path, blob = fw[-1]
    p = os.path.join(tmp, "c.fwsc")
    with open(p, "wb") as fh:
        fh.write(subprocess.run(["git", "cat-file", "blob", blob], cwd=d, capture_output=True).stdout)
    tested += 1
    try: r = subprocess.run([CHECK, p, "2"], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired: continue
    if r.stdout.startswith("OK"):
        br = subprocess.run(["git", "symbolic-ref", "--short", "HEAD"], cwd=d, capture_output=True, text=True).stdout.strip() or "main"
        f["pkg"] = "https://raw.githubusercontent.com/%s/%s/%s" % (f["repo"], br, path)
        ok += 1
print(".fwsc des depots :", ok, "demarrent sur", tested, "essayes")

for f in cat["firmwares"]:
    hw, web = bool(f.get("pkg")), bool(f.get("url"))
    f["compatible"] = hw or web
    if f["compatible"]:
        f["reason"] = ""
    elif "web build" in f.get("reason", "") or "no web build" in f.get("reason", ""):
        f["reason"] = "no update package (.fwsc) or web build published"
# descriptions de fm1-emu (plus parlantes que la polyphonie), firmware d'origine de M-VAVE en tete
about = {}
for e in emu:
    a = e.get("about", "").split(". ")[0].rstrip(".")
    about[norm(e["name"])] = a
    if repo_of(e.get("repo")): about.setdefault("r:" + repo_of(e["repo"]), a)
for f in cat["firmwares"]:
    a = about.get(norm(f["name"])) or about.get("r:" + f["repo"].lower())
    if a and (re.match(r"(Up to|\d|Dynamic|Bi-timbral|Multi-voice|Per-engine)", f["desc"]) or not f["desc"]): f["desc"] = a[:160]
    if f.get("vendor"): f.update(group="M-VAVE (original firmware)", name="FM-1 original firmware", author="M-VAVE")
cat["firmwares"] = [f for f in cat["firmwares"] if f.get("vendor")] + [f for f in cat["firmwares"] if not f.get("vendor")]
cat["generated"] = __import__("datetime").date.today().isoformat()
cat["hwSource"] = "https://github.com/joakimeriksson/fm1-emu (web/firmwares.json)"
n = sum(f["compatible"] for f in cat["firmwares"])
print(n, "compatibles sur", len(cat["firmwares"]), "(", sum(bool(f.get("pkg")) for f in cat["firmwares"]), "materiels,",
      sum(bool(f.get("url")) for f in cat["firmwares"]), "web )")
for dst in (os.path.join(ROOT, "catalog", "firmwares.json"), os.path.join(ROOT, "assets", "plugin", "firmwares.json")):
    json.dump(cat, open(dst, "w"), indent=1, ensure_ascii=False)
