// FM-9 (NINE HANDS | DSP) - presets NKS (.nksf) pour Komplete Kontrol et Maschine.
//
// Un fichier .nksf est un RIFF "NIKS" avec quatre blocs (chacun precede d'une version 32 bits = 1) :
//   NISI : metadonnees du preset (nom, banque, type, vendeur) en MessagePack
//   NICA : pages de 8 commandes affichees sur le clavier NI (identifiants de parametres VST3)
//   PLID : identifiant du plugin a charger (classe VST3)
//   PCHK : etat du plugin (exactement ce que FM-9 sauvegarde dans un projet)
#pragma once
#include <JuceHeader.h>

namespace nks
{
// ------------------------------------------------------------------------------- MessagePack minimal
struct Pack
{
    juce::MemoryOutputStream out;
    void be (uint64_t v, int bytes) { for (int i = bytes - 1; i >= 0; --i) out.writeByte ((char) ((v >> (8 * i)) & 0xff)); }
    void map (int n)   { if (n < 16) out.writeByte ((char) (0x80 | n)); else { out.writeByte ((char) 0xde); be ((uint64_t) n, 2); } }
    void arr (int n)   { if (n < 16) out.writeByte ((char) (0x90 | n)); else { out.writeByte ((char) 0xdc); be ((uint64_t) n, 2); } }
    void boolean (bool b) { out.writeByte ((char) (b ? 0xc3 : 0xc2)); }
    void uint (uint64_t v)
    {
        if (v < 128) out.writeByte ((char) v);
        else if (v < 256) { out.writeByte ((char) 0xcc); be (v, 1); }
        else if (v < 65536) { out.writeByte ((char) 0xcd); be (v, 2); }
        else if (v <= 0xffffffffULL) { out.writeByte ((char) 0xce); be (v, 4); }
        else { out.writeByte ((char) 0xcf); be (v, 8); }
    }
    void str (const juce::String& s)
    {
        const auto u = s.toUTF8();
        const auto n = (uint64_t) u.sizeInBytes() - 1;
        if (n < 32) out.writeByte ((char) (0xa0 | (int) n));
        else if (n < 256) { out.writeByte ((char) 0xd9); be (n, 1); }
        else { out.writeByte ((char) 0xda); be (n, 2); }
        out.write (u.getAddress(), (size_t) n);
    }
};

// ------------------------------------------------------------------------------- RIFF
inline void chunk (juce::MemoryOutputStream& riff, const char* id, const void* data, size_t size)
{
    riff.write (id, 4);
    riff.writeInt ((int) size + 4);
    riff.writeInt (1);                                  // version du bloc
    riff.write (data, size);
    if (((size + 4) & 1) != 0) riff.writeByte (0);      // alignement RIFF
}

struct Param { juce::String paramId, name; };           // paramId = identifiant JUCE ("" = case vide)
struct Page  { juce::String name; std::vector<Param> params; };

struct Preset
{
    juce::String name, bank, type, subType;
    juce::MemoryBlock state;
};

// classe VST3 du composant (JUCE, JUCE_VST3_CAN_REPLACE_VST2 = 0) : ABCDEF01 9182FAEB <fabricant> <plugin>
inline uint32_t fourCC (const char* c) { return ((uint32_t) (uint8_t) c[0] << 24) | ((uint32_t) (uint8_t) c[1] << 16) | ((uint32_t) (uint8_t) c[2] << 8) | (uint32_t) (uint8_t) c[3]; }

inline juce::MemoryBlock build (const Preset& p, const std::vector<Page>& pages, const char* vendor, const char* product,
                                const char* manufacturerCode, const char* pluginCode)
{
    Pack nisi;
    nisi.map (10);
    nisi.str ("author");      nisi.str (vendor);
    nisi.str ("bankchain");   nisi.arr (3); nisi.str (product); nisi.str (p.bank); nisi.str ("");
    nisi.str ("comment");     nisi.str ("");
    nisi.str ("deviceType");  nisi.str ("INST");
    nisi.str ("modes");       nisi.arr (0);
    nisi.str ("name");        nisi.str (p.name);
    nisi.str ("types");       nisi.arr (1); nisi.arr (p.subType.isNotEmpty() ? 2 : 1); nisi.str (p.type); if (p.subType.isNotEmpty()) nisi.str (p.subType);
    nisi.str ("uuid");        nisi.str (juce::Uuid().toDashedString());
    nisi.str ("vendor");      nisi.str (vendor);
    nisi.str ("product");     nisi.str (product);

    Pack nica;
    nica.map (1);
    nica.str ("ni8");
    nica.arr ((int) pages.size());
    for (auto& pg : pages)
    {
        nica.arr (8);
        for (int i = 0; i < 8; ++i)
        {
            const bool has = i < (int) pg.params.size() && pg.params[(size_t) i].paramId.isNotEmpty();
            if (! has) { nica.map (2); nica.str ("autoname"); nica.boolean (false); nica.str ("vflag"); nica.boolean (false); continue; }
            const auto& prm = pg.params[(size_t) i];
            nica.map (5);
            nica.str ("autoname"); nica.boolean (false);
            nica.str ("id");       nica.uint (juce::VST3ClientExtensions::convertJuceParameterId (prm.paramId, true));
            nica.str ("name");     nica.str (prm.name);
            nica.str ("section");  nica.str (i == 0 ? pg.name : juce::String());
            nica.str ("vflag");    nica.boolean (false);
        }
    }

    Pack plid;
    plid.map (1);
    plid.str ("VST3.uid");
    plid.arr (4);
    plid.uint (0xABCDEF01u); plid.uint (0x9182FAEBu); plid.uint (fourCC (manufacturerCode)); plid.uint (fourCC (pluginCode));

    juce::MemoryOutputStream body;
    body.write ("NIKS", 4);
    chunk (body, "NISI", nisi.out.getData(), nisi.out.getDataSize());
    chunk (body, "NICA", nica.out.getData(), nica.out.getDataSize());
    chunk (body, "PLID", plid.out.getData(), plid.out.getDataSize());
    chunk (body, "PCHK", p.state.getData(), p.state.getSize());

    juce::MemoryOutputStream riff;
    riff.write ("RIFF", 4);
    riff.writeInt ((int) body.getDataSize());
    riff.write (body.getData(), body.getDataSize());
    return riff.getMemoryBlock();
}
} // namespace nks
