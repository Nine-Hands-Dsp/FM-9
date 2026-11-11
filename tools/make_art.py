#!/usr/bin/env python3
"""FM-9 (NINE HANDS | DSP) - facade 3000 x 1800 d'apres le mockup FM-9_mockup_Design_v001.svg.

Tout est rendu en procedural a la taille finale (aucun agrandissement d'image) :
coque creme mate, pave de touches en silicone, touches, ecran (vitre vide),
eclairage a 45 deg venant du haut a gauche. Les potards, textes et logos ne sont
pas dans le fond : ce sont des calques animes / du vectoriel dessines par le plugin.

Sorties : assets/plugin/skin_<coloris>.jpg, assets/plugin/knob_*.png, Layout.h, tools/layout.json
"""
import json, os
import numpy as np
import cv2
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(ROOT, "assets", "plugin")
os.makedirs(OUT, exist_ok=True)

W, H = 3000, 1800
rng = np.random.default_rng(9)

# ------------------------------------------------------------------ geometrie : mockup FM-9_mockup_Design_v001.svg (memes coordonnees, 3000 x 1800)
def RR(x, y, w, h):
    return [x, y, x + w, y + h]
SHELL = RR(0, 0, 3000, 1800); SHELL_R = 152.43; SHELL_BEVEL = 40.0          # biseau plat de 40 px (calque Layer_8)
KB_PAD = RR(110, 1034, 2780, 656); KB_PAD_R = 76.52
FN_PAD = RR(1666, 495, 1224, 430); FN_PAD_R = 53.42
OCT_PAD = RR(188, 759.25, 446, 165.5); OCT_PAD_R = 42.36
SCREEN_BEZEL = RR(744.1, 117.04, 808, 808); BEZEL_R = 37.69                 # ecran (114944)
SCREEN_GLASS = RR(744.1 + 18, 117.04 + 18, 808 - 36, 808 - 36); GLASS_R = 22.0
KEY_W, KEY_H, KEY_R = 120.0, 266.0, 60.0
BLACK_X = [264, 432, 600, 936, 1104, 1440, 1608, 1776, 2112, 2280, 2616]; BLACK_Y = 1081.5
WHITE_X = [180 + 168 * i for i in range(16)]; WHITE_Y = 1376.5
FN_X = [1721.75, 1916.25, 2110.75, 2305.25, 2499.75, 2694.25]; FN_Y = [542.75, 737.25]; FN_S = 140.0; FN_R = 19.1
OCT_KEYS = [RR(243.75, 807, 140, 70), RR(432.25, 807, 140, 70)]; OCT_R = 19.1
KNOBS = {"master": (256, 290), "select": (566, 290), "presets": (256, 555.42), "algorithm": (566, 555.42),
         "knob1": (1813, 290), "knob2": (2123, 290), "knob3": (2433, 290), "knob4": (2743, 290)}
KNOB_D = 135.0                # diametre de la couronne
LOGO_NH = (1335, 1709, 1665, 1739)      # NINE HANDS | DSP (bas, centre)
LOGO_FM9 = (2360, 75, 2778, 119)        # FM-9 (haut droite)

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

# ------------------------------------------------------------------ skins : coloris officiels de la FM-1 (m-vave.com)
# shell = coque, pad = cuvettes en silicone, keys = touches en silicone (memes valeurs dans PluginEditor.cpp)
SKINS = {
    "black":      dict(shell=[61, 61, 60],    pad=[48, 48, 50],    keys=[30, 31, 34]),
    "purple":     dict(shell=[180, 140, 198], pad=[104, 94, 158],  keys=[122, 111, 178]),
    "orange":     dict(shell=[190, 107, 74],  pad=[124, 74, 63],   keys=[145, 87, 74]),
    "blackgreen": dict(shell=[70, 69, 72],    pad=[98, 178, 166],  keys=[121, 209, 196]),
    "coolgray":   dict(shell=[226, 218, 209], pad=[60, 64, 68],    keys=[30, 31, 34]),
    "whiteblue":  dict(shell=[238, 235, 229], pad=[72, 99, 124],   keys=[86, 117, 146]),
}

def chamfer(sd, bw, slope):
    # biseau plat : rampe lineaire de largeur bw puis dessus plat
    return np.clip(-sd / bw, 0, 1) * bw * slope

def shifted(a, s):
    o = np.zeros_like(a); s = int(round(s))
    o[s:, s:] = a[:-s, :-s]
    return o

def render_background(SKIN_ID, SK):
    global img, key_rects, alpha
    flat = float(np.clip(np.array([0, 0, 1.0]) @ L, 0, 1))
    def shade(h):
        lam, spec, _ = shade_from_height(h, 1.0)
        return 0.35 + 0.65 * lam / flat, spec
    def drop(m, off, sig, k, aoSig, aoK):
        # ombre portee a 45 deg vers le bas a droite + occlusion de contact, posees avant l'element
        global img
        img *= (1 - k * np.clip(shifted(blur(m, sig), off), 0, 1))[..., None]
        img *= (1 - aoK * np.clip(blur(m, aoSig), 0, 1))[..., None]

    # ------------------------------------------------------------------ coque (biseau plat de 40 px)
    sd_shell = sdf_rrect(SHELL, SHELL_R)
    alpha = cov(sd_shell)
    s, sp = shade(chamfer(sd_shell, SHELL_BEVEL, 0.30))
    img = np.array(SK["shell"], np.float32) * s[..., None] + (sp ** 40 * 10)[..., None]
    img += (grain(0.8, 1.8, 1) + grain(10, 0.5, 2))[..., None]

    # ------------------------------------------------------------------ paves en silicone (biseau plat, poses sur la coque)
    rubber = np.array(SK["pad"], np.float32)
    def plate(r, rad, col, bw=16.0, slope=0.55, off=9, sig=11, k=0.34, aoSig=4.0, aoK=0.22):
        global img
        sd = sdf_rrect(r, rad); m = cov(sd)
        drop(m, off, sig, k, aoSig, aoK)
        s, sp = shade(chamfer(sd, bw, slope))
        fill = np.asarray(col, np.float32) * s[..., None] + (sp ** 40 * 12)[..., None]
        img = img * (1 - m[..., None]) + fill * m[..., None]
        return m
    plate(KB_PAD, KB_PAD_R, rubber, 18.0)
    plate(FN_PAD, FN_PAD_R, rubber, 16.0)
    plate(OCT_PAD, OCT_PAD_R, rubber, 13.0)

    # ------------------------------------------------------------------ touches (biseau plat, rainure de la LED)
    keyH = np.zeros((H, W), np.float32); keyM = np.zeros((H, W), np.float32)
    key_rects = {}
    def key(name, r, rad, bw, slot=None):
        sd = sdf_rrect(r, rad)
        h = chamfer(sd, bw, 0.6) + 2.0
        if slot is not None:
            sds = sdf_rrect(slot, (slot[2] - slot[0]) / 2)
            h -= np.clip(1 - (sds + 2.5) / 4.0, 0, 1) * 3.0 * (sd < 0)
        np.maximum(keyH, h * (sd < 1.5), out=keyH)
        np.maximum(keyM, cov(sd), out=keyM)
        key_rects[name] = r
    for i, x in enumerate(WHITE_X):
        r = RR(x, WHITE_Y, KEY_W, KEY_H); cx = x + KEY_W / 2; cy = WHITE_Y + KEY_H / 2
        key("w%d" % i, r, KEY_R, 14.0, [cx - 6, cy - 55, cx + 6, cy + 55])
    for i, x in enumerate(BLACK_X):
        r = RR(x, BLACK_Y, KEY_W, KEY_H); cx = x + KEY_W / 2; cy0 = BLACK_Y + 0.22 * KEY_H
        key("b%d" % i, r, KEY_R, 14.0, [cx - 6, cy0, cx + 6, cy0 + 70])
    for j, y in enumerate(FN_Y):
        for i, x in enumerate(FN_X):
            key("f%d" % (j * 6 + i), RR(x, y, FN_S, FN_S), FN_R, 12.0)
    for i, r in enumerate(OCT_KEYS):
        key("oct%d" % i, r, OCT_R, 10.0)
    drop(keyM, 12, 12, 0.50, 4.0, 0.30)
    s, sp = shade(keyH)
    k = np.array(SK["keys"], np.float32) * s[..., None] + (sp ** 30 * 22)[..., None]
    k += (grain(0.7, 1.6, 3))[..., None]
    img = img * (1 - keyM[..., None]) + k * keyM[..., None]

    # ------------------------------------------------------------------ occlusion de contact sous les potards (l'ombre portee est un calque du potard)
    for (x, y) in KNOBS.values():
        d = np.hypot(xx - x, yy - y)
        mk = np.clip(KNOB_D / 2 - d + 0.5, 0, 1)
        img *= (1 - 0.40 * np.clip(blur(mk, 5.0) - mk, 0, 1) * 1.6)[..., None]

    # ------------------------------------------------------------------ ecran (cadre noir a biseau plat, vitre)
    sdb = sdf_rrect(SCREEN_BEZEL, BEZEL_R); mb = cov(sdb)
    drop(mb, 14, 16, 0.45, 5.0, 0.28)
    s, sp = shade(chamfer(sdb, 12.0, 0.6))
    bcol = np.array([20, 21, 24], np.float32) * s[..., None] + (sp ** 40 * 40)[..., None]
    img = img * (1 - mb[..., None]) + bcol * mb[..., None]
    sdg = sdf_rrect(SCREEN_GLASS, GLASS_R); mg = cov(sdg)
    g = np.array([7, 8, 10], np.float32) + np.zeros((H, W, 3), np.float32)
    gx0, gy0 = SCREEN_GLASS[0], SCREEN_GLASS[1]
    diag = ((xx - gx0) + (yy - gy0)) / (SCREEN_GLASS[2] - gx0 + SCREEN_GLASS[3] - gy0)
    g += (np.clip(1 - diag * 1.6, 0, 1) ** 2 * 10)[..., None]
    img = img * (1 - mg[..., None]) + g * mg[..., None]

    # ------------------------------------------------------------------ eclairage d'ambiance a 45 deg (fond seulement)
    dn = (xx + yy) / (W + H)
    gain = 1.10 - 0.24 * dn
    band = np.exp(-((dn - 0.30) / 0.10) ** 2) * 10.0
    img = img * gain[..., None] + band[..., None]
    return img


# ------------------------------------------------------------------ decomposition pour les couleurs a la volee
# Le rendu est affine en chaque couleur : img = A + Ms * coque + Mp * paves + Mk * touches
# (les reperes moules prennent la couleur de la coque assombrie). Le plugin recompose le fond
# avec n'importe quelle couleur a partir de ces cartes, sans aucune image figee par coloris.
Z = [0, 0, 0]
def run(shell=Z, pad=Z, keys=Z):
    return render_background(None, dict(shell=shell, pad=pad, keys=keys))
T = 100.0
A  = run()
Ms = (run(shell=[T] * 3) - A)[..., 0] / T
Mp = (run(pad=[T] * 3) - A)[..., 0] / T
Mk = (run(keys=[T] * 3) - A)[..., 0] / T
print("A", A.min(), A.max(), "Ms", Ms.min(), Ms.max(), "Mp", Mp.min(), Mp.max(), "Mk", Mk.min(), Mk.max())
A_OFF, M_SC = 64.0, 160.0           # A + 64 en 8 bits ; cartes x 160 (jusqu'a 1,59)
Image.fromarray(np.clip(np.rint(A + A_OFF), 0, 255).astype(np.uint8), "RGB").save(os.path.join(OUT, "face_base.png"), optimize=True)
Image.fromarray(np.clip(np.rint(np.dstack([Ms, Mp, Mk]) * M_SC), 0, 255).astype(np.uint8), "RGB").save(os.path.join(OUT, "face_maps.png"), optimize=True)
Image.fromarray((alpha * 255).astype(np.uint8), "L").save(os.path.join(ROOT, "tools", "shell_alpha.png"), optimize=True)
# controle : recomposition d'un coloris contre le rendu direct
for sid in ("coolgray", "purple"):
    sk = SKINS[sid]
    ref = run(sk["shell"], sk["pad"], sk["keys"])
    Aq = np.rint(A + A_OFF) - A_OFF; M = np.rint(np.dstack([Ms, Mp, Mk]) * M_SC) / M_SC
    rec = Aq + M[..., 0:1] * np.array(sk["shell"]) + M[..., 1:2] * np.array(sk["pad"]) + M[..., 2:3] * np.array(sk["keys"])
    err = np.abs(np.clip(rec, 0, 255) - np.clip(ref, 0, 255))
    print(sid, "erreur moyenne %.2f max %.1f" % (err.mean(), err.max()))
    Image.fromarray(np.clip(rec, 0, 255).astype(np.uint8)).save("/tmp/sk/rec_%s.png" % sid)

# ------------------------------------------------------------------ potards : calques fournis par l'utilisateur (800 px, couronne au bord du cadre)
# poses au centre d'un cadre de 1024 px (marge pour l'ombre portee) : le potard occupe 800 / 1024 du composant
KSRC = os.path.join(ROOT, "assets", "knob_src")
S = 1024; KF = 800.0 / S
def pad1024(name):
    src = Image.open(os.path.join(KSRC, name)).convert("RGBA")
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0)); out.paste(src, ((S - 800) // 2, (S - 800) // 2))
    return out
for old in ("cap", "marker"):
    p = os.path.join(OUT, "knob_%s.png" % old)
    if os.path.exists(p): os.remove(p)
pad1024("knob_02_soft_touch_grey_center_fixed.png").save(os.path.join(OUT, "knob_base.png"), optimize=True)
crown = pad1024("knob_03_silver_coarse_grip_crown_rotating.png"); crown.save(os.path.join(OUT, "knob_crown.png"), optimize=True)
pad1024("knob_03_silver_coarse_grip_reflect_static.png").save(os.path.join(OUT, "knob_highlight.png"), optimize=True)
pad1024("indicator_04_dot_warm_white.png").save(os.path.join(OUT, "knob_indicator.png"), optimize=True)
# ombre portee centree (le decalage a 45 deg vers le bas a droite est ajoute par le code)
ca = np.asarray(crown, np.float32)[..., 3] / 255.0
yk, xk = np.mgrid[0:S, 0:S]; disk = (np.hypot(xk - (S - 1) / 2, yk - (S - 1) / 2) < 400).astype(np.float32)
sh0 = np.maximum(ca, disk)
sh = cv2.GaussianBlur(sh0, (0, 0), 22) * 0.55 + cv2.GaussianBlur(sh0, (0, 0), 6) * 0.35
Image.fromarray(np.dstack([np.zeros((S, S, 3), np.uint8), (np.clip(sh, 0, 0.85) * 255).astype(np.uint8)]), "RGBA").save(os.path.join(OUT, "knob_shadow.png"), optimize=True)
print("potards ok")

# ------------------------------------------------------------------ repere du plugin (image / 2 = 1500 x 900 unites)
U = 0.5
def U4(r):
    return [round(v * U, 2) for v in r]
lay = {
    "image": [W, H], "units": [W * U, H * U],
    "shell": U4(SHELL), "shellR": round(SHELL_R * U, 2),
    "screen": U4(SCREEN_GLASS), "bezel": U4(SCREEN_BEZEL),
    "knobs": {k: [round(v[0] * U, 2), round(v[1] * U, 2)] for k, v in KNOBS.items()},
    "knobR": round(KNOB_D / 2 * U, 2), "knobD": round(KNOB_D * U / KF, 2),     # composant = cadre de 1024 px
    "keys": {k: U4(v) for k, v in key_rects.items()},
    "logoNH": U4(LOGO_NH), "logoFM9": U4(LOGO_FM9),
}
with open(os.path.join(ROOT, "tools", "layout.json"), "w") as f:
    json.dump(lay, f, indent=1)

def rect(v):
    return "juce::Rectangle<float> { %.2ff, %.2ff, %.2ff, %.2ff }" % (v[0], v[1], v[2] - v[0], v[3] - v[1])
with open(os.path.join(ROOT, "Layout.h"), "w") as f:
    f.write("// FM-9 - reperes de la facade (genere par tools/make_art.py d'apres FM-9_mockup_Design_v001.svg), repere 1500 x 900 unites\n#pragma once\n#include <JuceHeader.h>\n\nnamespace fm9::layout\n{\n")
    f.write("constexpr float unitsW = %.1ff, unitsH = %.1ff;\n" % (W * U, H * U))
    f.write("inline const juce::Rectangle<float> shell = %s;\nconstexpr float shellR = %.2ff;\n" % (rect(lay["shell"]), lay["shellR"]))
    f.write("inline const juce::Rectangle<float> screen = %s;\n" % rect(lay["screen"]))
    f.write("inline const juce::Rectangle<float> bezel = %s;\n" % rect(lay["bezel"]))
    f.write("constexpr float knobD = %.2ff, knobR = %.2ff;\n" % (lay["knobD"], lay["knobR"]))
    for k2, v in lay["knobs"].items():
        f.write("constexpr juce::Point<float> knob_%s { %.2ff, %.2ff };\n" % (k2, v[0], v[1]))
    f.write("inline const juce::Rectangle<float> whiteKeys[16] = {\n" + ",\n".join("    " + rect(lay["keys"]["w%d" % i]) for i in range(16)) + " };\n")
    f.write("inline const juce::Rectangle<float> blackKeys[11] = {\n" + ",\n".join("    " + rect(lay["keys"]["b%d" % i]) for i in range(11)) + " };\n")
    f.write("inline const juce::Rectangle<float> fnKeys[12] = {\n" + ",\n".join("    " + rect(lay["keys"]["f%d" % i]) for i in range(12)) + " };\n")
    f.write("inline const juce::Rectangle<float> octKeys[2] = {\n" + ",\n".join("    " + rect(lay["keys"]["oct%d" % i]) for i in range(2)) + " };\n")
    f.write("inline const juce::Rectangle<float> logoNH = %s;\n" % rect(lay["logoNH"]))
    f.write("inline const juce::Rectangle<float> logoFM9 = %s;\n" % rect(lay["logoFM9"]))
    f.write("}\n")
print("Layout.h ok")
