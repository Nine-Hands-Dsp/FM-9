# FM-9

Synthétiseur FM 6 opérateurs au format VST3 (Windows et macOS), d'après le synthé de poche **M-VAVE FM-1**, à jouer depuis un clavier maître MIDI. Moteur DX7 (32 algorithmes, compatible SysEx DX7), 6 effets, arpégiateur et séquenceur 16 pas, façade fidèle à la FM-1.

<p align="center">
  <a href="https://github.com/Nine-Hands-Dsp/fm-9/releases/latest/download/FM-9-VST3-Windows.zip"><img alt="Télécharger pour Windows" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-Windows-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/Nine-Hands-Dsp/fm-9/releases/latest/download/FM-9-VST3-macOS.zip"><img alt="Télécharger pour macOS" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-macOS-111111?style=for-the-badge&logo=apple&logoColor=white"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/fm-9/releases/latest/download/FM-9-Notice.pdf"><img alt="Télécharger la notice (PDF)" src="https://img.shields.io/badge/%F0%9F%93%96%20Notice-PDF-919496?style=for-the-badge"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/fm-9/releases/latest"><img alt="Dernière version" src="https://img.shields.io/github/v/release/Nine-Hands-Dsp/fm-9?label=derni%C3%A8re%20version&color=3c4044"></a>
</p>

![FM-9](design/apercu_home.png)

## Ce que fait FM-9

- **Moteur FM 6 opérateurs** façon DX7 : 32 algorithmes, feedback, enveloppes 4 vitesses / 4 niveaux par opérateur, enveloppe de hauteur, LFO 6 formes d'onde, échelonnage clavier, sensibilité à la vélocité, mode ratio / fixe. Cœur **msfa** (licence Apache 2.0, voir `msfa/NOTICE.md`).
- **128 presets** (001-128, banques A à D de 32) ; banque A d'usine, 32 sons originaux. Import et export de **banques SysEx DX7** (.syx de 32 voix) et de voix seules ; les banques importées sont gardées pour tous les projets.
- **12 voix** de polyphonie, mode **MONO** avec glissando (Full Time ou Fingered), pitch bend haut / bas séparés (1 à 24 demi-tons), molette de modulation et aftertouch.
- **Enveloppe ADSR** d'amplitude optionnelle (touche ENV), comme sur la FM-1.
- **6 effets** dans une chaîne réordonnable (FX + SEL) : filtre (passe-bas / passe-bande / passe-haut), reverb (room / hall / plate), delay, distorsion, chorus, phaser.
- **Arpégiateur** : Up, Down, Inclusive, Exclusive, Random, Order, Repeat ; 1 à 4 octaves ; gate, swing, latch ; synchro au tempo de l'hôte.
- **Séquenceur 16 pas** : 16 patterns, chaîne de patterns, preset associé à chaque pattern, enregistrement pas à pas (notes longues, silences), swing, gate, transposition, synchro au transport de Reaper.
- **Façade de la FM-1** : 8 potards animés (MASTER + 7 encodeurs sans fin), écran couleur (oscilloscope, schéma de l'algorithme, grille des effets, pas du séquenceur), 12 touches de fonction, clavier 27 touches jouable à la souris avec ses LED.
- **MIDI** : clavier maître sur tous les canaux ou un seul, Program Change 0-127, réception de SysEx DX7, **MIDI Learn** par clic droit sur un potard (aucun CC assigné par défaut).

## Installation

Les boutons ci-dessus téléchargent toujours la dernière version.

- **Windows** : décompresser et copier `FM-9.vst3` dans `C:\Program Files\Common Files\VST3\`.
- **macOS** : décompresser et copier `FM-9.vst3` dans `~/Library/Audio/Plug-Ins/VST3/`.

Dans Reaper, FM-9 apparaît comme instrument : **VSTi: FM-9 (NINE HANDS | DSP)**.

## Compilation

JUCE 8.0.15 + CMake : `cmake -B build && cmake --build build --config Release`. Le visuel est généré par `tools/make_art.py` (façade 3000 x 1800 et potards en calques), les sons d'usine par `tools/gen_presets.py`. `tools/render` compile le plugin sous Linux pour les tests audio et les captures.

Anti-gel v2 : rendu logiciel sous Windows, caches reconstruits par le timer, chien de garde, aucun appel à l'hôte depuis le thread audio.

## Licence

FM-9 est distribué sous **GPL-3.0** (voir `LICENSE`), pour pouvoir accueillir les firmwares libres de la FM-1. Le cœur FM msfa reste sous Apache 2.0 (compatible, voir `msfa/NOTICE.md`).

## Historique

- **1.0.0** : première version.

---
NINE HANDS | DSP
