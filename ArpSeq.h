// FM-9 (NINE HANDS | DSP) - arpegiateur et sequenceur 16 pas de la FM-1
// Les deux suivent une horloge en temps (noires) : celle de l'hote (synchro) ou le tempo interne.
#pragma once
#include <JuceHeader.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace fm9
{
// --- valeurs de pas communes ------------------------------------------------------------------
inline const juce::StringArray& rateNames()
{
    static const juce::StringArray n { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return n;
}
inline double rateBeats (int idx)
{
    static const double b[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, 5, idx)];
}

struct ClockInfo
{
    double beat0 = 0;             // position (en noires) du premier echantillon du bloc
    double beatsPerSample = 0;
    int numSamples = 0;
};

struct PendingOff { double beat; int note; };

// planifie les pas d'une grille avec swing ; appelle onStep(k, sampleOffset, longueurDuPasEnNoires)
template <typename Fn>
inline void forEachStep (const ClockInfo& c, double stepLen, float swing, Fn&& onStep)
{
    if (c.beatsPerSample <= 0 || c.numSamples <= 0) return;
    const double b1 = c.beat0 + c.beatsPerSample * c.numSamples;
    const double sw = juce::jlimit (0.0, 0.5, (double) swing) * stepLen;
    const long long first = (long long) std::floor (c.beat0 / stepLen) - 1;
    const long long last = (long long) std::floor (b1 / stepLen) + 1;
    for (long long k = first; k <= last; ++k)
    {
        const bool odd = (k & 1) != 0;
        const double on = (double) k * stepLen + (odd ? sw : 0.0);
        if (on < c.beat0 || on >= b1) continue;
        const double len = odd ? stepLen - sw : stepLen + sw;
        const int off = juce::jlimit (0, c.numSamples - 1, (int) ((on - c.beat0) / c.beatsPerSample));
        onStep (k, off, on, len);
    }
}

template <size_t Cap>
struct OffList
{
    std::array<PendingOff, Cap> items {};
    int count = 0;
    void add (double beat, int note) { if (count < (int) Cap) items[(size_t) count++] = { beat, note }; }
    template <typename Fn> void flush (const ClockInfo& c, Fn&& noteOff)
    {
        const double b1 = c.beat0 + c.beatsPerSample * c.numSamples;
        for (int i = 0; i < count;)
        {
            if (items[(size_t) i].beat < b1)
            {
                const int off = juce::jlimit (0, juce::jmax (0, c.numSamples - 1), (int) ((items[(size_t) i].beat - c.beat0) / juce::jmax (1.0e-12, c.beatsPerSample)));
                noteOff (items[(size_t) i].note, off);
                items[(size_t) i] = items[(size_t) --count];
            }
            else ++i;
        }
    }
    template <typename Fn> void flushAll (Fn&& noteOff) { for (int i = 0; i < count; ++i) noteOff (items[(size_t) i].note, 0); count = 0; }
};

// --- arpegiateur ------------------------------------------------------------------------------
class Arpeggiator
{
public:
    enum Mode { Up = 0, Down, Inclusive, Exclusive, Random, Order, Repeat };
    static const juce::StringArray& modeNames()
    {
        static const juce::StringArray n { "UP", "DOWN", "INCLUSIVE", "EXCLUSIVE", "RANDOM", "ORDER", "REPEAT" };
        return n;
    }

    void keyDown (int note, int vel)
    {
        if (latch && physicalDown == 0) count = 0;       // nouvel accord en mode latch
        ++physicalDown;
        for (int i = 0; i < count; ++i) if (notes[i] == note) return;
        if (count < 32) { notes[count] = note; vels[count] = vel; ++count; }
        if (count == 1) restart = true;
    }
    void keyUp (int note)
    {
        physicalDown = juce::jmax (0, physicalDown - 1);
        if (latch) return;
        removeNote (note);
    }
    void setLatch (bool l)
    {
        if (latch && ! l && physicalDown == 0) count = 0;
        latch = l;
    }
    void clear() { count = 0; physicalDown = 0; }
    bool hasNotes() const { return count > 0; }

    // produit les notes du bloc ; out(note, vel, offset) pour note on, vel = 0 pour note off
    template <typename Out>
    void process (const ClockInfo& c, int mode, int octaves, int rate, float gate, float swing, Out&& out)
    {
        offs.flush (c, [&] (int n, int off) { out (n, 0, off); });
        if (count == 0) { offs.flushAll ([&] (int n, int) { out (n, 0, 0); }); idx = -1; dir = 1; return; }
        const double L = rateBeats (rate);
        if (restart)
        {
            // premiere note tout de suite, puis la grille reprend
            restart = false; idx = -1; dir = 1;
            playStep (mode, octaves, c.beat0, L, gate, 0, out);
            lastStepBeat = c.beat0;
        }
        forEachStep (c, L, swing, [&] (long long, int off, double on, double len)
        {
            if (on - lastStepBeat < L * 0.5) return;  // evite un doublon juste apres le depart
            lastStepBeat = on;
            playStep (mode, octaves, on, len, gate, off, out);
        });
    }

    bool latch = false;

private:
    void removeNote (int note)
    {
        for (int i = 0; i < count; ++i)
            if (notes[i] == note) { for (int j = i; j < count - 1; ++j) { notes[j] = notes[j + 1]; vels[j] = vels[j + 1]; } --count; return; }
    }

    template <typename Out>
    void playStep (int mode, int octaves, double on, double len, float gate, int off, Out&& out)
    {
        // notes triees (ou dans l'ordre joue) sur 1 a 4 octaves, puis le cycle du mode
        int base[32], bv[32];
        for (int i = 0; i < count; ++i) { base[i] = notes[i]; bv[i] = vels[i]; }
        if (mode != Order)
            for (int i = 1; i < count; ++i)
                for (int j = i; j > 0 && base[j - 1] > base[j]; --j) { std::swap (base[j - 1], base[j]); std::swap (bv[j - 1], bv[j]); }
        const int oct = juce::jlimit (1, 4, octaves);
        int up[128], uv[128], n = 0;
        for (int o = 0; o < oct; ++o) for (int i = 0; i < count && n < 128; ++i) { up[n] = base[i] + 12 * o; uv[n] = bv[i]; ++n; }
        int cyc[256], cv[256], m = 0;
        auto push = [&] (int k) { if (m < 256) { cyc[m] = up[k]; cv[m] = uv[k]; ++m; } };
        switch (mode)
        {
            case Down:      for (int k = n - 1; k >= 0; --k) push (k); break;
            case Inclusive: for (int k = 0; k < n; ++k) push (k); for (int k = n - 1; k >= 0; --k) push (k); break;
            case Exclusive: for (int k = 0; k < n; ++k) push (k); for (int k = n - 2; k >= 1; --k) push (k); break;
            case Repeat:    for (int k = 0; k < n; ++k) { push (k); push (k); } break;
            default:        for (int k = 0; k < n; ++k) push (k); break;
        }
        if (m == 0) return;
        idx = mode == Random ? rng.nextInt (m) : (idx + 1) % m;
        if (idx >= m) idx = 0;
        const int note = juce::jlimit (0, 127, cyc[idx]);
        out (note, cv[idx], off);
        offs.add (on + len * juce::jlimit (0.05, 1.0, (double) gate), note);
    }

    int notes[32] {}, vels[32] {};
    int count = 0, physicalDown = 0, idx = -1, dir = 1;
    bool restart = false;
    double lastStepBeat = -1.0e9;
    OffList<64> offs;
    juce::Random rng;
};

// --- sequenceur -------------------------------------------------------------------------------
// un pas tient dans 64 bits : nombre de notes (3 bits), velocite (7), 6 notes de 7 bits
struct SeqStep
{
    int count = 0, vel = 100;
    int notes[6] {};
    bool tie = false;          // note longue : prolonge les notes du pas precedent
    uint64_t pack() const
    {
        uint64_t v = (uint64_t) juce::jlimit (0, 6, count) | ((uint64_t) (vel & 127) << 3);
        for (int i = 0; i < 6; ++i) v |= (uint64_t) (notes[i] & 127) << (10 + 7 * i);
        if (tie) v |= (uint64_t) 1 << 63;
        return v;
    }
    static SeqStep unpack (uint64_t v)
    {
        SeqStep s; s.count = (int) (v & 7); if (s.count > 6) s.count = 6;
        s.tie = (v >> 63) != 0;
        s.vel = (int) ((v >> 3) & 127);
        for (int i = 0; i < 6; ++i) s.notes[i] = (int) ((v >> (10 + 7 * i)) & 127);
        return s;
    }
};

class SequencerData
{
public:
    static constexpr int patterns = 16, steps = 16;
    std::atomic<uint64_t> data[patterns][steps];
    SequencerData() { clearAll(); }
    void clearAll() { for (auto& p : data) for (auto& s : p) s.store (0); }
    void clearPattern (int p) { for (auto& s : data[p]) s.store (0); }
    SeqStep get (int p, int s) const { return SeqStep::unpack (data[p][s].load (std::memory_order_relaxed)); }
    void put (int p, int s, const SeqStep& st) { data[p][s].store (st.pack(), std::memory_order_relaxed); }

    juce::String toString() const
    {
        juce::StringArray a;
        for (auto& p : data) for (auto& s : p) a.add (juce::String::toHexString ((juce::int64) s.load()));
        return a.joinIntoString (",");
    }
    void fromString (const juce::String& str)
    {
        auto a = juce::StringArray::fromTokens (str, ",", "");
        int i = 0;
        for (auto& p : data) for (auto& s : p) { s.store (i < a.size() ? (uint64_t) a[i].getHexValue64() : 0); ++i; }
    }
};

class Sequencer
{
public:
    // lecture ; out(note, vel, offset). chain = nombre de patterns joues a la suite.
    template <typename Out>
    void process (const SequencerData& d, const ClockInfo& c, int pattern, int chain, int length, bool poly, int rate,
                  float gate, float swing, int transpose, Out&& out)
    {
        offs.flush (c, [&] (int n, int off) { out (n, 0, off); });
        const double L = rateBeats (rate);
        length = juce::jlimit (1, 16, length);
        chain = juce::jlimit (1, 16, chain);
        forEachStep (c, L, swing, [&] (long long k, int off, double on, double len)
        {
            const long long kk = k < 0 ? 0 : k;
            const int stepIdx = (int) (kk % length);
            const int pat = (pattern + (int) ((kk / length) % chain)) % SequencerData::patterns;
            playhead.store (stepIdx);
            playPattern.store (pat);
            const auto st = d.get (pat, stepIdx);
            const double offBeat = on + len * juce::jlimit (0.05, 1.0, (double) gate);
            if (st.tie)
            {
                // note longue : les notes en cours sont prolongees jusqu'a la fin de ce pas
                for (int i = 0; i < offs.count; ++i) offs.items[(size_t) i].beat = juce::jmax (offs.items[(size_t) i].beat, offBeat);
                return;
            }
            if (st.count == 0) return;
            const int n = poly ? st.count : 1;
            for (int i = 0; i < n; ++i)
            {
                const int note = juce::jlimit (0, 127, st.notes[i] + transpose);
                out (note, juce::jmax (1, st.vel), off);
                offs.add (offBeat, note);
            }
        });
    }
    template <typename Out> void stop (Out&& out) { offs.flushAll ([&] (int n, int off) { out (n, 0, off); }); playhead.store (-1); }

    std::atomic<int> playhead { -1 }, playPattern { 0 };
private:
    OffList<128> offs;
};
} // namespace fm9
