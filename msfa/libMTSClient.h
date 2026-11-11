#pragma once
// FM-9 : pas de MTS-ESP, accordage standard uniquement
typedef struct MTSClient MTSClient;
inline bool MTS_HasMaster (const MTSClient*) { return false; }
inline double MTS_NoteToFrequency (const MTSClient*, char note, char) { return 440.0 * __builtin_pow (2.0, (note - 69) / 12.0); }
