# Coeur FM msfa

Les fichiers de ce dossier proviennent du moteur **msfa** (Music Synthesizer for Android, Google, 2012),
dans la version maintenue par le projet Dexed (Pascal Gauthier). Ils sont distribues sous
**licence Apache 2.0** (voir l'en-tete de chaque fichier et http://www.apache.org/licenses/LICENSE-2.0).

Modifications pour FM-9 (NINE HANDS | DSP) :
- `tuning.h` / `tuning.cc` : accordage standard seulement (pas de fichiers .scl / .kbm ni de MTS-ESP) ;
- `libMTSClient.h` : remplace le client MTS-ESP par une version inactive ;
- `fm_core.h` : accesseur `getAlgorithm()` pour dessiner le schema des algorithmes a l'ecran ;
- `../debugger/Logger.h` : journalisation desactivee.

Seul ce coeur est repris de Dexed : le reste de FM-9 (voix, effets, arpegiateur, sequenceur, interface) est ecrit pour FM-9.
