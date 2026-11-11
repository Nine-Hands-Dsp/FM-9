// FM-9 (NINE HANDS | DSP) - les six effets de la FM-1 (mono) :
// filtre (passe-bas / passe-haut / passe-bande), chorus, phaser, distorsion, delay, reverb (room / hall / plate).
// Chaque effet : marche/arret, dosage sec/traite, deux reglages. Ordre de la chaine modifiable.
#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <vector>

namespace fm9
{
enum FxId { fxFilter = 0, fxChorus, fxPhaser, fxDrive, fxDelay, fxReverb, fxCount };

struct FxSettings
{
    bool on[fxCount] {};
    float mix[fxCount] { 1.0f, 0.5f, 0.5f, 1.0f, 0.3f, 0.3f };
    float a[fxCount] { 0.7f, 0.3f, 0.3f, 0.4f, 0.35f, 0.5f };   // 1er reglage (0..1)
    float b[fxCount] { 0.2f, 0.5f, 0.5f, 0.6f, 0.35f, 0.5f };   // 2e reglage (0..1)
    int filterType = 0;     // 0 LP, 1 HP, 2 BP
    int reverbType = 1;     // 0 Room, 1 Hall, 2 Plate
    double delayMs = 0;     // si > 0 : temps impose par la synchro au tempo
    std::array<int, fxCount> order { fxFilter, fxReverb, fxDelay, fxDrive, fxChorus, fxPhaser };
};

class Smoothed
{
public:
    void reset (float v) { cur = tgt = v; }
    void set (float v) { tgt = v; }
    float next (float k) { cur += (tgt - cur) * k; return cur; }
    float cur = 0, tgt = 0;
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        dry.assign ((size_t) maxBlock, 0.0f);
        chorusBuf.assign ((size_t) (sr * 0.06) + 4, 0.0f);
        delayBuf.assign ((size_t) (sr * 2.6) + 4, 0.0f);
        verb.setSampleRate (sr);
        verbBufR.assign ((size_t) maxBlock, 0.0f);
        reset();
        k = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 30.0 / sr));
    }

    void reset()
    {
        ic1 = ic2 = 0; std::fill (chorusBuf.begin(), chorusBuf.end(), 0.0f); std::fill (delayBuf.begin(), delayBuf.end(), 0.0f);
        for (auto& s : ap) s = 0;
        phFb = 0; driveLp = 0; delLp = 0; chPh = phPh = 0;
        verb.reset();
        for (int i = 0; i < fxCount; ++i) { mixS[i].reset (0); aS[i].reset (0.5f); bS[i].reset (0.5f); }
        levelS.reset (0.6f);
    }

    void process (float* x, int n, const FxSettings& s)
    {
        for (int i = 0; i < fxCount; ++i)
        {
            // filtre : pas de dosage (100 %) ; distorsion : le dosage est son niveau de sortie
            mixS[i].set (s.on[i] ? ((i == fxFilter || i == fxDrive) ? 1.0f : s.mix[i]) : 0.0f);
            aS[i].set (s.a[i]); bS[i].set (s.b[i]);
        }
        levelS.set (s.mix[fxDrive]);
        for (int id : s.order)
        {
            const bool active = s.on[id] || mixS[id].cur > 1.0e-4f;
            if (! active) { mixS[id].cur = 0; continue; }
            switch (id)
            {
                case fxFilter: filter (x, n, s.filterType); break;
                case fxChorus: chorus (x, n); break;
                case fxPhaser: phaser (x, n); break;
                case fxDrive:  drive (x, n); break;
                case fxDelay:  delay (x, n, s.delayMs); break;
                case fxReverb: reverb (x, n, s.reverbType); break;
                default: break;
            }
        }
    }

    // temps du delay en ms pour la valeur 0..1 du potard (10 ms a 2 s, courbe exponentielle)
    static double delayTimeMs (float v) { return 10.0 * std::pow (200.0, (double) v); }
    static double cutoffHz (float v) { return 100.0 * std::pow (200.0, (double) v); }   // 100 Hz a 20 kHz

private:
    float wet (int id, float d, float w) { const float m = mixS[id].next (k); return d + (w - d) * m; }

    void filter (float* x, int n, int type)
    {
        for (int i = 0; i < n; ++i)
        {
            const float fc = (float) juce::jlimit (20.0, sr * 0.45, cutoffHz (aS[fxFilter].next (k)));
            const float q = 0.5f + std::pow (bS[fxFilter].next (k), 1.5f) * 14.5f;
            const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
            const float kk = 1.0f / q;
            const float a1 = 1.0f / (1.0f + g * (g + kk)), a2 = g * a1, a3 = g * a2;
            const float v0 = x[i];
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            float y = type == 0 ? v2 : type == 1 ? v0 - kk * v1 - v2 : v1;
            if (! std::isfinite (y)) { ic1 = ic2 = 0; y = 0; }
            x[i] = wet (fxFilter, v0, y);   // le filtre de la FM-1 n'a pas de dosage : mix fixe a 100 %
        }
    }

    float readFrac (const std::vector<float>& b, int w, double delaySamples) const
    {
        const int size = (int) b.size();
        double rp = (double) w - delaySamples;
        while (rp < 0) rp += size;
        const int i0 = (int) rp; const float f = (float) (rp - i0);
        const float y0 = b[(size_t) (i0 % size)], y1 = b[(size_t) ((i0 + 1) % size)];
        return y0 + (y1 - y0) * f;
    }

    void chorus (float* x, int n)
    {
        const int size = (int) chorusBuf.size();
        for (int i = 0; i < n; ++i)
        {
            const float rate = 0.1f + aS[fxChorus].next (k) * 4.9f;            // 0,1 a 5 Hz
            const float depth = bS[fxChorus].next (k);
            chPh += rate / (float) sr; if (chPh >= 1) chPh -= 1;
            const double d = (0.007 + 0.005 * depth * (1.0 + std::sin (2.0 * juce::MathConstants<double>::pi * chPh))) * sr;
            chorusBuf[(size_t) chW] = x[i];
            const float y = readFrac (chorusBuf, chW, d);
            chW = (chW + 1) % size;
            x[i] = wet (fxChorus, x[i], y);
        }
    }

    void phaser (float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float rate = 0.05f + aS[fxPhaser].next (k) * 3.95f;
            const float depth = bS[fxPhaser].next (k);
            phPh += rate / (float) sr; if (phPh >= 1) phPh -= 1;
            const float lfo = 0.5f + 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * phPh);
            const float fc = 200.0f * std::pow (20.0f, lfo * (0.3f + 0.7f * depth));
            const float t = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, (float) sr * 0.45f) / (float) sr);
            const float c = (t - 1.0f) / (t + 1.0f);
            float y = x[i] + phFb * (0.2f + 0.5f * depth);
            for (auto& s : ap) { const float o = c * y + s; s = y - c * o; y = o; }
            phFb = std::isfinite (y) ? y : 0.0f;
            x[i] = wet (fxPhaser, x[i], 0.5f * (x[i] + y) * 1.4f);
        }
    }

    void drive (float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float amt = aS[fxDrive].next (k);
            const float tone = bS[fxDrive].next (k);
            const float g = std::pow (60.0f, amt);
            float y = std::tanh (x[i] * g * 1.5f) / std::tanh (1.5f * std::sqrt (g));
            const float lpc = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * (800.0f * std::pow (18.0f, tone)) / (float) sr);
            driveLp += (y - driveLp) * lpc;
            // FM-1 : GAIN / TONE / LEVEL ; le dosage est le niveau de sortie de la distorsion
            const float level = levelS.next (k);
            x[i] = wet (fxDrive, x[i], driveLp * 0.8f * level * 1.6f);
        }
    }

    void delay (float* x, int n, double syncedMs)
    {
        const int size = (int) delayBuf.size();
        for (int i = 0; i < n; ++i)
        {
            const float tv = aS[fxDelay].next (k);
            const float fb = bS[fxDelay].next (k) * 0.95f;
            const double ms = syncedMs > 0 ? syncedMs : delayTimeMs (tv);
            delTime += (juce::jlimit (1.0, 2500.0, ms) * sr * 0.001 - delTime) * 0.0005;
            const float y = readFrac (delayBuf, delW, delTime);
            delLp += (y - delLp) * 0.35f;
            float in = x[i] + delLp * fb;
            if (! std::isfinite (in)) in = 0;
            delayBuf[(size_t) delW] = in;
            delW = (delW + 1) % size;
            x[i] = wet (fxDelay, x[i], y);
        }
    }

    void reverb (float* x, int n, int type)
    {
        const float size = aS[fxReverb].next (juce::jmin (1.0f, (float) n * k));
        const float tone = bS[fxReverb].next (juce::jmin (1.0f, (float) n * k));
        juce::Reverb::Parameters p;
        static const float baseRoom[3] = { 0.45f, 0.82f, 0.70f };
        static const float baseDamp[3] = { 0.55f, 0.35f, 0.10f };
        p.roomSize = juce::jlimit (0.0f, 0.98f, baseRoom[type] + (size - 0.5f) * 0.4f);
        p.damping = juce::jlimit (0.0f, 1.0f, baseDamp[type] + (0.5f - tone) * 0.6f);
        p.width = 0.0f; p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.freezeMode = 0.0f;
        verb.setParameters (p);
        std::copy (x, x + n, dry.begin());
        verb.processMono (x, n);
        for (int i = 0; i < n; ++i) x[i] = wet (fxReverb, dry[(size_t) i], x[i] * 0.6f);
    }

    double sr = 48000.0;
    float k = 0.01f;
    Smoothed mixS[fxCount], aS[fxCount], bS[fxCount], levelS;
    float ic1 = 0, ic2 = 0;
    std::vector<float> chorusBuf, delayBuf, dry, verbBufR;
    int chW = 0, delW = 0;
    double delTime = 0.3 * 48000.0;
    float chPh = 0, phPh = 0, phFb = 0, driveLp = 0, delLp = 0;
    std::array<float, 6> ap {};
    juce::Reverb verb;
};
} // namespace fm9
