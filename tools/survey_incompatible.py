#!/usr/bin/env python3
"""FM-9 - enquete sur les firmwares marques incompatibles dans le catalogue.

Pour chaque firmware incompatible, cherche partout ou un paquet materiel (.fwsc) ou une version web (.wasm)
pourrait etre publie, la ou le generateur du catalogue ne regarde pas :
  1. les Releases GitHub (fichiers joints),
  2. toutes les branches du depot (pas seulement gh-pages et la branche par defaut), y compris les dossiers profonds,
  3. la page d'installation / d'emulation (liens .fwsc / .wasm, version web en base64 dans la page),
  4. le depot lui-meme : existe-t-il, est-il archive, a-t-il une cible de compilation web (emscripten) ?
Telecharge chaque fichier candidat dans <sortie>/files/ et ecrit <sortie>/survey.json.

Usage : survey_incompatible.py <catalogue json> <sortie>   (variable GITHUB_TOKEN conseillee)
"""
import base64, json, os, re, sys, urllib.parse, urllib.request, urllib.error

CAT, OUT = sys.argv[1], sys.argv[2]
os.makedirs(os.path.join(OUT, "files"), exist_ok=True)
TOKEN = os.environ.get("GITHUB_TOKEN", "")
EXT = (".fwsc", ".wasm")
SRC_HINTS = ("emcc", "emscripten", "EMSCRIPTEN", "wasm32", "sim_render", "web_render")


def get(url, api=False, raw=False, limit=40_000_000):
    h = {"User-Agent": "FM-9 survey"}
    if api:
        h["Accept"] = "application/vnd.github+json"
        if TOKEN: h["Authorization"] = "Bearer " + TOKEN
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=h), timeout=40) as r:
            data = r.read(limit)
            return data if raw else data.decode("utf-8", "replace")
    except Exception as e:
        return None


def api(path):
    t = get("https://api.github.com" + path, api=True)
    try: return json.loads(t) if t else None
    except Exception: return None


def save(entry, url, tag):
    data = get(url, raw=True)
    if not data or len(data) < 64: return None
    name = re.sub(r"[^A-Za-z0-9._-]+", "_", entry["name"])[:40] + "__" + tag + "__" + re.sub(r"[^A-Za-z0-9._-]+", "_", url.rsplit("/", 1)[-1])[-60:]
    p = os.path.join(OUT, "files", name)
    open(p, "wb").write(data)
    return {"url": url, "file": name, "size": len(data), "kind": "wasm" if data[:4] == b"\0asm" else ("fwsc" if url.lower().endswith(".fwsc") else "other")}


cat = json.load(open(CAT, encoding="utf-8"))
items = cat["firmwares"] if isinstance(cat, dict) else cat
report = []
for e in items:
    if e.get("compatible"): continue
    r = {"name": e["name"], "repo": e.get("repo", ""), "page": e.get("page", ""), "reason": e.get("reason", ""),
         "exists": None, "archived": None, "releases": [], "branch_files": [], "page_links": [], "web_source": [], "found": []}
    repo = e.get("repo", "")
    if repo:
        meta = api("/repos/" + repo)
        r["exists"] = bool(meta and meta.get("full_name"))
        if r["exists"]:
            r["archived"] = meta.get("archived"); r["pushed"] = meta.get("pushed_at"); r["language"] = meta.get("language")
            for rel in (api("/repos/%s/releases?per_page=10" % repo) or [])[:10]:
                for a in rel.get("assets", []):
                    r["releases"].append(a["name"])
                    if a["name"].lower().endswith(EXT):
                        f = save(e, a["browser_download_url"], "rel")
                        if f: r["found"].append(f)
            for b in (api("/repos/%s/branches?per_page=50" % repo) or [])[:50]:
                tree = api("/repos/%s/git/trees/%s?recursive=1" % (repo, b["name"])) or {}
                for t in tree.get("tree", []):
                    p = t.get("path", "")
                    if p.lower().endswith(EXT):
                        r["branch_files"].append(b["name"] + ":" + p)
                    if re.search(r"(Makefile|CMakeLists\.txt|build[^/]*\.sh|package\.json)$", p) and len(r["web_source"]) < 12:
                        txt = get("https://raw.githubusercontent.com/%s/%s/%s" % (repo, b["name"], p)) or ""
                        hits = [h for h in SRC_HINTS if h in txt]
                        if hits: r["web_source"].append(b["name"] + ":" + p + " (" + ",".join(hits) + ")")
            for bf in r["branch_files"][:6]:
                br, p = bf.split(":", 1)
                f = save(e, "https://raw.githubusercontent.com/%s/%s/%s" % (repo, br, p), "git")
                if f: r["found"].append(f)
    if r["page"]:
        html = get(r["page"]) or ""
        base = r["page"] if r["page"].endswith("/") else r["page"].rsplit("/", 1)[0] + "/"
        links = set(re.findall(r"""["'(]([^"'()\s]+?\.(?:fwsc|wasm))(?:\?[^"']*)?["')]""", html))
        # scripts de la page : les installateurs chargent souvent la liste des paquets en JS / JSON
        for js in re.findall(r"""<script[^>]+src=["']([^"']+)["']""", html)[:8]:
            u = js if js.startswith("http") else urllib.parse.urljoin(base, js)
            t = get(u) or ""
            links |= set(re.findall(r"""["'`]([^"'`\s]+?\.(?:fwsc|wasm))["'`]""", t))
        for l in sorted(links)[:8]:
            u = l if l.startswith("http") else urllib.parse.urljoin(base, l)
            r["page_links"].append(u)
            f = save(e, u, "page")
            if f: r["found"].append(f)
        m = re.search(r"AGFzbQ[A-Za-z0-9+/=]{1000,}", html)
        if m:
            p = os.path.join(OUT, "files", re.sub(r"[^A-Za-z0-9._-]+", "_", e["name"])[:40] + "__b64.wasm")
            open(p, "wb").write(base64.b64decode(m.group(0) + "=" * (-len(m.group(0)) % 4)))
            r["found"].append({"url": r["page"] + " (base64)", "file": os.path.basename(p), "kind": "wasm"})
    report.append(r)
    print("%-40s exists=%s rel=%d git=%d page=%d src=%d found=%d" % (e["name"][:40], r["exists"], len(r["releases"]), len(r["branch_files"]),
                                                                    len(r["page_links"]), len(r["web_source"]), len(r["found"])), flush=True)

json.dump(report, open(os.path.join(OUT, "survey.json"), "w"), indent=1, ensure_ascii=False)
