#!/usr/bin/env python3
"""FM-9 (NINE HANDS | DSP) - facade 3000 x 1800 d'apres la photo de la M-VAVE FM-1.

Tout est rendu en procedural a la taille finale (aucun agrandissement d'image) :
coque creme mate, pave de touches en silicone, touches, ecran (vitre vide),
eclairage a 45 deg venant du haut a gauche. Les potards, textes et logos ne sont
pas dans le fond : ce sont des calques animes / du vectoriel dessines par le plugin.

Sorties : assets/plugin/background.png, assets/plugin/knob_*.png, Layout.h, tools/layout.json
"""
import json, os
import numpy as np
import cv2
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "assets", "plugin")
os.makedirs(OUT, exist_ok=True)

W, H = 3000, 1800
F = 5.0 / 3.0                 # pixels de l'image par pixel de la photo de reference (2000 px)
OX, OY = 100.0, 460.0         # coin haut gauche du boitier sur la photo
rng = np.random.default_rng(9)

def P(x, y):                  # photo -> image finale
    return ((x - OX) * F, (y - OY) * F)

def R(x0, y0, x1, y1):
    a, b = P(x0, y0); c, d = P(x1, y1)
    return [a, b, c, d]

# ------------------------------------------------------------------ geometrie (photo)
SHELL = R(100, 460, 1900, 1540); SHELL_R = 88 * F
SCREEN_BEZEL = R(566, 543, 990, 969); BEZEL_R = 36 * F
SCREEN_GLASS = R(622, 598, 936, 912); GLASS_R = 6 * F
OCT_PAD = R(207, 923, 484, 1011); OCT_PAD_R = 12 * F
OCT_KEYS = [R(219, 932, 351, 1002), R(362, 932, 474, 1002)]
FN_PAD = R(1060, 762, 1775, 1012); FN_PAD_R = 22 * F
FN_COLS = [1140, 1251, 1364, 1476, 1589, 1702]
FN_ROWS = [830, 944]
FN_KEY = 90                   # touche de fonction 90 x 90 (photo)
KB_PAD = R(170, 1078, 1830, 1468); KB_PAD_R = 46 * F
WHITE_X = [237 + 101.45 * i for i in range(16)]
BLACK_X = [292, 393, 493, 697, 797, 1000, 1101, 1202, 1404, 1505, 1707]
BLACK_Y = (1102, 1266); WHITE_Y = (1266, 1430); KEY_W = 80
DOTS = [(597, 1272), (899, 1272), (1301, 1272), (1603, 1272), (1418, 887)]
KNOBS = {"master": (252, 629), "select": (440, 629), "presets": (252, 818), "algorithm": (440, 818),
         "knob1": (1114, 627), "knob2": (1316, 627), "knob3": (1518, 627), "knob4": (1722, 627)}
KNOB_D = 82                   # diametre exterieur de la couronne (photo)
LOGO_C = (782, 1018); LOGO_W = 200

# ------------------------------------------------------------------ outils
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
L = np.array([-1.0, -1.0, 1.25]); L /= np.linalg.norm(L)        # lumiere haut gauche, 45 deg

def sdf_rrect(r, rad):
    x0, y0, x1, y1 = r
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - rad, (y1 - y0) / 2 - rad
    qx = np.abs(xx - cx) - hx; qy = np.abs(yy - cy) - hy
    out = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    return out + np.minimum(np.maximum(qx, qy), 0) - rad

def cov(sd):                  # couverture anti-crenelee
    return np.clip(0.5 - sd, 0, 1)

def blur(a, s):
    k = int(s * 3) | 1
    return cv2.GaussianBlur(a, (k, k), s)

def shade_from_height(h, strength=1.0):
    gy, gx = np.gradient(h)
    n = np.dstack([-gx * strength, -gy * strength, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    lam = np.clip(n @ L, 0, 1)
    hv = (L + np.array([0, 0, 1.0])); hv /= np.linalg.norm(hv)
    spec = np.clip(n @ hv, 0, 1)
    return lam, spec, n

def mix(img, col, a):
    a = a[..., None]
    img[:] = img * (1 - a) + np.asarray(col, np.float32) * a

def grain(sigma, amp, seed):
    g = np.random.default_rng(seed).normal(0, 1, (H, W)).astype(np.float32)
    return blur(g, sigma) * amp / max(1e-6, blur(g, sigma).std())

# ------------------------------------------------------------------ coque
img = np.zeros((H, W, 3), np.float32)
sd_shell = sdf_rrect(SHELL, SHELL_R)
alpha = cov(sd_shell)
cream = np.array([226, 218, 209], np.float32)
img[:] = cream
# grain de plastique mat (fin) + tres legere marbrure
img += (grain(0.8, 1.8, 1) + grain(10, 0.5, 2))[..., None]
# arete arrondie du boitier : bande de 26 px eclairee / ombree selon la lumiere
edge = np.clip(-sd_shell / 46.0, 0, 1)
hshell = np.sin(edge * np.pi / 2)                  # arrondi doux du boitier
lam, spec, _ = shade_from_height(blur(hshell * 30.0, 2.0), 1.0)
flat = float(np.clip(np.array([0, 0, 1.0]) @ L, 0, 1))
img *= np.clip(0.86 + 0.14 * lam / flat, 0.7, 1.08)[..., None]

# ------------------------------------------------------------------ cuvettes et paves de silicone
rubber = np.array([60, 64, 68], np.float32)
def pad(r, rad, lip=7.0):
    sd = sdf_rrect(r, rad)
    # ombre interne de la cuvette (cote haut gauche) puis le silicone
    m = cov(sd)
    mix(img, rubber, m)
    inner = np.clip(1 - (-sd) / (lip * 2.2), 0, 1) * m
    sh = blur((sd > -lip * 1.5).astype(np.float32) * m, lip)
    off = np.zeros_like(sh); s = int(lip * 1.2)
    off[s:, s:] = sh[:-s, :-s]
    img[:] *= (1 - 0.50 * np.clip(off * m, 0, 1))[..., None]
    # occlusion de contact sur la coque tout autour de la cuvette
    aoc = blur((sd < 0).astype(np.float32), 6.0)
    img[:] *= (1 - 0.16 * np.clip(aoc - m, 0, 1))[..., None]
    # liseré de la coque autour : arete claire en bas a droite (lumiere sur la levre), sombre en haut a gauche
    ring = np.clip(1 - np.abs(sd - 2.0) / 3.0, 0, 1)
    nx = np.gradient(sd, axis=1); ny = np.gradient(sd, axis=0)
    facing = -(nx * -1 + ny * -1) / np.sqrt(2)
    img[:] += (ring * facing * 18)[..., None]
    return sd

pad(KB_PAD, KB_PAD_R, 10); pad(FN_PAD, FN_PAD_R, 10); pad(OCT_PAD, OCT_PAD_R, 7)

# ------------------------------------------------------------------ touches en silicone (champ de hauteur)
keyH = np.zeros((H, W), np.float32); keyM = np.zeros((H, W), np.float32)
key_rects = {}
def key(name, r, rad, slot=None, bevel=16.0):
    sd = sdf_rrect(r, rad)
    t = np.clip(-sd / bevel, 0, 1)
    h = np.sqrt(1 - (1 - t) ** 2) * 18.0          # bord arrondi, dessus plat
    if slot is not None:                           # rainure au centre
        sds = sdf_rrect(slot, (slot[2] - slot[0]) / 2)
        h -= np.clip(1 - (sds + 3) / 5.0, 0, 1) * 3.0 * (sd < 0)
    np.maximum(keyH, h, out=keyH)
    np.maximum(keyM, cov(sd), out=keyM)
    key_rects[name] = r

for i, x in enumerate(WHITE_X):
    r = R(x - KEY_W / 2, WHITE_Y[0] + 4, x + KEY_W / 2, WHITE_Y[1])
    cx = (r[0] + r[2]) / 2; cy = (r[1] + r[3]) / 2
    key("w%d" % i, r, KEY_W / 2 * F, [cx - 6, cy - 55, cx + 6, cy + 55])
for i, x in enumerate(BLACK_X):
    r = R(x - KEY_W / 2, BLACK_Y[0], x + KEY_W / 2, BLACK_Y[1] - 6)
    cx = (r[0] + r[2]) / 2; cy0 = r[1] + 0.22 * (r[3] - r[1])
    key("b%d" % i, r, KEY_W / 2 * F, [cx - 6, cy0, cx + 6, cy0 + 70])
for j, y in enumerate(FN_ROWS):
    for i, x in enumerate(FN_COLS):
        key("f%d" % (j * 6 + i), R(x - FN_KEY / 2, y - FN_KEY / 2, x + FN_KEY / 2, y + FN_KEY / 2), 20 * F)
for i, r in enumerate(OCT_KEYS):
    key("oct%d" % i, r, 10 * F, bevel=12.0)

# ombre portee des touches sur le pave (45 deg vers le bas a droite)
shadow = blur(keyM, 13.0)
off = np.zeros_like(shadow); s = 15
off[s:, s:] = shadow[:-s, :-s]
img *= (1 - 0.58 * np.clip(off - keyM, 0, 1))[..., None]
contact = blur(keyM, 4.0)
img *= (1 - 0.30 * np.clip(contact - keyM, 0, 1))[..., None]
ao = blur(keyM, 24.0)
img *= (1 - 0.18 * np.clip(ao - keyM, 0, 1))[..., None]

lam, spec, _ = shade_from_height(keyH, 1.0)
keycol = np.array([72, 76, 80], np.float32)
shade = 0.62 + 0.45 * lam / flat
k = keycol * shade[..., None] + (spec ** 28 * 26)[..., None]
k += (grain(0.7, 1.6, 3))[..., None]
mix(img, k * 1.0, keyM) if False else None
img = img * (1 - keyM[..., None]) + k * keyM[..., None]

# points de repere moules
for (x, y) in DOTS:
    cx, cy = P(x, y)
    d = np.hypot(xx - cx, yy - cy)
    m = np.clip(7.5 - d, 0, 1)
    img = img * (1 - m[..., None]) + np.array([16, 17, 19], np.float32) * m[..., None]

# ------------------------------------------------------------------ ecran
sdb = sdf_rrect(SCREEN_BEZEL, BEZEL_R)
mb = cov(sdb)
sh = blur(mb, 18.0); off = np.zeros_like(sh); off[18:, 18:] = sh[:-18, :-18]
img *= (1 - 0.42 * np.clip(off - mb, 0, 1))[..., None]
con = blur(mb, 5.0)
img *= (1 - 0.28 * np.clip(con - mb, 0, 1))[..., None]
bez = np.clip(-sdb / 14.0, 0, 1)
lamb, specb, _ = shade_from_height((1 - (1 - bez) ** 2) * 14.0, 1.0)
bcol = np.array([20, 21, 24], np.float32) * (0.75 + 0.45 * lamb / flat)[..., None] + (specb ** 40 * 60)[..., None]
img = img * (1 - mb[..., None]) + bcol * mb[..., None]
sdg = sdf_rrect(SCREEN_GLASS, GLASS_R)
mg = cov(sdg)
g = np.array([7, 8, 10], np.float32) + np.zeros((H, W, 3), np.float32)
# reflet tres doux de la vitre en haut a gauche
gx0, gy0 = SCREEN_GLASS[0], SCREEN_GLASS[1]
diag = ((xx - gx0) + (yy - gy0)) / (SCREEN_GLASS[2] - gx0 + SCREEN_GLASS[3] - gy0)
g += (np.clip(1 - diag * 1.6, 0, 1) ** 2 * 10)[..., None]
img = img * (1 - mg[..., None]) + g * mg[..., None]

# ------------------------------------------------------------------ eclairage d'ambiance a 45 deg (fond seulement)
dn = (xx + yy) / (W + H)
gain = 1.12 - 0.28 * dn
band = np.exp(-((dn - 0.30) / 0.10) ** 2) * 12.0
img = img * gain[..., None] + band[..., None]

out = np.dstack([np.clip(img, 0, 255), alpha * 255]).astype(np.uint8)
Image.fromarray(out, "RGBA").save(os.path.join(OUT, "background.png"), optimize=True)
print("background.png", out.shape)

# ------------------------------------------------------------------ potard noir moletee (calques 1024 px)
S = 1024
ky, kx = np.mgrid[0:S, 0:S].astype(np.float32)
c = (S - 1) / 2.0
rr = np.hypot(kx - c, ky - c) / (S / 2.0)          # 1 = bord de l'image
th = np.arctan2(ky - c, kx - c)
R_OUT, R_CAP = 0.92, 0.70
teeth = 48
def save(name, rgb, a):
    arr = np.dstack([np.clip(rgb, 0, 255), np.clip(a, 0, 1) * 255]).astype(np.uint8)
    Image.fromarray(arr, "RGBA").save(os.path.join(OUT, "knob_%s.png" % name), optimize=True)

def aa(v):
    return np.clip(v * S / 2.0 + 0.5, 0, 1)

# couronne : dents trapezoidales. Eclairage SANS direction (lumiere venant d'en face) : le calque tourne
# avec le potard sans faire tourner la lumiere ; la lumiere a 45 deg est dans le calque fixe "highlight".
tooth = 0.5 + 0.5 * np.cos(th * teeth)
r_edge = R_OUT - 0.014 * (1 - tooth)
crown_a = aa(r_edge - rr) * aa(rr - (R_CAP - 0.02))
hc = np.clip((r_edge - rr) / 0.06, 0, 1) ** 0.6 * (0.62 + 0.38 * tooth) * 34
# forme et rendu des aretes comme la premiere version (moletage fin et reflets sur chaque arete),
# mais la lumiere de chaque dent est definie dans le repere de la dent (radial / tangentiel) :
# toutes les dents sont eclairees pareil, donc rien ne bouge quand le calque tourne.
gy_, gx_ = np.gradient(hc)
n = np.dstack([-gx_, -gy_, np.ones_like(hc)]); n /= np.linalg.norm(n, axis=2, keepdims=True)
cr_, sr_ = np.cos(th), np.sin(th)
nr = n[..., 0] * cr_ + n[..., 1] * sr_             # composante radiale
nt = -n[..., 0] * sr_ + n[..., 1] * cr_            # composante tangentielle
Lloc = np.array([-0.45, -0.75, 1.0]); Lloc /= np.linalg.norm(Lloc)
lamc = np.clip(nr * Lloc[0] + nt * Lloc[1] + n[..., 2] * Lloc[2], 0, 1)
hv = Lloc + np.array([0, 0, 1.0]); hv /= np.linalg.norm(hv)
specc = np.clip(nr * hv[0] + nt * hv[1] + n[..., 2] * hv[2], 0, 1)
base = np.array([28, 28, 30], np.float32)
crown_rgb = base * (0.45 + 0.9 * lamc)[..., None] + (specc ** 30 * 70)[..., None]
save("crown", crown_rgb, crown_a)

# chapeau : dome tres plat, plastique noir satine, sans reflet (les reflets sont un calque fixe)
cap_a = aa(R_CAP - rr)
cap_rgb = np.zeros((S, S, 3), np.float32) + np.array([22, 22, 24], np.float32)
cap_rgb += (np.random.default_rng(5).normal(0, 1, (S, S)) * 1.2)[..., None]
cap_rgb *= (1 - 0.18 * np.clip((rr - R_CAP * 0.7) / (R_CAP * 0.3), 0, 1))[..., None]
save("cap", cap_rgb, cap_a)

# index blanc (potard MASTER) pointe vers le haut
mk = np.zeros((S, S), np.float32)
x0, x1 = c - 0.075 * S / 2, c + 0.075 * S / 2
y0, y1 = c - (R_CAP - 0.05) * S / 2, c - 0.40 * S / 2
mk = np.clip(np.minimum(np.minimum(kx - x0, x1 - kx), np.minimum(ky - y0, y1 - ky)) + 0.5, 0, 1)
save("marker", np.zeros((S, S, 3), np.float32) + np.array([236, 234, 228], np.float32), mk)

# ombre portee centree (decalage ajoute par le code)
sh0 = aa(R_OUT - 0.01 - rr)
sh = cv2.GaussianBlur(sh0, (0, 0), 26) * 0.55 + cv2.GaussianBlur(sh0, (0, 0), 7) * 0.40
save("shadow", np.zeros((S, S, 3), np.float32), np.clip(sh, 0, 0.85))

# calque FIXE de lumiere : lumiere douce en haut a gauche a 45 deg (eclaire le flanc de la couronne et le liseré du chapeau),
# ombre propre en bas a droite. Ne tourne jamais.
ang = np.cos(th - np.deg2rad(-135))                              # 1 face a la lumiere (haut gauche), -1 a l'oppose
ring = np.clip((rr - (R_CAP - 0.01)) / (R_OUT - R_CAP), 0, 1) * aa(R_OUT - 0.004 - rr) * aa(rr - (R_CAP - 0.02))
lip = np.clip(1 - np.abs(rr - (R_CAP - 0.025)) / 0.022, 0, 1)
spot = np.exp(-((kx - (c - 0.24 * S / 2)) ** 2 + (ky - (c - 0.24 * S / 2)) ** 2) / (2 * (0.26 * S / 2) ** 2)) * 0.045 * (rr < R_CAP)
light = np.clip(ang, 0, 1) ** 1.5 * (ring * 0.30 + lip * 0.50) + spot
dark = np.clip(-ang, 0, 1) ** 1.2 * (ring * 0.38 + lip * 0.20)
rgb = np.zeros((S, S, 3), np.float32) + 255 * (light / np.maximum(light + dark, 1e-6))[..., None]
save("highlight", rgb, np.clip(light + dark, 0, 1))
print("potards ok")

# ------------------------------------------------------------------ repere du plugin (image / 2 = 1500 x 900 unites)
U = 0.5
def U4(r):
    return [round(v * U, 2) for v in r]
lay = {
    "image": [W, H], "units": [W * U, H * U],
    "screen": U4(SCREEN_GLASS), "bezel": U4(SCREEN_BEZEL),
    "knobs": {k: [round(P(*v)[0] * U, 2), round(P(*v)[1] * U, 2)] for k, v in KNOBS.items()},
    "knobD": round(KNOB_D * F * U / 0.92, 2),           # calque 1024 : couronne a 92 % du cadre
    "keys": {k: U4(v) for k, v in key_rects.items()},
    "logo": [round(P(*LOGO_C)[0] * U, 2), round(P(*LOGO_C)[1] * U, 2), round(LOGO_W * F * U, 2)],
    "labelsTop": round(P(0, 547)[1] * U, 2), "labelsMid": round(P(0, 735)[1] * U, 2),
}
with open(os.path.join(ROOT, "tools", "layout.json"), "w") as f:
    json.dump(lay, f, indent=1)

def rect(v):
    return "juce::Rectangle<float> { %.2ff, %.2ff, %.2ff, %.2ff }" % (v[0], v[1], v[2] - v[0], v[3] - v[1])
with open(os.path.join(ROOT, "Layout.h"), "w") as f:
    f.write("// FM-9 - reperes de la facade (genere par tools/make_art.py), repere 1500 x 900 unites\n#pragma once\n#include <JuceHeader.h>\n\nnamespace fm9::layout\n{\n")
    f.write("constexpr float unitsW = %.1ff, unitsH = %.1ff;\n" % (W * U, H * U))
    f.write("inline const juce::Rectangle<float> screen = %s;\n" % rect(lay["screen"]))
    f.write("inline const juce::Rectangle<float> bezel = %s;\n" % rect(lay["bezel"]))
    f.write("constexpr float knobD = %.2ff;\n" % lay["knobD"])
    for k2, v in lay["knobs"].items():
        f.write("constexpr juce::Point<float> knob_%s { %.2ff, %.2ff };\n" % (k2, v[0], v[1]))
    f.write("inline const juce::Rectangle<float> whiteKeys[16] = {\n" + ",\n".join("    " + rect(lay["keys"]["w%d" % i]) for i in range(16)) + " };\n")
    f.write("inline const juce::Rectangle<float> blackKeys[11] = {\n" + ",\n".join("    " + rect(lay["keys"]["b%d" % i]) for i in range(11)) + " };\n")
    f.write("inline const juce::Rectangle<float> fnKeys[12] = {\n" + ",\n".join("    " + rect(lay["keys"]["f%d" % i]) for i in range(12)) + " };\n")
    f.write("inline const juce::Rectangle<float> octKeys[2] = {\n" + ",\n".join("    " + rect(lay["keys"]["oct%d" % i]) for i in range(2)) + " };\n")
    f.write("constexpr juce::Point<float> logoCentre { %.2ff, %.2ff };\nconstexpr float logoW = %.2ff;\n" % tuple(lay["logo"]))
    f.write("constexpr float labelsTopY = %.2ff, labelsMidY = %.2ff;\n" % (lay["labelsTop"], lay["labelsMid"]))
    f.write("}\n")
print("Layout.h ok")
