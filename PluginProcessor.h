// FM-9 (NINE HANDS | DSP) - synthe FM 6 operateurs d'apres la M-VAVE FM-1
#pragma once
#include <JuceHeader.h>
#include "FmEngine.h"
#include "Effects.h"
#include "ArpSeq.h"
#include "Params.h"

class FM9Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int numBanks = 4, bankSize = 32;

    FM9Processor();
    ~FM9Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FM-9"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return numBanks * bankSize; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- voix et banques (thread de messages) ---
    fm9::Voice currentVoiceFromParams() const;
    void applyVoiceToParams (const fm9::Voice& v);
    juce::String getVoiceName() const { return voiceName; }
    void setVoiceName (const juce::String& n) { voiceName = n.toUpperCase().substring (0, 10); sendChangeMessageToUi(); }
    // presets numerotes 001-128 comme la FM-1 (banques A-D de 32)
    static juce::String programLabel (int index) { return juce::String (index + 1).paddedLeft ('0', 3); }
    const fm9::Voice& bankVoice (int index) const { return banks[(size_t) (index / bankSize)][(size_t) (index % bankSize)]; }
    void storeCurrentVoice (int index);
    bool importSysexFile (const juce::File& f, int bank);       // banque ou voix seule
    bool exportBank (const juce::File& f, int bank) const;
    bool exportVoice (const juce::File& f) const;
    void resetBankToFactory (int bank);
    bool isEdited() const { return edited; }
    void nextProgram (int delta) { setCurrentProgram ((currentProgram + delta + getNumPrograms()) % getNumPrograms()); }

    // --- MIDI learn ---
    void startMidiLearn (const juce::String& paramId) { learnTarget = paramId; learning.store (true); }
    void cancelMidiLearn() { learning.store (false); learnTarget.clear(); }
    void clearMidiLearn (const juce::String& paramId);
    int learnedCcFor (const juce::String& paramId) const;
    bool isLearning() const { return learning.load(); }
    juce::String getLearnTarget() const { return learnTarget; }

    // --- etat lu par l'interface ---
    std::atomic<uint32_t> keyBits[4] {};      // notes en cours (128 bits)
    std::atomic<float> outLevel { 0.0f };
    std::atomic<int> uiEvent { 0 };           // incremente a chaque changement a afficher
    std::atomic<int> lastCc { -1 };
    std::atomic<int> recStep { 0 };
    std::atomic<bool> restRequest { false };
    std::atomic<int> tieRequests { 0 };       // notes longues en enregistrement (SELECT tourne en tenant une note)
    std::array<std::atomic<int>, 16> patternVoice;   // preset associe a chaque pattern (-1 = aucun)
    // oscilloscope de l'ecran d'accueil
    static constexpr int scopeSize = 2048;
    std::array<float, scopeSize> scope {};
    std::atomic<int> scopePos { 0 };
    fm9::Sequencer seq;
    fm9::SequencerData seqData;
    // ordre de la chaine d'effets (6 x 3 bits, lu par le thread audio)
    std::array<int, fm9::fxCount> getFxOrder() const
    {
        std::array<int, fm9::fxCount> o {}; const uint32_t v = fxOrderPacked.load();
        for (int i = 0; i < fm9::fxCount; ++i) o[(size_t) i] = (int) ((v >> (3 * i)) & 7);
        return o;
    }
    void setFxOrder (const std::array<int, fm9::fxCount>& o)
    {
        uint32_t v = 0; for (int i = 0; i < fm9::fxCount; ++i) v |= (uint32_t) o[(size_t) i] << (3 * i);
        fxOrderPacked.store (v); sendChangeMessageToUi();
    }

    juce::MidiKeyboardState keyboardState;    // clavier de la facade (souris)
    std::atomic<int> pendingProgram { -1 };   // Program Change MIDI (applique par le timer)
    juce::AudioProcessorValueTreeState apvts;
    juce::ValueTree uiState { "UI" };         // taille, glow... (thread de messages)

    void sendChangeMessageToUi() { uiEvent.fetch_add (1); }

private:
    void timerCallback() override;
    void loadBanksFromDisk();
    void saveBankToDisk (int bank) const;
    static juce::File userFolder();
    void handleMidi (const juce::MidiMessage& m, int pos, juce::MidiBuffer& toEngine);
    void setKey (int note, bool on);
    float pv (const char* id) const;

    std::array<std::array<fm9::Voice, bankSize>, numBanks> banks;
    int currentProgram = 0;
    juce::String voiceName { "FM PIANO" };
    bool edited = false;
    bool applyingVoice = false;

    fm9::FmEngine engine;
    fm9::FxChain fx;
    fm9::Arpeggiator arp;
    std::vector<float> mono;
    juce::MidiBuffer engineEvents, kbEvents;
    int lastPatternLoaded = -1;
    double sr = 48000.0;
    double freeBeat = 0.0;
    bool wasArpOn = false, wasSeqRunning = false, wasRec = false;
    int recHeld = 0;
    fm9::SeqStep recChord;

    // pointeurs vers les parametres (lus dans le thread audio)
    std::array<std::atomic<float>*, 156> voiceParams {};
    std::array<std::atomic<float>*, 6> opOnP {};
    double seqFree = 0.0;
    int editCheckDiv = 0;
    std::atomic<uint32_t> fxOrderPacked { 0 };
    std::atomic<float>* p[64] {};
    enum PIdx { pVolume, pPoly, pMono, pGlide, pGlideMode, pBendUp, pBendDown, pMidiCh, pWheel, pWheelDest, pAt, pAdsr, pAtt, pDec, pSus, pRel,
                pFilterType, pReverbType, pDelaySync, pDelayDiv,
                pArpOn, pArpMode, pArpOct, pArpRate, pArpGate, pArpSwing, pArpLatch,
                pSeqActive, pSeqChain, pSeqOn, pSeqRec, pSeqMode, pSeqRate, pSeqGate, pSeqSwing, pSeqTrans, pSeqPattern, pSeqLen,
                pSync, pTempo, pCount };
    std::atomic<float>* fxP[fm9::fxCount][4] {};

    // MIDI learn : CC -> parametre, files sans verrou vers le thread de messages
    std::atomic<bool> learning { false };
    juce::String learnTarget;
    std::array<std::atomic<int>, 128> ccTarget;          // index dans allParams, -1 = rien
    std::array<std::atomic<float>, 128> ccValue;
    std::array<std::atomic<bool>, 128> ccPending;
    std::atomic<int> learnCc { -1 };
    juce::Array<juce::RangedAudioParameter*> allParams;
    struct CcRamp { float from = 0, to = 0, cur = 0; double t0 = 0, dur = 0.03, lastTime = 0; int lo = 127, hi = 0; bool active = false; };
    std::array<CcRamp, 128> ramps;

    // voix recue en SysEx par le MIDI (copiee dans le thread audio, appliquee par le timer)
    std::array<uint8_t, 4104> sysexBuf {};
    std::atomic<int> sysexSize { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FM9Processor)
};
