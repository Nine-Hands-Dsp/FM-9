// FM-9 (NINE HANDS | DSP) - hote de firmwares de la FM-1
// Fait tourner dans FM-9 les firmwares libres de la FM-1 compiles pour le web (WebAssembly autonome),
// avec le moteur wasm3 (licence MIT, dossier wasm3/). Deux interfaces sont reconnues :
//  - "sim_*" (fm1sim : Hortator...) : sim_init / sim_render / sim_audio / sim_btn / sim_key / sim_enc / sim_master / sim_fb ...
//  - "web_*" (famille Felucca : X0X, TRIBE, GHOULBOX...) : web_boot / web_render / web_out_l|r / web_buttons / web_keys ...
// Le firmware ne tourne que dans le thread audio (apres son demarrage) ; l'interface lui parle par une file sans verrou.
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

struct M3Environment; struct M3Runtime; struct M3Module; struct M3Function;

namespace fm9
{
class FirmwareHost
{
public:
    enum class Abi { sim, web };
    static constexpr int fbW = 240, fbH = 240;
    static constexpr double fwRate = 44100.0;

    // charge et demarre un firmware (thread de messages) ; storage = fichier de sauvegarde (flash) du firmware
    static std::unique_ptr<FirmwareHost> create (const void* wasm, size_t size, const juce::File& storage, juce::String& error);
    ~FirmwareHost();

    // ---- thread audio ----
    void render (float* left, float* right, int numSamples, double hostRate);
    void handleMidi (const juce::MidiMessage& m);

    // ---- interface (n'importe quel thread : file sans verrou) ----
    void button (int index, bool down) { push (evButton, index, down ? 1 : 0); }     // 0-11 touches de fonction, 12 OCT-, 13 OCT+
    void key (int index, bool down)    { push (evKey, index, down ? 1 : 0); }        // 0 (F3) .. 26 (G5)
    void encoder (int role, int steps) { push (evEnc, role, steps); }                // 0 SELECT, 1 ALGORITHM, 2 PRESETS, 3-6 KNOB1-4
    void master (float normalised)     { masterNorm.store (juce::jlimit (0.0f, 1.0f, normalised)); }
    void releaseAll()                  { push (evReleaseAll, 0, 0); }

    // ---- etat lu par l'interface ----
    std::atomic<uint32_t> litButtons { 0 }, litKeys { 0 }, dimKeys { 0 };
    std::atomic<uint32_t> fbSerial { 0 };
    void copyScreen (juce::Image& dest);                                 // RGB565 -> image 240 x 240
    Abi abi() const { return kind; }

    // ---- sauvegarde (thread de messages, a appeler regulierement) ----
    void saveIfDirty();

private:
    enum EvType { evButton, evKey, evEnc, evReleaseAll, evMidi };
    struct Event { int type, a, b; };

    FirmwareHost() = default;
    bool start (const void* wasm, size_t size, juce::String& error);
    void push (int type, int a, int b);
    void applyEvents();
    void renderChunk (int frames);           // ajoute 'frames' echantillons du firmware dans l'anneau
    uint8_t* mem (size_t* size = nullptr);
    M3Function* fn (const char* name);
    bool call (M3Function* f, ...);
    uint32_t callU (M3Function* f);
    void captureScreen();
    void captureStorage (bool force);

    Abi kind = Abi::sim;
    M3Environment* env = nullptr;
    M3Runtime* rt = nullptr;
    M3Module* mod = nullptr;
    std::vector<uint8_t> wasmBytes;           // wasm3 garde un pointeur sur le code : il doit vivre avec le module

    // fonctions du firmware
    M3Function *fRender = nullptr, *fAudio = nullptr, *fOutL = nullptr, *fOutR = nullptr, *fBtn = nullptr, *fKey = nullptr,
               *fButtons = nullptr, *fKeys = nullptr, *fEnc = nullptr, *fMaster = nullptr, *fFb = nullptr, *fBlits = nullptr,
               *fLeds = nullptr, *fKeyLeds = nullptr, *fDimKeys = nullptr, *fMidi = nullptr,
               *fFlash = nullptr, *fFlashDirty = nullptr, *fFlashClean = nullptr,
               *fNor = nullptr, *fNorSize = nullptr, *fNorWrites = nullptr,
               *fStore = nullptr, *fStoreLen = nullptr, *fStoreSetLen = nullptr, *fStoreWrites = nullptr, *fNobj = nullptr, *fStoreMax = nullptr;
    bool fbSwapped = false;                   // famille Felucca : RGB565 a octets inverses (ordre de l'ecran)
    float masterMax = 1023.0f;

    // file interface -> audio
    juce::AbstractFifo fifo { 256 };
    std::array<Event, 256> events {};
    juce::SpinLock pushLock;                  // plusieurs producteurs possibles (interface, MIDI)
    uint32_t btnMask = 0, keyMask = 0;
    std::atomic<float> masterNorm { 0.7f };
    int lastMaster = -1;

    // anneau stereo a 44,1 kHz + reechantillonnage (Hermite 4 points)
    static constexpr int ring = 8192;
    std::vector<float> rl = std::vector<float> (ring), rr = std::vector<float> (ring);
    int64_t written = 0;                      // echantillons du firmware produits
    double readPos = 0.0;                     // position de lecture (echantillons du firmware)

    // ecran (double tampon) et sauvegarde
    std::array<std::array<uint16_t, fbW * fbH>, 2> fb {};
    std::atomic<int> fbFront { 0 };
    uint32_t lastBlits = 0xffffffffu;
    double sinceScreen = 1.0, sinceStorage = 0.0;
    std::vector<uint8_t> storageMirror;       // copie de la flash / NOR / objets sauvegardes
    std::atomic<bool> storageDirty { false };
    juce::SpinLock storageLock;
    uint32_t lastWrites = 0;
    juce::File storageFile;

    JUCE_DECLARE_NON_COPYABLE (FirmwareHost)
};

// installation depuis GitHub : trouve le firmware (.wasm) publie par un depot (branche gh-pages, sinon depot)
struct FirmwareInstaller
{
    static juce::File folder();                                          // .../Nine Hands DSP/FM-9/Firmwares
    static bool parseRepo (const juce::String& text, juce::String& owner, juce::String& repo);
    // telecharge (thread d'arriere-plan) ; renvoie le fichier installe ou une erreur
    static juce::File install (const juce::String& owner, const juce::String& repo, juce::String& error,
                               std::function<void (const juce::String&)> progress = {});
    static int abiOf (const void* wasm, size_t size);                    // 1 sim, 2 web, 0 inconnu
    static juce::StringArray installed();                                // noms des firmwares installes
};
} // namespace fm9
