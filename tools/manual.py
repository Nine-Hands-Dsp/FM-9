# FM-9 (NINE HANDS | DSP) - notice PDF en francais, illustree avec le rendu reel du plugin (couverture minimale commune).
# Usage : python3 tools/manual.py <dossier des captures (design/)> <fichier PDF de sortie>
import os, sys, json, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS, OUT = sys.argv[1], sys.argv[2]
VERSION = "1.0.0"
TMP = os.path.join(ROOT, "tools", "manual_assets"); os.makedirs(TMP, exist_ok=True)
FONTS = os.path.join(ROOT, "assets", "fonts")
for n, f in (("Pop", "Poppins-Regular"), ("PopL", "Poppins-Light"), ("PopM", "Poppins-Medium"), ("PopB", "Poppins-Bold"),
             ("Title", "BarlowCondensed-ExtraBold")):
    pdfmetrics.registerFont(TTFont(n, os.path.join(FONTS, f + ".ttf")))

ACC = colors.HexColor("#2E86AE"); ACC_L = colors.HexColor("#4FC3F7")        # bleu de l'ecran de FM-9
DARK = colors.HexColor("#1C2024"); GREY = colors.HexColor("#5E666E"); LIGHT = colors.HexColor("#EEF3F6")
LAY = json.load(open(os.path.join(ROOT, "tools", "layout.json")))

def shot(name): return Image.open(os.path.join(SHOTS, name)).convert("RGB")
src = shot("apercu_home.png")
k = src.height / 900.0

# ---------- capture annotee ----------
K = LAY["knobs"]; S = LAY["screen"]; KEYS = LAY["keys"]
marks = [(1, K["master"][0] - 52, K["master"][1] - 48), (2, K["select"][0] + 52, K["select"][1] - 48),
         (3, K["presets"][0] - 52, K["presets"][1] - 46), (4, K["algorithm"][0] + 52, K["algorithm"][1] - 46),
         (5, KEYS["oct1"][2] + 20, KEYS["oct1"][1] - 4), (6, S[2] + 22, S[1] - 6),
         (7, K["knob1"][0] - 58, K["knob1"][1] + 40), (8, KEYS["f0"][0] - 22, KEYS["f0"][1] - 18),
         (9, KEYS["b0"][0] - 22, KEYS["b0"][1] + 6), (10, KEYS["w0"][0] - 4, KEYS["w0"][3] + 20), (11, LAY["logo"][0] + 104, LAY["logo"][1])]
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(FONTS, "Poppins-Bold.ttf"), 26)
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 21
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(46, 134, 174), width=4)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(28, 32, 36), anchor="mm")
annotated = os.path.join(TMP, "fm9_annotated.png"); ann.save(annotated)

# ---------- les ecrans ----------
def screen_crop(name):
    im = shot(name); b = LAY["bezel"]
    return im.crop((int(b[0] * k), int(b[1] * k), int(b[2] * k), int(b[3] * k)))
def strip(parts, path, gap=16):
    s = Image.new("RGB", (sum(p.width for p in parts) + gap * (len(parts) - 1), max(p.height for p in parts)), (255, 255, 255))
    x = 0
    for p in parts: s.paste(p, (x, 0)); x += p.width + gap
    s.save(path); return path
screens = strip([screen_crop(n) for n in ("apercu_home.png", "apercu_algo.png", "apercu_fx.png")], os.path.join(TMP, "fm9_screens1.png"))
screens2 = strip([screen_crop(n) for n in ("apercu_edit.png", "apercu_seq.png")], os.path.join(TMP, "fm9_screens2.png"))

# ---------- couverture : FM-9 detouree (coins arrondis = alpha du fond), reflet au sol, halo bleu tres doux ----------
msk = np.asarray(Image.open(os.path.join(ROOT, "assets", "plugin", "background.png")).getchannel("A").resize(src.size, Image.LANCZOS))
cut = Image.new("RGBA", src.size, (0, 0, 0, 0)); cut.paste(src, (0, 0), Image.fromarray(msk))
cut = cut.crop(cut.getbbox())
cw, ch = cut.size
pad = int(cw * 0.16)
canvas = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.30) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.45
glow = np.exp(-(((xx - gcx) / (cw * 0.62)) ** 2 + ((yy - gcy) / (ch * 0.48)) ** 2)) * 26
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
halo = np.dstack([glow * 0.25, glow * 0.62, glow * 0.85, np.full_like(glow, 255)]).astype(np.uint8)
canvas = Image.alpha_composite(canvas, Image.fromarray(halo, "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM)
ra = np.array(refl).astype(np.float64)
fade = np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22
ra[..., 3] *= fade[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
canvas = canvas.crop((0, 0, canvas.width, pad + int(ch * 1.20)))
cover = os.path.join(TMP, "fm9_cover_cutout.png"); canvas.convert("RGB").save(cover)

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "tip": ParagraphStyle("tip", fontName="Pop", fontSize=9.0, leading=12.4, textColor=DARK, spaceAfter=3),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
    "cells": ParagraphStyle("cells", fontName="Pop", fontSize=7.8, leading=10, textColor=DARK),
    "cellbs": ParagraphStyle("cellbs", fontName="PopM", fontSize=7.8, leading=10, textColor=DARK),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items, s="p"): return [P("&bull;&nbsp;&nbsp;" + i, s) for i in items]
def table(rows, widths, header=True, small=False):
    data = [[P(c, ("cellbs" if small else "cellb") if (header and r == 0) else ("cells" if small else "cell")) for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#D5DEE4")),
             ("TOPPADDING", (0, 0), (-1, -1), 2 if small else 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 2 if small else 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

W = A4[0] - 36 * mm

# ---------- donnees (identiques au code livre) ----------
FACTORY = [
    ("FM PIANO", "piano électrique FM classique, attaque de marteau, dynamique au toucher"),
    ("TINE BELL", "piano électrique plus cristallin, tine brillant"),
    ("SOFT KEYS", "piano doux et rond, pour accompagner"),
    ("GRAND FM", "piano FM plus percussif et riche en harmoniques"),
    ("SOLID BASS", "basse FM ronde avec feedback, attaque nette"),
    ("SLAP BASS", "basse slappée, transitoire métallique"),
    ("SUB BASS", "basse sinusoïdale profonde"),
    ("SYNTH BASS", "basse synthé saturée par le feedback"),
    ("FM BRASS", "cuivres FM avec enveloppe de hauteur et vibrato différé"),
    ("HORN SECT", "section de cuivres plus ample"),
    ("STRINGS", "ensemble de cordes à attaque lente"),
    ("SLOW PAD", "nappe lente et large"),
    ("DRAWBARS", "orgue à tirettes (algorithme 32, six porteuses)"),
    ("PERC ORGAN", "orgue avec percussion d'attaque"),
    ("CHURCH", "orgue d'église, harmoniques graves et aiguës"),
    ("REED ORGAN", "orgue à anches nasillard"),
    ("BELLS", "cloches inharmoniques"),
    ("MARIMBA", "marimba boisé et court"),
    ("VIBES", "vibraphone avec trémolo (LFO d'amplitude)"),
    ("KALIMBA", "kalimba, attaque pincée métallique"),
    ("HARPSICHRD", "clavecin brillant"),
    ("FM CLAV", "clavinet funky"),
    ("NYLON", "guitare à cordes nylon"),
    ("PLUCK", "son pincé court pour arpèges"),
    ("SAW LEAD", "solo en dent de scie (feedback maximal)"),
    ("SQR LEAD", "solo carré creux"),
    ("FLUTE", "flûte avec souffle et vibrato"),
    ("SYNC LEAD", "solo agressif façon oscillateurs synchronisés"),
    ("GLASS PAD", "nappe de verre scintillante"),
    ("METAL HIT", "impact métallique inharmonique"),
    ("SPACE WIND", "texture spatiale avec LFO lent"),
    ("INIT VOICE", "voix vierge (un seul opérateur) pour partir de zéro"),
]

T = dict(
    lang="FRANÇAIS", sub="Synthétiseur FM 6 opérateurs",
    intro_t="Présentation",
    intro=["<b>FM-9</b> est un synthétiseur FM 6 opérateurs au format VST3, d'après le synthé de poche <b>M-VAVE FM-1</b>. "
           "On le joue depuis un clavier maître MIDI (ou à la souris sur son clavier de 27 touches). Sa façade reprend celle de la FM-1 "
           "et son fonctionnement suit le manuel de la FM-1 : mêmes potards, mêmes touches, mêmes pages à l'écran.",
           "Le moteur est celui d'un <b>DX7</b> : 6 opérateurs sinusoïdaux, 32 algorithmes, enveloppes à 4 vitesses et 4 niveaux, "
           "enveloppe de hauteur, LFO, échelonnage clavier. Il accepte directement les <b>banques SysEx DX7</b> (32 voix par banque), "
           "comme la FM-1. S'y ajoutent l'enveloppe ADSR optionnelle de la FM-1, ses 6 effets en chaîne réordonnable, "
           "un arpégiateur et un séquenceur 16 pas synchronisés au tempo de Reaper.",
           "FM-9 garde 128 presets (001 à 128, en 4 banques A à D de 32). La banque A contient 32 sons d'usine originaux ; "
           "les banques B à D sont libres pour vos voix et vos banques DX7."],
    inst_t="Installation",
    inst=[("Windows", "Décompressez l'archive et copiez <b>FM-9.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3\\</b>."),
          ("macOS", "Décompressez l'archive et copiez <b>FM-9.vst3</b> dans <b>~/Library/Audio/Plug-Ins/VST3/</b>."),
          ("Reaper", "Options &gt; Preferences &gt; Plug-ins &gt; VST : <b>Re-scan</b>. FM-9 apparaît comme instrument : "
                     "<b>VSTi: FM-9 (NINE HANDS | DSP)</b>. Insérez-le sur une piste dont l'entrée est votre clavier maître (MIDI) et armez la piste.")],
    ov_t="Vue d'ensemble",
    ov_rows=[["N°", "Commande", "Rôle"],
             ["1", "MASTER", "volume de sortie (potentiomètre 300°)"],
             ["2", "SELECT", "pages du mode courant ; en FX, emplacement de la chaîne ; appui = valider"],
             ["3", "PRESETS", "presets 001 à 128 (liste à l'écran)"],
             ["4", "ALGORITHM", "algorithme 01 à 32 (schéma à l'écran)"],
             ["5", "OCT- / OCT+", "octave du clavier ; tenu + glisser = transposition"],
             ["6", "Écran", "preset, contenu du mode, page"],
             ["7", "KNOB1 à 4", "les 4 réglages de la page affichée"],
             ["8", "Touches de fonction", "FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY/STOP, REC"],
             ["9", "Touches noires", "notes ; en EDIT : OP1-6, PIT, GLO, MONO, POLY"],
             ["10", "Touches blanches", "notes ; en SEQ : témoins des 16 pas"],
             ["11", "Logo", "NINE HANDS | DSP"]],
    scr_t="L'écran",
    scr=["L'écran est divisé en trois zones, comme sur la FM-1 : en haut, le numéro et le nom du preset (un point bleu signale une voix modifiée "
         "et pas encore sauvegardée) ; au centre, le contenu du mode ; en bas, le nom du mode et la page (ex. <b>1/2  LFO</b>). "
         "Quand vous tournez un potard, la barre du bas affiche le réglage et sa valeur (ex. <b>Cutoff: 1004Hz</b>) pendant un instant.",
         "Des fenêtres temporaires s'ouvrent au centre : liste des presets (PRESETS), schéma de l'algorithme (ALGORITHM), octave, transposition, "
         "volume, « Preset Saved ». Elles disparaissent seules après environ une seconde. Un clic sur l'écran passe à la page suivante ; "
         "la molette de la souris sur l'écran agit comme SELECT."],
    scr_cap1="Accueil (oscilloscope), schéma d'un algorithme, chaîne d'effets.",
    scr_cap2="Mode EDIT (OP1, page 2/6), séquenceur en enregistrement.",
    knobs_t="Potards",
    knobs=["Les potards se tournent en glissant verticalement (Maj = réglage fin) ou à la molette. MASTER est un potentiomètre classique "
           "(double-clic = valeur par défaut). Les sept autres sont des <b>encodeurs sans fin</b> comme sur la FM-1 : chaque cran avance "
           "le réglage d'un pas. Un clic droit sur un potard ouvre son menu (MIDI Learn du réglage qu'il commande, voir MIDI)."],
    fn_t="Touches de fonction",
    fn_rows=[["Touche", "Fonction"],
             ["FX", "chaîne des 6 effets (grille 3 x 2) ; FX à nouveau = retour à l'accueil"],
             ["SEL", "en FX : saisit l'effet choisi ; tournez SELECT pour le déplacer dans la chaîne ; SEL à nouveau pour le poser"],
             ["ENV", "active l'enveloppe ADSR et ouvre ses réglages ; ENV à nouveau la coupe"],
             ["LFO", "réglages du LFO (2 pages)"],
             ["EDIT", "édition FM : les touches noires choisissent OP1-OP6, PIT, GLO, MONO ou POLY ; EDIT à nouveau = sortie"],
             ["GLO", "réglages globaux (canal MIDI, pitch bend, vélocité, glissando, remise à zéro d'une banque)"],
             ["HOME", "écran d'accueil (oscilloscope)"],
             ["SAVE", "<b>maintenu</b> : enregistre la voix dans l'emplacement du preset courant (« Preset Saved »)"],
             ["ARP", "active l'arpégiateur et ouvre ses réglages ; ARP à nouveau l'arrête (impossible si SEQ est actif)"],
             ["SEQ", "active le séquenceur (STOP orange) ; SEQ à nouveau le coupe (impossible si ARP est actif)"],
             ["PLAY/STOP", "lance (vert) ou arrête (orange) la lecture du séquenceur"],
             ["REC", "enregistrement pas à pas (rouge) ; REC à nouveau = fin de l'enregistrement"]],
    kb_t="Clavier, octave et transposition",
    kb=["Les 27 touches (FA à SOL, deux octaves) jouent les notes à la souris ; leurs LED s'allument quand une note sonne, y compris depuis "
        "votre clavier maître, l'arpégiateur ou le séquenceur. La vélocité de la souris se règle dans GLO (page 2, VELOCITY).",
        "<b>OCT- / OCT+</b> décalent le clavier d'une octave (de -3 à +3). Leur LED est du côté du décalage : clignotement lent à ±1, rapide à ±2, "
        "fixe à ±3. <b>Tenez OCT- ou OCT+ et glissez vers le haut</b> pour transposer de 0 à -12 ou de 0 à +12 demi-tons "
        "(sur la FM-1 : OCT tenu + ALGORITHM). <b>Maj + clic</b> sur une touche OCT remet octave et transposition à zéro "
        "(les deux touches ensemble sur la FM-1). Octave et transposition s'appliquent au clavier de la façade."],
    edit_t="Mode EDIT : édition des opérateurs",
    edit=["En mode EDIT, les touches noires deviennent des sélecteurs (leurs LED clignotent, la sélection est fixe) : <b>OP1 à OP6</b> ouvrent "
          "l'opérateur (un <b>appui long</b> le coupe ou le rétablit : LED rouge), <b>PIT</b> l'enveloppe de hauteur, <b>GLO</b> les paramètres "
          "de la voix, <b>MONO</b> / <b>POLY</b> le mode de jeu (12 voix en POLY). SELECT parcourt les pages ; chaque opérateur garde sa dernière page."],
    op_rows=[["Page", "KNOB1", "KNOB2", "KNOB3", "KNOB4"],
             ["OP 1/6", "Detune (-7 à +7)", "Coarse (0-31)", "Fine (0-99)", "Out Level (0-99)"],
             ["OP 2/6", "Rate 1", "Rate 2", "Rate 3", "Rate 4"],
             ["OP 3/6", "Level 1", "Level 2", "Level 3", "Level 4"],
             ["OP 4/6", "A ModSens (0-3)", "Key Velo (0-7)", "Osc Mode (Ratio / Fixed)", "-"],
             ["OP 5/6", "L Depth (0-99)", "R Depth (0-99)", "Breakpoint (A-1 à C7)", "-"],
             ["OP 6/6", "L Curve", "R Curve", "Ratescale (0-7)", "-"],
             ["PIT 1/2", "Rate 1", "Rate 2", "Rate 3", "Rate 4"],
             ["PIT 2/2", "Level 1", "Level 2", "Level 3", "Level 4"],
             ["GLO 1/2", "Algorithm (1-32)", "Feedback (0-7)", "Osc Sync", "Transpose (0-48)"],
             ["GLO 2/2", "Init Voice", "Rename", "Copy Voice", "-"]],
    edit2=["<b>Init Voice</b> : tournez KNOB1 dans le sens horaire pour repartir d'une voix vierge. <b>Rename</b> : KNOB2 horaire ouvre la saisie du nom "
           "(10 caractères). <b>Copy Voice</b> : KNOB3 choisit la voix source parmi les 128 presets, puis un <b>appui sur SELECT</b> la copie dans la voix en cours. "
           "Pensez à maintenir SAVE pour garder vos modifications."],
    pages_t="Pages des autres modes",
    pages_rows=[["Mode", "KNOB1", "KNOB2", "KNOB3", "KNOB4"],
                ["ENV", "Attack", "Decay", "Sustain", "Release"],
                ["LFO 1/2", "Wave", "Speed (0-99)", "PMD (0-99)", "AMD (0-99)"],
                ["LFO 2/2", "Delay (0-99)", "Pitch Sens (0-7)", "LFO Sync", "Osc Sync"],
                ["GLO 1/2", "MIDI Ch (ALL / 1-16)", "Effect Ch (-)", "Bend Up (1-24)", "Bend Down (1-24)"],
                ["GLO 2/2", "Reset (banque A-D)", "Velocity (1-127)", "Glide (Full Time / Fingered)", "Glide Val (0-100)"],
                ["ARP 1/2", "Mode", "Octave (1-4)", "Rate", "Tempo (30-300 BPM)"],
                ["ARP 2/2", "Gate (20-100 %)", "Swing (50-75 %)", "Latch", "Sync"],
                ["SEQ 1/3", "Pattern (1-16)", "Clear", "Chain (1-16)", "Step (1-16)"],
                ["SEQ 2/3", "Voice (preset du pattern)", "Rate", "Tempo (30-300 BPM)", "Gate (20-100 %)"],
                ["SEQ 3/3", "Swing (50-75 %)", "Sync", "Transpose (-24 à +24)", "-"]],
    pages_note=["<b>Reset</b> (GLO 2/2) : KNOB1 choisit la banque, un appui sur SELECT la remet à zéro (banque A : sons d'usine). "
                "Le glissando ne fonctionne qu'en MONO ; en <b>Fingered</b>, il ne glisse qu'en jeu lié (touche précédente encore tenue)."],
    fx_t="Effets",
    fx=["Le mode FX montre les 6 emplacements de la chaîne (cadre clair = effet actif, point = emplacement choisi). SELECT choisit l'emplacement, "
        "KNOB1 à 3 règlent l'effet, <b>KNOB4 l'allume (sens horaire) ou l'éteint (sens anti-horaire)</b>. Pour changer l'ordre : SEL, SELECT, SEL. "
        "Les réglages d'effets sont enregistrés avec le projet."],
    fx_rows=[["Effet", "KNOB1", "KNOB2", "KNOB3"],
             ["Filter", "Type : Low Pass / Band Pass / High Pass", "Cutoff : 100 Hz à 20 kHz", "Q (résonance) 0-100"],
             ["Reverb", "Type : Room / Hall / Plate", "Decay 0-100 %", "Mix 0-100 %"],
             ["Delay", "Decay (réinjection) 0-100 %", "Rate (temps) 10 ms à 2 s", "Mix 0-100 %"],
             ["Distortion", "Gain 0-100 %", "Tone 0-100 %", "Level 0-100 %"],
             ["Chorus", "Freq 0-100 %", "Depth 0-100 %", "Mix 0-100 %"],
             ["Phaser", "Freq 0-100 %", "Depth 0-100 %", "Mix 0-100 %"]],
    fx2=["Clic droit &gt; <b>Effects</b> : remettre l'ordre d'origine (Filter, Reverb, Delay, Distortion, Chorus, Phaser), "
         "et caler le delay sur le tempo (Delay synced to tempo, division de 1/32 à 1/1)."],
    arp_t="Arpégiateur",
    arp=["ARP allume l'arpégiateur : tenez une ou plusieurs touches (clavier maître ou façade), il les joue selon le mode choisi. "
         "Modes : <b>Up</b>, <b>Down</b>, <b>Inclusive</b> (montée puis descente, extrémités répétées), <b>Exclusive</b> (sans répéter les extrémités), "
         "<b>Random</b>, <b>Order</b> (ordre de jeu), <b>Repeat</b> (chaque note deux fois). <b>Latch</b> garde l'arpège après avoir lâché les touches ; "
         "un nouvel accord le remplace. <b>Sync</b> suit le tempo de Reaper (sinon le Tempo de la page 1)."],
    seq_t="Séquenceur",
    seq=["SEQ active le séquenceur : la barre du bas indique <b>Sequencer</b>, les 16 touches blanches deviennent les témoins des pas "
         "(allumée = note, clignotante = pas joué). PLAY/STOP lance la lecture ; avec <b>Sync</b>, le séquenceur suit le transport de Reaper.",
         "<b>Enregistrer</b> : REC (rouge), puis jouez : chaque note (ou accord en mode Poly) remplit le pas suivant. "
         "<b>Note longue</b> : tenez la note et tournez SELECT, le pas suivant prolonge la note. "
         "<b>Silences et corrections</b> : tenez REC et tournez SELECT pour aller à un pas, puis jouez la note qui le remplace. REC à nouveau termine l'enregistrement.",
         "16 patterns indépendants (Pattern), effacement (Clear, KNOB2 horaire), <b>Chain</b> : enchaîne N patterns à partir du pattern courant. "
         "<b>Step</b> : nombre de pas joués. <b>Voice</b> : preset associé au pattern ; quand SEQ est actif, PRESETS est désactivé et chaque pattern charge sa voix. "
         "Patterns et voix associées sont enregistrés avec le projet."],
    midi_t="MIDI",
    midi=["FM-9 reçoit le clavier maître sur tous les canaux ou sur un seul (GLO 1/2, MIDI Ch) : notes et vélocité, pitch bend (plages haut et bas "
          "séparées), molette de modulation (vibrato), aftertouch, pédale de sustain (CC 64), Program Change 0-127 (presets 001-128) "
          "et <b>SysEx DX7</b> (voix seule ou banque de 32 voix, chargée dans la banque du preset courant)."],
    learn=["<b>MIDI Learn</b> : clic droit sur un potard &gt; MIDI Learn, puis bougez la commande MIDI. Pour KNOB1 à 4, c'est le réglage affiché "
           "à ce moment qui est appris. Aucun CC n'est assigné d'usine.",
           "La course réelle de votre pédale d'expression est apprise automatiquement ; les réglages continus suivent la pédale sans à-coups.",
           "Clic droit &gt; MIDI &gt; <b>Clear all MIDI assignments</b> efface toutes les assignations."],
    pre_t="Presets et banques",
    pre=["Tournez PRESETS ou passez par clic droit &gt; <b>Load preset</b> (banques A à D). Les voix modifiées s'enregistrent en maintenant SAVE "
         "(emplacement courant) ou par clic droit &gt; <b>Save preset to...</b> (tambour de sélection). "
         "Clic droit &gt; Bank X &gt; <b>Load DX7 SysEx bank</b> charge un fichier .syx de 32 voix (par exemple les cartouches d'origine du DX7) ; "
         "<b>Save bank as SysEx</b> exporte une banque. Les banques modifiées sont gardées pour tous vos projets "
         "(dossier Nine Hands DSP/FM-9 de votre compte utilisateur).",
         "Banque A d'usine :"],
    fac_hdr=["N°", "Nom", "Caractère"],
    win_t="Fenêtre et réglages",
    win=["La fenêtre s'ouvre à la taille « 60 % » et ne descend jamais en dessous. Clic droit &gt; <b>Plugin size</b> : 60 %, 75 %, 100 % ou 125 %, "
         "et taille par défaut des nouvelles fenêtres. Clic droit &gt; <b>Glow</b> : intensité des LED des touches (25, 50, 75 ou 100 %). "
         "Les menus s'ouvrent toujours là où vous cliquez."],
    tips_t="Conseils",
    tips=["Créez une piste par son et armez-la avec votre clavier maître en entrée MIDI ; FM-9 se joue comme n'importe quel instrument de Reaper.",
          "Chargez les banques ROM du DX7 dans B, C ou D : la plupart des sons emblématiques des années 80 y sont.",
          "Les effets de la FM-1 « colorent » le son : pour un DX7 pur, éteignez-les (FX, KNOB4 anti-horaire sur chaque effet).",
          "Pour les basses jouées en legato, passez en MONO (EDIT, touche MONO) avec Glide en Fingered.",
          "Le séquenceur en Sync suit la lecture de Reaper : lancez la lecture du projet pour démarrer les patterns."],
    tech_t="Caractéristiques techniques",
    tech=[["Format", "VST3 instrument, Windows (64 bits) et macOS (Intel et Apple Silicon)"],
          ["Moteur", "FM 6 opérateurs façon DX7, 32 algorithmes, cœur msfa (Apache 2.0), 12 voix, sortie mono recopiée en stéréo"],
          ["Presets", "128 (4 banques de 32), banque A d'usine, SysEx DX7 en import et export"],
          ["Effets", "Filter, Reverb, Delay, Distortion, Chorus, Phaser, ordre réglable"],
          ["Séquences", "arpégiateur 7 modes ; séquenceur 16 pas, 16 patterns, chaîne, notes longues, swing, synchro hôte"],
          ["MIDI", "canal réglable, Program Change, pitch bend, molette, aftertouch, sustain, SysEx DX7, MIDI Learn"],
          ["Fenêtre", "60 / 75 / 100 / 125 %, jamais sous 60 % ; anti-gel v2 sous Windows"],
          ["Licence", "GPL-3.0"]],
)

COVER_TEXT = {}
LOGO_PNG = os.path.join(TMP, "nine_hands_logo_blanc.png")
subprocess.run(["rsvg-convert", "-w", "1200", os.path.join(ROOT, "assets", "plugin", "NineHandsLogoWhite.svg"), "-o", LOGO_PNG], check=True)

def section():
    t = T; f = []
    f.append(NextPageTemplate("p"))
    COVER_TEXT.update(lang=t["lang"], sub=t["sub"])
    f.append(Spacer(1, 1)); f.append(PageBreak())
    f.append(P(t["intro_t"], "h1")); f += [P(x) for x in t["intro"]]
    f.append(P(t["inst_t"], "h1"))
    for h, x in t["inst"]: f.append(P(h, "h2")); f.append(P(x))
    f.append(PageBreak())
    f.append(P(t["ov_t"], "h1")); f.append(img(annotated, W)); f.append(Spacer(1, 4 * mm))
    f.append(table(t["ov_rows"], [10 * mm, 38 * mm, W - 48 * mm], small=True))
    f.append(PageBreak())
    f.append(P(t["scr_t"], "h1")); f += [P(x) for x in t["scr"]]
    f.append(img(screens, W)); f.append(P(t["scr_cap1"], "small")); f.append(Spacer(1, 3 * mm))
    f.append(img(screens2, W * 0.66)); f.append(P(t["scr_cap2"], "small"))
    f.append(P(t["knobs_t"], "h1")); f += [P(x) for x in t["knobs"]]
    f.append(P(t["fn_t"], "h1")); f.append(table(t["fn_rows"], [26 * mm, W - 26 * mm], small=True))
    f.append(P(t["kb_t"], "h1")); f += [P(x) for x in t["kb"]]
    f.append(P(t["edit_t"], "h1")); f += [P(x) for x in t["edit"]]
    cw = (W - 18 * mm) / 4
    f.append(KeepTogether([table(t["op_rows"], [18 * mm, cw, cw, cw, cw], small=True)])); f.append(Spacer(1, 2 * mm))
    f += [P(x) for x in t["edit2"]]
    f.append(KeepTogether([P(t["pages_t"], "h1"), table(t["pages_rows"], [18 * mm, cw, cw, cw, cw], small=True)])); f.append(Spacer(1, 2 * mm))
    f += [P(x) for x in t["pages_note"]]
    cf = (W - 22 * mm) / 3
    f.append(KeepTogether([P(t["fx_t"], "h1")] + [P(x) for x in t["fx"]] + [table(t["fx_rows"], [22 * mm, cf, cf, cf], small=True)])); f.append(Spacer(1, 2 * mm))
    f += [P(x) for x in t["fx2"]]
    f.append(P(t["arp_t"], "h1")); f += [P(x) for x in t["arp"]]
    f.append(P(t["seq_t"], "h1")); f += [P(x) for x in t["seq"]]
    f.append(P(t["midi_t"], "h1")); f += [P(x) for x in t["midi"]]; f += bullets(t["learn"])
    f.append(P(t["pre_t"], "h1")); f += [P(x) for x in t["pre"]]
    rows = [t["fac_hdr"]] + [[str(i + 1).zfill(3), "<b>" + n + "</b>", d] for i, (n, d) in enumerate(FACTORY)]
    f.append(table(rows, [12 * mm, 30 * mm, W - 42 * mm], small=True))
    f.append(P(t["win_t"], "h1")); f += [P(x) for x in t["win"]]
    f.append(KeepTogether([P(t["tips_t"], "h1")] + bullets(t["tips"], "tip") +
                          [P(t["tech_t"], "h1"), table(t["tech"], [28 * mm, W - 28 * mm], header=False, small=True)]))
    return f

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "FM-9 (NINE HANDS | DSP) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="FM-9 - Notice v" + VERSION, author="NINE HANDS | DSP")
# ---- Couverture « minimale » : gabarit fixe commun a toutes les notices (mm depuis le haut, logo et bandeau depuis le bas) ----
COVER_NAME = "FM-9"
PEDAL_H, PEDAL_MAXW, PEDAL_TOP = 82 * mm, 115 * mm, 84 * mm
LANG_TOP = 18 * mm
NAME_TOP, NAME_PT = 194 * mm, 30
SUB_TOP, SUB_PT = 208 * mm, 10
LOGO_W, LOGO_BOTTOM = 38 * mm, 22 * mm
BAND_H = 3 * mm
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, 0, A4[0], BAND_H, stroke=0, fill=1)
    def line(text, top, font, size, cs, col):
        w = pdfmetrics.stringWidth(text, font, size) + cs * (len(text) - 1)
        t = c.beginText((A4[0] - w) / 2, A4[1] - top - size * 0.8); t.setFont(font, size); t.setCharSpace(cs)
        t.setFillColor(col); t.textOut(text); c.drawText(t)
    line(COVER_TEXT["lang"], LANG_TOP, "PopM", 8.5, 0, ACC_L)
    ci = Image.open(cover); iw, ih = ci.size
    cw_ = iw / 1.32; ch_ = (ih - int(cw_ * 0.16)) / 1.2; pad_ = int(cw_ * 0.16)
    kk = min(PEDAL_H / ch_, PEDAL_MAXW / cw_)
    dw, dh = iw * kk, ih * kk
    y0 = A4[1] - (PEDAL_TOP + (PEDAL_H - ch_ * kk) / 2) + pad_ * kk
    c.drawImage(cover, (A4[0] - dw) / 2, y0 - dh, dw, dh)
    line(COVER_NAME, NAME_TOP - NAME_PT * 0.02, "Title", NAME_PT, NAME_PT * 0.5, colors.white)
    line(COVER_TEXT["sub"], SUB_TOP, "Pop", SUB_PT, 1.5, colors.HexColor("#BDC4B8"))
    lw, lh = Image.open(LOGO_PNG).size
    c.drawImage(LOGO_PNG, (A4[0] - LOGO_W) / 2, LOGO_BOTTOM, LOGO_W, LOGO_W * lh / lw, mask="auto")
    c.restoreState()
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
doc.build(section())
print(OUT)
