// FM-9 (NINE HANDS | DSP) - moteur FM 6 operateurs (coeur msfa, licence Apache 2.0)
// Gestion des voix (12 par defaut comme la FM-1), mono / legato avec portamento,
// enveloppe ADSR d'amplitude optionnelle (fonction propre a la FM-1), sortie mono.
#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include "Dx7Patch.h"
#include "msfa/synth.h"
#include "msfa/dx7note.h"
#include "msfa/lfo.h"
#include "msfa/env.h"
#include "msfa/pitchenv.h"
#include "msfa/porta.h"
#include "msfa/fm_core.h"
#include "msfa/exp2.h"
#include "msfa/sin.h"
#include "msfa/freqlut.h"
#include "msfa/controllers.h"
#include "msfa/tuning.h"

namespace fm9
{
struct SimpleAdsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float level = 0.0f, aInc = 1, dCoef = 0, s = 1, rCoef = 0;

    void set (double sr, float a, float d, float sus, float r)
    {
        aInc = (float) (1.0 / std::max (1.0, a * sr));
        dCoef = (float) std::exp (-1.0 / std::max (1.0, d * sr / 5.0));
        rCoef = (float) std::exp (-1.0 / std::max (1.0, r * sr / 5.0));
        s = sus;
    }
    void on() { stage = Attack; }
    void off() { if (stage != Idle) stage = Release; }
    bool active() const { return stage != Idle; }
    float next()
    {
        switch (stage)
        {
            case Attack: level += aInc; if (level >= 1.0f) { level = 1.0f; stage = Decay; } break;
            case Decay:  level = s + (level - s) * dCoef; if (std::abs (level - s) < 1.0e-4f) { level = s; stage = Sustain; } break;
            case Sustain: level = s; break;
            case Release: level *= rCoef; if (level < 1.0e-5f) { level = 0; stage = Idle; } break;
            case Idle: level = 0; break;
        }
        return level;
    }
};

struct EngineSettings
{
    int polyphony = 12;          // 1..16
    bool mono = false;
    int glide = 0;               // 0..100 (0 a 1 s)
    bool fingered = false;       // glissando seulement en legato
    int bendUp = 2, bendDown = 2; // demi-tons
    int wheelRange = 50;         // 0..99 profondeur de la molette (hauteur + amplitude)
    int wheelTarget = 1;         // 0 hauteur, 1 hauteur+ampli, 2 ampli
    int atRange = 0;             // aftertouch 0..99
    bool ampEnvOn = false;       // ADSR optionnel de la FM-1
    float att = 0.005f, dec = 0.3f, sus = 1.0f, rel = 0.3f;
};

class FmEngine
{
public:
    static constexpr int maxVoices = 16;

    FmEngine()
    {
        static bool tablesReady = false;
        if (! tablesReady) { Exp2::init(); Tanh::init(); Sin::init(); tablesReady = true; }
        tuning = createStandardTuning();
        for (auto& v : voices) v.note = std::make_unique<Dx7Note> (tuning, nullptr);
        patch = initVoice();
        ctrl.core = &core;
        ctrl.masterTune = 0;
        ctrl.mpeEnabled = false;
        ctrl.pitch_cc = 0x2000;
        ctrl.pitch_step = 0;
        ctrl.modwheel_cc = ctrl.foot_cc = ctrl.breath_cc = ctrl.aftertouch_cc = 0;
        ctrl.portamento_enable_cc = false;
        ctrl.portamento_cc = 0;
        ctrl.portamento_gliss_cc = false;
        applyControllers();
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        Freqlut::init (sr);
        Lfo::init (sr);
        PitchEnv::init (sr);
        Env::init_sr (sr);
        Porta::init_sr (sr);
        lfo.reset (patch.data() + vp::LFS);
        allSoundOff();
        pos = N;
    }

    // a appeler en debut de bloc audio : nouvelle voix ou memes donnees
    void setPatch (const Voice& v)
    {
        if (std::memcmp (v.data(), patch.data(), 156) == 0) return;
        const bool lfoChanged = std::memcmp (v.data() + vp::LFS, patch.data() + vp::LFS, 6) != 0;
        patch = v;
        if (lfoChanged) lfo.reset (patch.data() + vp::LFS);
        for (auto& s : voices)
            if (s.live) s.note->update (patch.data(), s.midi, s.vel, 1);
    }

    void setSettings (const EngineSettings& e)
    {
        const bool monoChanged = e.mono != set.mono;
        set = e;
        set.polyphony = std::clamp (set.polyphony, 1, maxVoices);
        if (monoChanged) allNotesOff();
        for (auto& s : voices) s.env.set (sr, set.att, set.dec, set.sus, set.rel);
        applyControllers();
    }

    void noteOn (int note, int velocity)
    {
        if (velocity <= 0) { noteOff (note); return; }
        note = std::clamp (note, 0, 127);
        const bool anyDown = std::any_of (voices.begin(), voices.end(), [] (const Slot& s) { return s.live && s.keydown; });
        if (! anyDown) lfo.keydown();

        if (set.mono)
        {
            heldPush (note, velocity);
            startMono (note, velocity, anyDown);
            return;
        }

        int best = 0, bestScore = -1;
        const int n = set.polyphony;
        for (int i = 0; i < n; ++i)
        {
            auto& s = voices[(size_t) i];
            int score = 0;
            if (! s.live) score += 8;
            if (! s.keydown) score += 4;
            if (s.midi == note) score += 2;
            if (score > bestScore || (score == bestScore && s.order < voices[(size_t) best].order)) { best = i; bestScore = score; }
        }
        startVoice (best, note, velocity, -1);
    }

    void noteOff (int note)
    {
        if (set.mono)
        {
            heldRemove (note);
            if (monoVoice >= 0 && voices[(size_t) monoVoice].midi == note)
            {
                if (heldCount > 0) { startMono (held[heldCount - 1].note, held[heldCount - 1].vel, true); return; }
                release (voices[(size_t) monoVoice]);
            }
            return;
        }
        for (auto& s : voices)
            if (s.live && s.keydown && s.midi == note) release (s);
    }

    void sustain (bool down)
    {
        sustainDown = down;
        if (! down)
            for (auto& s : voices)
                if (s.sustained) { s.sustained = false; keyUp (s); }
    }

    void pitchBend (int value14) { ctrl.pitch_cc = std::clamp (value14, 0, 16383); }
    void modWheel (int v) { ctrl.modwheel_cc = std::clamp (v, 0, 127); ctrl.refresh(); }
    void aftertouch (int v) { ctrl.aftertouch_cc = std::clamp (v, 0, 127); ctrl.refresh(); }

    void allNotesOff()
    {
        heldCount = 0;
        for (auto& s : voices) if (s.live) { s.sustained = false; release (s); }
    }

    void allSoundOff()
    {
        heldCount = 0; monoVoice = -1;
        for (auto& s : voices) { s.live = false; s.keydown = false; s.sustained = false; s.env.stage = SimpleAdsr::Idle; s.env.level = 0; }
    }

    int activeVoices() const { int c = 0; for (auto& s : voices) c += s.live ? 1 : 0; return c; }

    // rendu mono, nombre d'echantillons quelconque (blocs internes de 64)
    void render (float* out, int num)
    {
        int done = 0;
        while (done < num)
        {
            if (pos >= N) { renderBlock(); pos = 0; }
            const int k = std::min (num - done, N - pos);
            std::memcpy (out + done, block + pos, sizeof (float) * (size_t) k);
            pos += k; done += k;
        }
    }

private:
    struct Slot
    {
        std::unique_ptr<Dx7Note> note;
        int midi = -1, vel = 0;
        bool live = false, keydown = false, sustained = false;
        uint32_t order = 0;
        SimpleAdsr env;
    };
    struct Held { int note, vel; };

    static int juce_glide (int g) { return std::clamp ((g * 99) / 100, 0, 99); }

    void applyControllers()
    {
        ctrl.pitch_range_up = set.bendUp; ctrl.pitch_range_dn = set.bendDown;
        ctrl.wheel.range = set.wheelRange;
        ctrl.wheel.pitch = set.wheelTarget <= 1;
        ctrl.wheel.amp = set.wheelTarget >= 1;
        ctrl.wheel.eg = false;
        ctrl.at.range = set.atRange;
        ctrl.at.pitch = true; ctrl.at.amp = false; ctrl.at.eg = false;
        ctrl.portamento_enable_cc = set.mono && set.glide > 0;
        ctrl.portamento_cc = juce_glide (set.glide);
        ctrl.refresh();
    }

    void startVoice (int i, int note, int vel, int portaFrom)
    {
        auto& s = voices[(size_t) i];
        s.note->init (patch.data(), note, vel, 1, &ctrl);
        if (portaFrom >= 0 && ctrl.portamento_enable_cc) s.note->initPortamento (*voices[(size_t) portaFrom].note);
        if (patch[vp::OKS]) s.note->oscSync();
        s.midi = note; s.vel = vel; s.live = true; s.keydown = true; s.sustained = false;
        s.order = ++counter;
        s.env.set (sr, set.att, set.dec, set.sus, set.rel);
        s.env.level = 0; s.env.on();
    }

    void startMono (int note, int vel, bool legato)
    {
        // deux emplacements en alternance : la nouvelle note glisse depuis l'ancienne
        const int prev = monoVoice;
        const int next = prev == 0 ? 1 : 0;
        if (prev >= 0 && voices[(size_t) prev].live) keyUpNow (voices[(size_t) prev], ! legato);
        const bool glideNow = prev >= 0 && (! set.fingered || legato);
        startVoice (next, note, vel, glideNow ? prev : -1);
        if (prev >= 0 && legato) voices[(size_t) prev].live = false; // legato : pas de double note
        monoVoice = next;
    }

    void release (Slot& s)
    {
        s.keydown = false;
        if (sustainDown) { s.sustained = true; return; }
        keyUp (s);
    }
    void keyUp (Slot& s) { s.note->keyup(); s.env.off(); }
    void keyUpNow (Slot& s, bool keepTail) { s.keydown = false; s.note->keyup(); s.env.off(); if (! keepTail) s.live = false; }

    void heldPush (int note, int vel)
    {
        heldRemove (note);
        if (heldCount < 128) held[heldCount++] = { note, vel };
    }
    void heldRemove (int note)
    {
        for (int i = 0; i < heldCount; ++i)
            if (held[i].note == note) { for (int j = i; j < heldCount - 1; ++j) held[j] = held[j + 1]; --heldCount; return; }
    }

    void renderBlock()
    {
        std::fill (block, block + N, 0.0f);
        const int32_t lfoVal = lfo.getsample();
        const int32_t lfoDelay = lfo.getdelay();
        for (auto& s : voices)
        {
            if (! s.live) continue;
            std::fill (ibuf, ibuf + N, 0);
            s.note->compute (ibuf, lfoVal, lfoDelay, &ctrl);
            for (int j = 0; j < N; ++j)
            {
                float f = (float) ((double) ibuf[j] * (1.0 / (double) (1 << 28)));
                f = std::clamp (f, -1.0f, 1.0f);
                if (set.ampEnvOn) f *= s.env.next();
                block[j] += f;
            }
            if (set.ampEnvOn ? (! s.env.active() && ! s.keydown) : (! s.keydown && ! s.sustained && ! s.note->isPlaying()))
                s.live = false;
        }
    }

    double sr = 48000.0;
    std::shared_ptr<TuningState> tuning;
    std::array<Slot, maxVoices> voices;
    Voice patch;
    Controllers ctrl;
    FmCore core;
    Lfo lfo;
    EngineSettings set;
    Held held[128] {};
    int heldCount = 0, monoVoice = -1;
    bool sustainDown = false;
    uint32_t counter = 0;
    float block[64] {};
    int32_t ibuf[64] {};
    int pos = 64;
};
} // namespace fm9
