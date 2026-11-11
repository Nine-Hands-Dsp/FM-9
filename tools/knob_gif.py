#!/usr/bin/env python3
"""Apercu GIF des potards (skill potards-nine-hands-dsp) : calques solidaires, flou de mouvement selon la vitesse,
ombre fixe a 45 deg vers le bas a droite, reflets fixes. Net a gauche, encodeur en rotation a droite."""
import os, math
import numpy as np
from PIL import Image
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
A = os.path.join(ROOT, "assets", "plugin")
S = 260
def L(n): return Image.open(os.path.join(A, "knob_%s.png" % n)).resize((S, S), Image.LANCZOS)
sh, cr, cap, mk, hl = [L(n) for n in ["shadow", "crown", "cap", "marker", "highlight"]]
stack = Image.alpha_composite(Image.alpha_composite(cr, cap), mk)
stackE = Image.alpha_composite(cr, cap)
bg = Image.new("RGBA", (S * 2 + 40, S + 40), (222, 214, 205, 255))
def frame_knob(st, ang, span, dirn):
    n = max(1, min(16, int(math.ceil(span / 3.0)) + 1)) if span > 0.05 else 1
    acc = np.zeros((S, S, 4), np.float64)
    acs = np.zeros((S, S, 4), np.float64)
    for k in range(n):
        a = ang - dirn * span * (k / max(1, n - 1))
        acc += np.asarray(st.rotate(-a, Image.BICUBIC), np.float64)
        acs += np.asarray(sh.rotate(-a, Image.BICUBIC), np.float64)
    return Image.fromarray((acc / n).astype(np.uint8)), Image.fromarray((acs / n).astype(np.uint8))
frames = []; ang = -150.0; angE = 0.0; prev = ang; prevE = 0.0; span = 0; spanE = 0
for f in range(150):
    t = f / 30.0
    target = -150 + 300 * (0.5 - 0.5 * math.cos(t * 1.6))             # MASTER : aller-retour lent puis rapide
    speed = 6 if t < 2.5 else 28
    angE += speed if (f // 40) % 2 == 0 else -speed * 1.6              # encodeur : lent puis tres rapide
    v = target - prev; prev = target; span = max(min(abs(v), 40), span * 0.6); span = 0 if span < 0.1 else span
    vE = angE - prevE; prevE = angE; spanE = max(min(abs(vE), 40), spanE * 0.6)
    im = bg.copy()
    for i, (st, a, sp, vv) in enumerate([(stack, target, span, v), (stackE, angE, spanE, vE)]):
        k, s = frame_knob(st, a, sp, 1 if vv >= 0 else -1)
        x = 20 + i * (S + 0)
        d = int(S * 0.03)
        im.alpha_composite(s, (x + d, 20 + d)); im.alpha_composite(k, (x, 20)); im.alpha_composite(hl, (x, 20))
    frames.append(im.convert("P", palette=Image.ADAPTIVE))
out = os.path.join(ROOT, "design", "potards.gif")
frames[0].save(out, save_all=True, append_images=frames[1:], duration=33, loop=0)
print(out)
