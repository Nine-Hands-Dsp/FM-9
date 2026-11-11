#!/usr/bin/env python3
"""FM-9 - catalogue des firmwares de la FM-1 (d'apres le hub github.com/aroum/fm1-custom-fw).

Pour chaque firmware du hub (sons / grooveboxes, plateformes modulaires, jeux) :
  - cherche une version web (.wasm) dans le depot (branche gh-pages puis branche par defaut),
  - verifie ses exports (sim_render = famille Hortator, web_render = famille Felucca),
  - verifie qu'il demarre vraiment dans l'hote de FM-9 (outil tools/render --fwcheck),
  - note l'adresse directe du fichier (raw.githubusercontent.com) : le plugin l'installe sans l'API GitHub.
Ecrit catalog/firmwares.json (lu par FM-9 : integre au plugin, et mis a jour depuis GitHub).

Usage : gen_fw_catalog.py <README du hub> <outil render> [dossier de travail]
"""
import json, os, re, subprocess, sys, tempfile, concurrent.futures as cf

README, RENDER = sys.argv[1], sys.argv[2]
WORK = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix="fwcat_")
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
os.makedirs(WORK, exist_ok=True)

SECTIONS = {"1.1": "Sound & groovebox", "1.2": "Modular platforms", "1.3": "Games & experiments"}

def clean(t):
    t = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", t)          # liens markdown
    t = re.sub(r"<br\s*/?>", " ", t)
    t = re.sub(r"[*`_]", "", t)
    return re.sub(r"\s+", " ", t).strip()

def first_sentence(t, n=150):
    t = clean(t)
    m = re.search(r"Concept:\s*(.+?)(?:\.\s|$)", t)
    s = m.group(1) if m else re.split(r"(?<=\.)\s", t)[0]
    s = re.sub(r"^(Polyphony[^:]*:)\s*", "", s).strip().rstrip(".")
    return (s[: n - 1] + "…") if len(s) > n else s

def parse_readme():
    rows, sec, sub = [], None, ""
    for line in open(README, encoding="utf-8"):
        m = re.match(r"^###\s+(\d\.\d)\.", line)
        if m: sec = m.group(1) if m.group(1) in SECTIONS else None; sub = ""; continue
        m = re.match(r"^####\s+\d\.\d\.\d\.\s+(.*)", line)
        if m: sub = re.sub(r"\s*\(.*", "", m.group(1)).strip(); continue
        if line.startswith("## "): sec = None
        if not sec or not re.match(r"^\|\s*\d+\s*\|", line): continue
        cols = [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", line.strip().strip("|"))]
        if len(cols) < 3: continue
        head, desc = cols[1], cols[2]
        nm = re.search(r"\*\*(.+?)\*\*", head)
        title = clean(nm.group(1)) if nm else clean(head)[:60]
        name, _, author = title.partition(" by ")
        gh = re.search(r"https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)", head)
        web = re.search(r"\[(?:Web [^\]]*|[^\]]*Emulator[^\]]*|[^\]]*Installer[^\]]*)\]\((https?://[^)]+)\)", head)
        rows.append(dict(name=name.strip(), author=author.strip(), group=sub or SECTIONS[sec], section=SECTIONS[sec],
                         desc=first_sentence(desc), repo=("%s/%s" % (gh.group(1), gh.group(2).removesuffix(".git"))) if gh else "",
                         page=web.group(1) if web else ""))
    return rows

def abi_of(path):
    d = open(path, "rb").read()
    if d[:4] != b"\0asm": return 0
    p = 8
    def leb(q):
        r = s = 0
        while q < len(d):
            b = d[q]; q += 1; r |= (b & 0x7F) << s; s += 7
            if b < 0x80: break
        return r, q
    while p < len(d):
        sid = d[p]; p += 1
        ln, p = leb(p); end = p + ln
        if sid == 7:
            n, q = leb(p); found = 0
            for _ in range(n):
                nl, q = leb(q); nm = d[q:q + nl].decode("utf-8", "replace"); q += nl + 1; _, q = leb(q)
                if nm == "sim_render": found = 1
                elif nm == "web_render" and not found: found = 2
            return found
        p = end
    return 0

ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0")
def git(*a, cwd=None, timeout=300):
    try:
        return subprocess.run(["git", *a], cwd=cwd, capture_output=True, text=True, timeout=timeout, env=ENV)
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(a, 1, "", "timeout")

SKIP = ("espeak", "flac", "vendor", "reader", "node_modules")

def resolve(row):
    out = dict(row, compatible=False, reason="", url="", abi="", size=0)
    if not row["repo"]:
        out["reason"] = "no public GitHub repository"; return out
    owner, repo = row["repo"].split("/")
    d = os.path.join(WORK, owner + "__" + repo)
    if not os.path.isdir(d):
        r = git("clone", "-q", "--filter=blob:none", "--no-checkout", "https://github.com/%s/%s" % (owner, repo), d)
        if r.returncode != 0:
            out["reason"] = "repository not reachable"; return out
    heads = git("ls-remote", "--heads", "origin", cwd=d).stdout
    default = git("symbolic-ref", "--short", "HEAD", cwd=d).stdout.strip() or "main"
    branches = (["gh-pages"] if "refs/heads/gh-pages" in heads else []) + [default]
    tried_any = False
    for br in branches:
        git("fetch", "-q", "--filter=blob:none", "origin", br, cwd=d)
        ls = git("ls-tree", "-r", "FETCH_HEAD", cwd=d).stdout.splitlines()
        cands = []
        for l in ls:
            meta, path = l.split("\t", 1)
            low = path.lower()
            if low.endswith(".wasm") and not any(s in low for s in SKIP):
                cands.append((path, meta.split()[2]))
        cands.sort(key=lambda c: -((2 if "emu" in c[0].lower() else 0) + (1 if "sim" in c[0].lower() else 0)))
        for path, blob in cands[:6]:
            tried_any = True
            f = os.path.join(d, "cand_%s.wasm" % blob[:8])
            with open(f, "wb") as fh:
                try: fh.write(subprocess.run(["git", "cat-file", "blob", blob], cwd=d, capture_output=True, timeout=300, env=ENV).stdout)
                except subprocess.TimeoutExpired: continue
            abi = abi_of(f)
            if not abi: continue
            try: chk = subprocess.run([RENDER, "--fwcheck", f], capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired: out["reason"] = "web build found but does not start (timeout)"; continue
            if "fwcheck ok" not in chk.stdout:
                out["reason"] = "web build found but does not start (%s)" % (chk.stdout.strip().splitlines() or ["?"])[-1][:80]
                continue
            out.update(compatible=True, reason="", abi="sim" if abi == 1 else "web", size=os.path.getsize(f),
                       url="https://raw.githubusercontent.com/%s/%s/%s/%s" % (owner, repo, br, path), branch=br, path=path)
            return out
    if not out["reason"]:
        out["reason"] = "web build with an unknown interface" if tried_any else "no web build (.wasm): hardware firmware only"
    return out

rows = parse_readme()
print(len(rows), "firmwares dans le hub")
seen, uniq = set(), []
for r in rows:
    k = (r["name"].lower(), r["repo"].lower())
    if k in seen: continue
    seen.add(k); uniq.append(r)
with cf.ThreadPoolExecutor(12) as ex:
    res = list(ex.map(resolve, uniq))
ok = sum(r["compatible"] for r in res)
print(ok, "compatibles sur", len(res))
os.makedirs(os.path.join(ROOT, "catalog"), exist_ok=True)
cat = dict(source="https://github.com/aroum/fm1-custom-fw", generated=__import__("datetime").date.today().isoformat(),
           firmwares=[{k: r[k] for k in ("name", "author", "group", "section", "desc", "repo", "page", "compatible", "reason", "url", "abi", "size") if k in r} for r in res])
for dst in (os.path.join(ROOT, "catalog", "firmwares.json"), os.path.join(ROOT, "assets", "plugin", "firmwares.json")):
    json.dump(cat, open(dst, "w"), indent=1, ensure_ascii=False)
for r in res:
    print("%-4s %-28s %-30s %s" % ("OK" if r["compatible"] else "--", r["name"][:28], r["repo"][:30], r["url"].rsplit("/", 1)[-1] if r["compatible"] else r["reason"]))
