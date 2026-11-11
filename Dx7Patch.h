// FM-9 (NINE HANDS | DSP) - format de voix DX7 / FM-1
// Voix "depliee" de 156 octets (format msfa) : 6 x 21 octets d'operateurs (OP6 en premier),
// puis l'enveloppe de hauteur, l'algorithme, le feedback, le LFO, la transposition, le nom, et l'interrupteur des operateurs.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fm9
{
using Voice = std::array<uint8_t, 156>;

namespace vp // positions dans la voix depliee
{
    // operateur : base = (6 - numeroOp) * 21
    enum Op { R1 = 0, R2, R3, R4, L1, L2, L3, L4, BP, LD, RD, LC, RC, RS, AMS, KVS, OL, MODE, FC, FF, DET, OpSize };
    enum Glob { PR1 = 126, PR2, PR3, PR4, PL1, PL2, PL3, PL4, ALG = 134, FB, OKS, LFS, LFD, LPMD, LAMD, LFKS, LFW, LPMS, TRNSP, NAME = 145, OPSW = 155 };
    inline int opBase (int opNumber1to6) { return (6 - opNumber1to6) * OpSize; }
}

// bornes de chaque octet (pour borner les donnees importees)
inline uint8_t voiceMax (int i)
{
    if (i < 126)
    {
        static const uint8_t opMax[21] = { 99,99,99,99, 99,99,99,99, 99,99,99, 3,3, 7, 3, 7, 99, 1, 31, 99, 14 };
        return opMax[i % 21];
    }
    static const uint8_t gMax[] = { 99,99,99,99, 99,99,99,99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48 };
    if (i < 145) return gMax[i - 126];
    if (i < 155) return 127;
    return 63;
}

inline void sanitize (Voice& v)
{
    for (int i = 0; i < 156; ++i)
        if (v[(size_t) i] > voiceMax (i)) v[(size_t) i] = voiceMax (i);
    for (int i = 0; i < 10; ++i)
        if (v[(size_t) vp::NAME + i] < 32) v[(size_t) vp::NAME + i] = ' ';
}

inline std::string voiceName (const Voice& v)
{
    std::string s (reinterpret_cast<const char*> (v.data() + vp::NAME), 10);
    while (! s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

inline void setVoiceName (Voice& v, const std::string& n)
{
    for (int i = 0; i < 10; ++i)
        v[(size_t) vp::NAME + i] = (uint8_t) (i < (int) n.size() ? (unsigned char) n[(size_t) i] : ' ');
}

// voix d'initialisation (INIT VOICE du DX7)
inline Voice initVoice()
{
    Voice v {};
    for (int op = 1; op <= 6; ++op)
    {
        auto b = vp::opBase (op);
        v[b + vp::R1] = v[b + vp::R2] = v[b + vp::R3] = v[b + vp::R4] = 99;
        v[b + vp::L1] = v[b + vp::L2] = v[b + vp::L3] = 99; v[b + vp::L4] = 0;
        v[b + vp::BP] = 39;
        v[b + vp::OL] = op == 1 ? 99 : 0;
        v[b + vp::FC] = 1;
        v[b + vp::DET] = 7;
    }
    for (int i = 0; i < 4; ++i) { v[vp::PR1 + i] = 99; v[vp::PL1 + i] = 50; }
    v[vp::OKS] = 1; v[vp::LFS] = 35; v[vp::LFKS] = 1; v[vp::LPMS] = 3; v[vp::TRNSP] = 24;
    setVoiceName (v, "INIT VOICE");
    v[vp::OPSW] = 0x3f;
    return v;
}

// --- format compresse (128 octets) des banques de 32 voix -------------------------------
inline Voice unpackVoice (const uint8_t* p)
{
    Voice v {};
    for (int op = 0; op < 6; ++op)
    {
        const uint8_t* s = p + op * 17;
        uint8_t* d = v.data() + op * 21;
        for (int i = 0; i < 11; ++i) d[i] = s[i] & 0x7f;
        d[vp::LC] = s[11] & 3;          d[vp::RC] = (s[11] >> 2) & 3;
        d[vp::RS] = s[12] & 7;          d[vp::DET] = (s[12] >> 3) & 15;
        d[vp::AMS] = s[13] & 3;         d[vp::KVS] = (s[13] >> 2) & 7;
        d[vp::OL] = s[14] & 0x7f;
        d[vp::MODE] = s[15] & 1;        d[vp::FC] = (s[15] >> 1) & 31;
        d[vp::FF] = s[16] & 0x7f;
    }
    for (int i = 0; i < 8; ++i) v[(size_t) vp::PR1 + i] = p[102 + i] & 0x7f;
    v[vp::ALG] = p[110] & 31;
    v[vp::FB] = p[111] & 7;            v[vp::OKS] = (p[111] >> 3) & 1;
    v[vp::LFS] = p[112] & 0x7f; v[vp::LFD] = p[113] & 0x7f; v[vp::LPMD] = p[114] & 0x7f; v[vp::LAMD] = p[115] & 0x7f;
    v[vp::LFKS] = p[116] & 1;          v[vp::LFW] = (p[116] >> 1) & 7; v[vp::LPMS] = (p[116] >> 4) & 7;
    v[vp::TRNSP] = p[117] & 0x7f;
    for (int i = 0; i < 10; ++i) v[(size_t) vp::NAME + i] = p[118 + i] & 0x7f;
    v[vp::OPSW] = 0x3f;
    sanitize (v);
    return v;
}

inline void packVoice (const Voice& v, uint8_t* p)
{
    for (int op = 0; op < 6; ++op)
    {
        const uint8_t* s = v.data() + op * 21;
        uint8_t* d = p + op * 17;
        for (int i = 0; i < 11; ++i) d[i] = s[i];
        d[11] = (uint8_t) ((s[vp::RC] << 2) | s[vp::LC]);
        d[12] = (uint8_t) ((s[vp::DET] << 3) | s[vp::RS]);
        d[13] = (uint8_t) ((s[vp::KVS] << 2) | s[vp::AMS]);
        d[14] = s[vp::OL];
        d[15] = (uint8_t) ((s[vp::FC] << 1) | s[vp::MODE]);
        d[16] = s[vp::FF];
    }
    for (int i = 0; i < 8; ++i) p[102 + i] = v[(size_t) vp::PR1 + i];
    p[110] = v[vp::ALG];
    p[111] = (uint8_t) ((v[vp::OKS] << 3) | v[vp::FB]);
    p[112] = v[vp::LFS]; p[113] = v[vp::LFD]; p[114] = v[vp::LPMD]; p[115] = v[vp::LAMD];
    p[116] = (uint8_t) ((v[vp::LPMS] << 4) | (v[vp::LFW] << 1) | v[vp::LFKS]);
    p[117] = v[vp::TRNSP];
    for (int i = 0; i < 10; ++i) p[118 + i] = v[(size_t) vp::NAME + i];
}

// Lit un fichier .syx : banque de 32 voix (4104 octets), voix seule (163 octets),
// ou donnees brutes (4096 octets). Renvoie les voix trouvees (vide si format inconnu).
inline std::vector<Voice> parseSysex (const uint8_t* d, size_t n)
{
    std::vector<Voice> out;
    for (size_t i = 0; i + 6 <= n; ++i)
    {
        if (d[i] != 0xF0 || d[i + 1] != 0x43) continue;
        const uint8_t fmt = d[i + 3];
        if (fmt == 9 && i + 6 + 4096 <= n)
        {
            for (int k = 0; k < 32; ++k) out.push_back (unpackVoice (d + i + 6 + k * 128));
            return out;
        }
        if (fmt == 0 && i + 6 + 155 <= n)
        {
            Voice v {};
            std::memcpy (v.data(), d + i + 6, 155);
            v[vp::OPSW] = 0x3f;
            sanitize (v);
            out.push_back (v);
            return out;
        }
    }
    if (n == 4096)
        for (int k = 0; k < 32; ++k) out.push_back (unpackVoice (d + k * 128));
    return out;
}

inline uint8_t checksum (const uint8_t* d, size_t n)
{
    int s = 0;
    for (size_t i = 0; i < n; ++i) s += d[i];
    return (uint8_t) ((128 - (s & 127)) & 127);
}

inline std::vector<uint8_t> bankToSysex (const std::vector<Voice>& voices)
{
    std::vector<uint8_t> out { 0xF0, 0x43, 0x00, 0x09, 0x20, 0x00 };
    std::vector<uint8_t> data (4096, 0);
    const Voice init = initVoice();
    for (int k = 0; k < 32; ++k)
        packVoice (k < (int) voices.size() ? voices[(size_t) k] : init, data.data() + k * 128);
    out.insert (out.end(), data.begin(), data.end());
    out.push_back (checksum (data.data(), data.size()));
    out.push_back (0xF7);
    return out;
}

inline std::vector<uint8_t> voiceToSysex (const Voice& v)
{
    std::vector<uint8_t> out { 0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B };
    out.insert (out.end(), v.begin(), v.begin() + 155);
    out.push_back (checksum (v.data(), 155));
    out.push_back (0xF7);
    return out;
}
} // namespace fm9
