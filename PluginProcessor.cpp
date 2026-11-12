// FM-9 (NINE HANDS | DSP) - synthe FM 6 operateurs d'apres la M-VAVE FM-1
#include "PluginProcessor.h"
#include "NksExport.h"
#include "PluginEditor.h"
#include "FactoryBank.h"

using namespace fm9;

FM9Processor::FM9Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)), apvts (*this, nullptr, "FM9", createLayout())
{
    for (int op = 1; op <= 6; ++op)
        for (int f = 0; f < 21; ++f)
            voiceParams[(size_t) (vp::opBase (op) + f)] = apvts.getRawParameterValue (opParamId (op, f));
    for (int g = 0; g < 19; ++g)
        voiceParams[(size_t) (vp::PR1 + g)] = apvts.getRawParameterValue (globIds[g]);
    for (int op = 1; op <= 6; ++op) opOnP[(size_t) op - 1] = apvts.getRawParameterValue (opOnId (op));
    setFxOrder ({ fxFilter, fxReverb, fxDelay, fxDrive, fxChorus, fxPhaser });

    const char* ids[] = { "volume", "poly", "mono", "glide", "glideMode", "bendUp", "bendDown", "midiCh", "wheel", "wheelDest", "atRange", "adsrOn", "attack", "decay", "sustain", "release",
                          "filterType", "reverbType", "delaySync", "delayDiv",
                          "arpOn", "arpMode", "arpOct", "arpRate", "arpGate", "arpSwing", "arpLatch",
                          "seqActive", "seqChain", "seqOn", "seqRec", "seqMode", "seqRate", "seqGate", "seqSwing", "seqTrans", "seqPattern", "seqLen",
                          "sync", "tempo" };
    for (int i = 0; i < pCount; ++i) { p[i] = apvts.getRawParameterValue (ids[i]); jassert (p[i] != nullptr); }
    for (int f = 0; f < fxCount; ++f)
    {
        fxP[f][0] = apvts.getRawParameterValue (fxId (f, "on"));
        fxP[f][1] = apvts.getRawParameterValue (fxId (f, "mix"));
        fxP[f][2] = apvts.getRawParameterValue (fxId (f, "a"));
        fxP[f][3] = apvts.getRawParameterValue (fxId (f, "b"));
    }

    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) allParams.add (r);
    for (auto& v : patternVoice) v.store (-1);
    for (int c = 0; c < 16; ++c) for (int i = 0; i < 128; ++i) shiftedNote[c][i] = (uint8_t) i;
    for (auto& t : extTarget) t.store (-1);
    for (int i = 0; i < 128; ++i) { ctlLast[(size_t) i] = -1; ccTarget[(size_t) i].store (-1); ccValue[(size_t) i].store (0); ccPending[(size_t) i].store (false); keyStepTarget[(size_t) i] = -1; }
    for (auto& k : keyStepMap)
        for (int i = 0; i < allParams.size(); ++i)
            if (allParams[i]->getParameterID() == k.paramId) keyStepTarget[(size_t) k.cc] = i;

    for (int b = 0; b < numBanks; ++b)
        for (int i = 0; i < bankSize; ++i)
        {
            Voice v = initVoice();
            if (b == 0) std::copy (factoryBank[i], factoryBank[i] + 156, v.begin());
            banks[(size_t) b][(size_t) i] = v;
        }
    loadBanksFromDisk();
    setCurrentProgram (0);
    uiState.setProperty ("scale", 0.6, nullptr);
    uiState.setProperty ("glow", 75, nullptr);
    startTimerHz (60);
}

FM9Processor::~FM9Processor()
{
    stopTimer();
    if (fwOwner != nullptr) fwOwner->saveIfDirty();
}

// ------------------------------------------------------------------------------------------------ firmwares
juce::StringArray FM9Processor::availableFirmwares() const
{
    juce::StringArray s { builtinFirmware };
    for (auto& n : fm9::FirmwareInstaller::installed()) if (! n.equalsIgnoreCase (builtinFirmware)) s.add (n);
    return s;
}

std::unique_ptr<fm9::FirmwareHost> FM9Processor::loadFirmware (const juce::String& name, juce::String& error, std::function<void (float)> progress)
{
    // creation du firmware (n'importe quel thread) : lecture du fichier puis demarrage du moteur
    auto say = [&progress] (float f) { if (progress) progress (f); };
    std::unique_ptr<fm9::FirmwareHost> next;
    const auto folder = fm9::FirmwareInstaller::folder();
    say (0.05f);
    if (name.endsWith (fm9::FirmwareInstaller::hardwareSuffix))
    {
        // paquet materiel (.fwsc) : emulateur de la puce de la FM-1
        const auto stem = name.dropLastCharacters (fm9::FirmwareInstaller::hardwareSuffix.length());
        const auto file = folder.getChildFile (juce::File::createLegalFileName (stem) + ".fwsc");
        juce::MemoryBlock pkg;
        if (! file.existsAsFile() || ! file.loadFileAsData (pkg)) { error = "firmware not found: " + name; return {}; }
        say (0.25f);
        next = fm9::FirmwareHost::createHardware (pkg.getData(), pkg.getSize(), folder.getChildFile (juce::File::createLegalFileName (stem) + ".hwflash"), error);
        if (next == nullptr) return {};
        say (0.5f);
        // la machine demarre seule : on attend son premier ecran (quelques centaines de ms)
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (next->fbSerial.load() == 0 && next->status().isEmpty())
        {
            const double el = juce::Time::getMillisecondCounterHiRes() - t0;
            if (el > 4000.0) break;
            say (0.5f + 0.45f * (float) juce::jmin (1.0, el / 1500.0));
            juce::Thread::sleep (20);
        }
    }
    else
    {
        juce::MemoryBlock wasm;
        const auto file = folder.getChildFile (juce::File::createLegalFileName (name) + ".wasm");
        if (file.existsAsFile()) file.loadFileAsData (wasm);
        else if (name.equalsIgnoreCase (builtinFirmware)) wasm.append (BinaryData::hortator_wasm, (size_t) BinaryData::hortator_wasmSize);
        else { error = "firmware not found: " + name; return {}; }
        say (0.3f);
        next = fm9::FirmwareHost::create (wasm.getData(), wasm.getSize(), folder.getChildFile (juce::File::createLegalFileName (name) + ".flash"), error);
        if (next == nullptr) return {};
    }
    say (1.0f);
    return next;
}

bool FM9Processor::activateFirmware (const juce::String& name, juce::String& error)
{
    std::unique_ptr<fm9::FirmwareHost> next;
    if (name.isNotEmpty())
    {
        next = loadFirmware (name, error);
        if (next == nullptr) return false;
    }
    activatePrepared (name, std::move (next));
    return true;
}

void FM9Processor::updateFirmwareLatency()
{
    if (auto* fw = fwOwner.get()) setLatencySamples (fw->latencySamples (sr));
}

void FM9Processor::activatePrepared (const juce::String& name, std::unique_ptr<fm9::FirmwareHost> next)
{
    for (auto& w : keyBits) w.store (0);                  // pas de touche restee allumee d'un moteur a l'autre
    if (next != nullptr) next->prepare (sr, blockSize);
    // echange sans verrou : le thread audio ne garde jamais l'ancien firmware apres cette attente
    auto* old = fwOwner.get();
    fwActive.store (next.get());
    for (int i = 0; i < 400 && fwInUse.load(); ++i) juce::Thread::sleep (1);
    if (old != nullptr) old->saveIfDirty();
    fwOwner = std::move (next);
    firmwareName = name;
    // le firmware materiel tourne un peu en avance : Reaper compense ce decalage
    setLatencySamples (fwOwner != nullptr ? fwOwner->latencySamples (sr) : 0);
    engine.allSoundOff();
    sendChangeMessageToUi();
}

bool FM9Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FM9Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    blockSize = samplesPerBlock;
    limiter.prepare (sampleRate);
    if (auto* fw = fwOwner.get()) { fw->prepare (sr, samplesPerBlock); setLatencySamples (fw->latencySamples (sr)); }
    engine.prepare (sr);
    fx.prepare (sr, juce::jmax (samplesPerBlock, 4096) + 64);
    mono.assign ((size_t) juce::jmax (samplesPerBlock, 4096) + 64, 0.0f);
    engineEvents.ensureSize (4096);
}

float FM9Processor::pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

// ------------------------------------------------------------------------------------------------
Voice FM9Processor::currentVoiceFromParams() const
{
    Voice v {};
    for (int i = 0; i < 145; ++i)
        v[(size_t) i] = (uint8_t) juce::jlimit (0, (int) voiceMax (i), juce::roundToInt (voiceParams[(size_t) i]->load (std::memory_order_relaxed)));
    uint8_t sw = 0;
    for (int op = 1; op <= 6; ++op)
        if (opOnP[(size_t) op - 1]->load() > 0.5f) sw |= (uint8_t) (1u << (6 - op));
    v[vp::OPSW] = sw;
    for (int i = 0; i < 10; ++i)
        v[(size_t) vp::NAME + i] = (uint8_t) (i < voiceName.length() ? (int) voiceName[i] & 127 : ' ');
    return v;
}

void FM9Processor::applyVoiceToParams (const Voice& v)
{
    const juce::ScopedValueSetter<bool> svs (applyingVoice, true);
    auto setP = [this] (const juce::String& id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    for (int op = 1; op <= 6; ++op)
    {
        for (int f = 0; f < 21; ++f) setP (opParamId (op, f), (float) v[(size_t) (vp::opBase (op) + f)]);
        setP (opOnId (op), (v[vp::OPSW] >> (6 - op)) & 1 ? 1.0f : 0.0f);
    }
    for (int g = 0; g < 19; ++g) setP (globIds[g], (float) v[(size_t) (vp::PR1 + g)]);
    voiceName = juce::String (fm9::voiceName (v));
    edited = false;
    sendChangeMessageToUi();
}

void FM9Processor::setCurrentProgram (int index)
{
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, index);
    applyVoiceToParams (bankVoice (currentProgram));
}

const juce::String FM9Processor::getProgramName (int index)
{
    return programLabel (index) + " " + juce::String (fm9::voiceName (bankVoice (index)));
}

void FM9Processor::changeProgramName (int index, const juce::String& newName)
{
    auto& v = banks[(size_t) (index / bankSize)][(size_t) (index % bankSize)];
    fm9::setVoiceName (v, newName.toUpperCase().toStdString());
    saveBankToDisk (index / bankSize);
}

void FM9Processor::storeCurrentVoice (int index)
{
    index = juce::jlimit (0, getNumPrograms() - 1, index);
    banks[(size_t) (index / bankSize)][(size_t) (index % bankSize)] = currentVoiceFromParams();
    currentProgram = index;
    edited = false;
    saveBankToDisk (index / bankSize);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    sendChangeMessageToUi();
}

// ------------------------------------------------------------------------------------------------
juce::File FM9Processor::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Nine Hands DSP").getChildFile ("FM-9");
}

void FM9Processor::loadBanksFromDisk()
{
    for (int b = 0; b < numBanks; ++b)
    {
        auto f = userFolder().getChildFile ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + b)) + ".syx");
        if (! f.existsAsFile()) continue;
        juce::MemoryBlock mb;
        if (! f.loadFileAsData (mb)) continue;
        auto voices = parseSysex ((const uint8_t*) mb.getData(), mb.getSize());
        if (voices.size() == 32)
            for (int i = 0; i < 32; ++i) banks[(size_t) b][(size_t) i] = voices[(size_t) i];
    }
}

void FM9Processor::saveBankToDisk (int bank) const
{
    auto folder = userFolder();
    folder.createDirectory();
    exportBank (folder.getChildFile ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + ".syx"), bank);
}

bool FM9Processor::exportBank (const juce::File& f, int bank) const
{
    std::vector<Voice> v (banks[(size_t) bank].begin(), banks[(size_t) bank].end());
    auto bytes = bankToSysex (v);
    return f.replaceWithData (bytes.data(), bytes.size());
}

bool FM9Processor::exportVoice (const juce::File& f) const
{
    auto bytes = voiceToSysex (currentVoiceFromParams());
    return f.replaceWithData (bytes.data(), bytes.size());
}

bool FM9Processor::importSysexFile (const juce::File& f, int bank)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb)) return false;
    auto voices = parseSysex ((const uint8_t*) mb.getData(), mb.getSize());
    if (voices.empty()) return false;
    if (voices.size() == 1)
    {
        applyVoiceToParams (voices[0]);
        edited = true;
        return true;
    }
    bank = juce::jlimit (0, numBanks - 1, bank);
    for (int i = 0; i < bankSize; ++i) banks[(size_t) bank][(size_t) i] = voices[(size_t) juce::jmin (i, (int) voices.size() - 1)];
    saveBankToDisk (bank);
    setCurrentProgram (bank * bankSize);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
    return true;
}

void FM9Processor::resetBankToFactory (int bank)
{
    for (int i = 0; i < bankSize; ++i)
    {
        Voice v = initVoice();
        if (bank == 0) std::copy (factoryBank[i], factoryBank[i] + 156, v.begin());
        banks[(size_t) bank][(size_t) i] = v;
    }
    userFolder().getChildFile ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + bank)) + ".syx").deleteFile();
    if (currentProgram / bankSize == bank) setCurrentProgram (currentProgram);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

// ------------------------------------------------------------------------------------------------
juce::String FM9Processor::ctlId (int i)
{
    static const char* n[numCtl] = { "select", "presets", "algorithm", "knob1", "knob2", "knob3", "knob4", "octdown", "octup",
                                     "btn_fx", "btn_sel", "btn_env", "btn_lfo", "btn_edit", "btn_glo", "btn_home", "btn_save", "btn_arp", "btn_seq", "btn_play", "btn_rec" };
    return "ctl:" + juce::String (n[juce::jlimit (0, numCtl - 1, i)]);
}

juce::String FM9Processor::ctlName (int i)
{
    static const char* n[numCtl] = { "SELECT", "PRESETS", "ALGORITHM", "KNOB1", "KNOB2", "KNOB3", "KNOB4", "OCT-", "OCT+",
                                     "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY/STOP", "REC" };
    return n[juce::jlimit (0, numCtl - 1, i)];
}

juce::String FM9Processor::targetIdOf (int t) const
{
    if (t >= ctlBase && t < ctlBase + numCtl) return ctlId (t - ctlBase);
    if (t >= 0 && t < allParams.size()) return allParams[t]->getParameterID();
    return {};
}

int FM9Processor::targetIndexOf (const juce::String& id) const
{
    for (int i = 0; i < numCtl; ++i) if (ctlId (i) == id) return ctlBase + i;
    for (int i = 0; i < allParams.size(); ++i) if (allParams[i]->getParameterID() == id) return i;
    return -1;
}

void FM9Processor::clearMidiLearn (const juce::String& paramId)
{
    for (int c = 0; c < 128; ++c)
        if (targetIdOf (ccTarget[(size_t) c].load()) == paramId) ccTarget[(size_t) c].store (-1);
    for (auto& t : extTarget) if (targetIdOf (t.load()) == paramId) t.store (-1);
}

juce::String FM9Processor::learnedSourceName (const juce::String& id) const
{
    if (const int c = learnedCcFor (id); c >= 0) return "CC " + juce::String (c);
    for (int s = 0; s < extSources; ++s)
        if (targetIdOf (extTarget[(size_t) s].load()) == id)
        {
            if (s >= 128 && s < 256) return "Note " + juce::MidiMessage::getMidiNoteName (s - 128, true, true, 3);
            if (s == 256) return "Start";
            if (s == 257) return "Continue";
            if (s == 258) return "Stop";
            if (s >= 300 && s < 300 + 128)
            {
                const int c = s - 300;
                return "MMC " + juce::String (c == 1 ? "Stop" : c == 2 ? "Play" : c == 3 ? "Deferred play" : c == 4 ? "Fast forward" : c == 5 ? "Rewind"
                                              : c == 6 ? "Record" : c == 7 ? "Record exit" : c == 9 ? "Pause" : juce::String (c));
            }
        }
    return {};
}

// boutons de la facade appris sur une touche, Start / Stop ou une commande MMC (thread audio)
bool FM9Processor::routeButtonSource (const juce::MidiMessage& m)
{
    int src = -1;
    bool momentary = true, down = true;
    if (m.isNoteOnOrOff()) { src = 128 + m.getNoteNumber(); momentary = false; down = m.isNoteOn(); }
    else if (m.isMidiStart()) src = 256;
    else if (m.isMidiContinue()) src = 257;
    else if (m.isMidiStop()) src = 258;
    else if (m.isSysEx())
    {
        // MMC : F0 7F <appareil> 06 <commande> F7
        const auto* d = m.getSysExData();
        if (m.getSysExDataSize() >= 4 && d[0] == 0x7f && d[2] == 0x06) src = 300 + (d[3] & 0x7f);
    }
    if (src < 0) return false;
    if (learning.load() && learnButton.load())
    {
        if (down) learnExt.store (src);
        return true;
    }
    const int t = extTarget[(size_t) src].load();
    if (t < ctlBase + firstButton || t >= ctlBase + numCtl) return false;
    const int k = t - ctlBase;
    if (momentary) { btnPress[(size_t) k].fetch_add (1); btnRel[(size_t) k].fetch_add (1); }
    else (down ? btnPress : btnRel)[(size_t) k].fetch_add (1);
    return true;
}

// appuis et relachements des boutons (thread de messages) : l'interface s'en charge, sinon le firmware ou l'octave
void FM9Processor::dispatchButtons()
{
    for (int k = firstButton; k < numCtl; ++k)
    {
        const int pr = btnPress[(size_t) k].exchange (0), rl = btnRel[(size_t) k].exchange (0);
        if (pr == 0 && rl == 0) continue;
        if (ctlUi.load()) { ctlSteps[(size_t) k].fetch_add (pr); ctlRel[(size_t) k].fetch_add (rl); continue; }
        const int idx = k >= firstFn ? k - firstFn : 12 + (k - firstButton);
        if (auto* fw = firmware())
        {
            for (int n = 0; n < pr; ++n) { fw->button (idx, true); if (k < firstFn) fw->button (idx, false); }
            if (k >= firstFn) for (int n = 0; n < rl; ++n) fw->button (idx, false);
            continue;
        }
        if (k < firstFn)
            for (int n = 0; n < pr; ++n)
            {
                const int oct = juce::jlimit (-3, 3, (int) uiState.getProperty ("octave", 0) + (k == firstButton ? -1 : 1));
                uiState.setProperty ("octave", oct, nullptr);
                kbShift.store (12 * oct + (int) uiState.getProperty ("transpose", 0));
                kbOct.store (oct);
            }
    }
}

int FM9Processor::learnedCcFor (const juce::String& paramId) const
{
    for (int c = 0; c < 128; ++c)
        if (targetIdOf (ccTarget[(size_t) c].load()) == paramId) return c;
    return -1;
}

juce::String FM9Processor::midiMapString() const
{
    juce::StringArray map;
    for (int c = 0; c < 128; ++c)
        if (const auto id = targetIdOf (ccTarget[(size_t) c].load()); id.isNotEmpty()) map.add (juce::String (c) + "=" + id);
    for (int s = 0; s < extSources; ++s)
        if (const auto id = targetIdOf (extTarget[(size_t) s].load()); id.isNotEmpty()) map.add ("x" + juce::String (s) + "=" + id);
    return map.joinIntoString (";");
}

void FM9Processor::applyMidiMapString (const juce::String& mapText)
{
    for (int c = 0; c < 128; ++c) { ccTarget[(size_t) c].store (-1); ctlLast[(size_t) c] = -1; ramps[(size_t) c] = {}; }
    for (auto& t : extTarget) t.store (-1);
    for (auto& e : juce::StringArray::fromTokens (mapText, ";", ""))
    {
        if (e.startsWith ("x"))
        {
            const int s = e.substring (1).upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const int t = targetIndexOf (e.fromFirstOccurrenceOf ("=", false, false));
            if (t >= 0 && s >= 0 && s < extSources) extTarget[(size_t) s].store (t);
            continue;
        }
        const int c = e.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const int t = targetIndexOf (e.fromFirstOccurrenceOf ("=", false, false));
        if (t >= 0 && c >= 0 && c < 128) ccTarget[(size_t) c].store (t);
    }
    sendChangeMessageToUi();
}

juce::File FM9Processor::midiMapFolder() const
{
    auto d = userFolder().getChildFile ("MIDI Maps");
    d.createDirectory();
    return d;
}

bool FM9Processor::saveMidiMap (const juce::File& f) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", "FM-9 MIDI map");
    o->setProperty ("map", midiMapString());
    o->setProperty ("ctlProfile", ctlProfile.load());
    return f.replaceWithText (juce::JSON::toString (juce::var (o)));
}

bool FM9Processor::loadMidiMap (const juce::File& f)
{
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.hasProperty ("map")) return false;
    if (v.hasProperty ("ctlProfile")) ctlProfile.store ((int) v.getProperty ("ctlProfile", 0));
    applyMidiMapString (v.getProperty ("map", "").toString());
    return true;
}

void FM9Processor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    if (fm9::FirmwareHost::hwLatencyChanged().exchange (false)) updateFirmwareLatency();   // la marge de l'emulation a grandi
    if (kbOctChanged.exchange (false))                       // l'octave a suivi le clavier maitre
    {
        uiState.setProperty ("octave", kbOct.load(), nullptr);
        kbShift.store (12 * kbOct.load() + (int) uiState.getProperty ("transpose", 0));
    }

    // configuration MIDI par defaut (nouvelle instance sans assignation)
    if (pendingDefaultMap) { pendingDefaultMap = false; if (midiMapString().isEmpty() && defaultMidiMapFile().existsAsFile()) loadMidiMap (defaultMidiMapFile()); }

    // apprentissage MIDI
    const int lc = learnCc.exchange (-1);
    if (lc >= 0 && learning.load())
    {
        const int target = targetIndexOf (learnTarget);
        if (target >= 0)
        {
            clearMidiLearn (learnTarget);
            ccTarget[(size_t) lc].store (target);
            ramps[(size_t) lc] = {};
            ctlLast[(size_t) lc] = -1;
        }
        learning.store (false);
        learnTarget.clear();
        sendChangeMessageToUi();
    }
    const int le = learnExt.exchange (-1);
    if (le >= 0 && learning.load())
    {
        const int target = targetIndexOf (learnTarget);
        if (target >= 0) { clearMidiLearn (learnTarget); extTarget[(size_t) le].store (target); }
        learning.store (false);
        learnTarget.clear();
        sendChangeMessageToUi();
    }

    dispatchButtons();

    // CC appris -> parametres, avec rampe et course apprise
    for (int c = 0; c < 128; ++c)
    {
        auto& r = ramps[(size_t) c];
        int t = ccTarget[(size_t) c].load();                    // le MIDI Learn est prioritaire
        const bool fromKeyStep = t < 0 && ctlProfile.load() == 1 && keyStepTarget[(size_t) c] >= 0;
        if (fromKeyStep) t = keyStepTarget[(size_t) c];
        if (t < 0) { r.active = false; continue; }
        if (t >= ctlBase)
        {
            // commande physique (SELECT, PRESETS, ALGORITHM, KNOB1-4) : chaque cran de CC donne un pas
            r.active = false;
            if (! ccPending[(size_t) c].exchange (false)) continue;
            const int v = juce::roundToInt (ccValue[(size_t) c].load());
            int& last = ctlLast[(size_t) c];
            if (t - ctlBase >= firstButton)
            {
                // boutons (OCT- / OCT+, FX ... REC) : appui quand le CC passe au-dessus de 63, relachement en dessous
                const int k = t - ctlBase;
                const bool press = v >= 64 && last < 64, release = v < 64 && last >= 64;
                last = v;
                if (press) btnPress[(size_t) k].fetch_add (1);
                if (release && k >= firstFn) btnRel[(size_t) k].fetch_add (1);
                continue;
            }
            int steps = last < 0 ? 0 : v - last;
            if (last >= 0 && steps == 0 && (v == 0 || v == 127)) steps = v == 0 ? -1 : 1;     // encore tourne en butee
            last = v;
            if (steps == 0) continue;
            const int k = t - ctlBase;
            if (ctlUi.load()) ctlSteps[(size_t) k].fetch_add (steps);
            else if (k == 1) nextProgram (steps > 0 ? 1 : -1);                         // interface fermee : PRESETS
            else if (k == 2) if (auto* a = apvts.getParameter ("alg"))                // et ALGORITHM restent actifs
            {
                const auto& rg = a->getNormalisableRange();
                a->setValueNotifyingHost (a->convertTo0to1 (juce::jlimit (rg.start, rg.end, a->convertFrom0to1 (a->getValue()) + (steps > 0 ? 1.0f : -1.0f))));
            }
            continue;
        }
        auto* prm = allParams[t];
        if (fromKeyStep && ccPending[(size_t) c].load())
        {
            // la KeyStep met en marche ce qu'elle regle : effet (mix / cutoff) ou ADSR
            const auto id = prm->getParameterID();
            juce::String sw;
            if (id.endsWith ("_mix") || id.startsWith ("filter_")) sw = id.upToFirstOccurrenceOf ("_", false, false) + "_on";
            else if (id == "attack" || id == "decay" || id == "sustain" || id == "release") sw = "adsrOn";
            if (sw.isNotEmpty()) if (auto* q = apvts.getParameter (sw)) if (q->getValue() < 0.5f) q->setValueNotifyingHost (1.0f);
        }
        if (ccPending[(size_t) c].exchange (false))
        {
            const int raw = juce::roundToInt (ccValue[(size_t) c].load());
            r.lo = juce::jmin (r.lo, raw); r.hi = juce::jmax (r.hi, raw);
            float norm = (float) raw / 127.0f;
            if (r.hi - r.lo >= 24)  // course reelle de la pedale, zones mortes aux butees
                norm = juce::jlimit (0.0f, 1.0f, ((float) raw - (float) r.lo - 1.0f) / juce::jmax (1.0f, (float) (r.hi - r.lo) - 2.0f));
            const bool discrete = prm->getNumSteps() <= 128 || dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr;
            if (discrete)
            {
                prm->setValueNotifyingHost (norm);
                r.active = false;
            }
            else
            {
                r.from = r.active ? r.cur : prm->getValue();
                r.to = norm;
                r.dur = juce::jlimit (0.01, 0.12, now - r.lastTime);
                r.t0 = now; r.lastTime = now; r.active = true;
            }
        }
        if (r.active)
        {
            const float a = (float) juce::jlimit (0.0, 1.0, (now - r.t0) / r.dur);
            const float lin = r.from + (r.to - r.from) * a;
            r.cur += (lin - r.cur) * 0.6f;                  // lissage a 2 poles
            if (a >= 1.0f && std::abs (r.cur - r.to) < 1.0e-3f) { r.cur = r.to; r.active = false; }
            prm->setValueNotifyingHost (r.cur);
        }
    }

    if (const int pc = pendingProgram.exchange (-1); pc >= 0) setCurrentProgram (pc);
    if (pendingFirmware.isNotEmpty())
    {
        const auto name = pendingFirmware == "-" ? juce::String() : pendingFirmware;
        pendingFirmware.clear();
        juce::String err;
        if (name != firmwareName) activateFirmware (name, err);
    }
    if (++fwSaveDiv >= 60) { fwSaveDiv = 0; if (fwOwner != nullptr) fwOwner->saveIfDirty(); }

    // SEQ actif : chaque pattern joue (chaine) charge le preset qui lui est associe
    if (apvts.getRawParameterValue ("seqActive")->load() > 0.5f)
    {
        const int pat = apvts.getRawParameterValue ("seqOn")->load() > 0.5f ? seq.playPattern.load()
                                                                             : juce::roundToInt (apvts.getRawParameterValue ("seqPattern")->load()) - 1;
        if (pat != lastPatternLoaded)
        {
            lastPatternLoaded = pat;
            const int v = patternVoice[(size_t) juce::jlimit (0, 15, pat)].load();
            if (v >= 0 && v != currentProgram) setCurrentProgram (v);
        }
    }
    else lastPatternLoaded = -1;

    // voix ou banque recue en SysEx par l'entree MIDI
    if (const int n = sysexSize.exchange (0); n > 0)
    {
        auto voices = parseSysex (sysexBuf.data(), (size_t) n);
        if (voices.size() == 1) { applyVoiceToParams (voices[0]); edited = true; }
        else if (voices.size() == 32)
        {
            const int b = currentProgram / bankSize;
            for (int i = 0; i < bankSize; ++i) banks[(size_t) b][(size_t) i] = voices[(size_t) i];
            saveBankToDisk (b);
            setCurrentProgram (b * bankSize);
        }
    }

    // voix modifiee par rapport a l'emplacement memoire ?
    if (++editCheckDiv >= 6)
    {
        editCheckDiv = 0;
        auto cur = currentVoiceFromParams();
        const auto& mem = bankVoice (currentProgram);
        const bool e = std::memcmp (cur.data(), mem.data(), 145) != 0 || cur[vp::OPSW] != mem[vp::OPSW];
        if (e != edited) { edited = e; sendChangeMessageToUi(); }
    }
}

// ------------------------------------------------------------------------------------------------
void FM9Processor::setKey (int note, bool on)
{
    if (note < 0 || note > 127) return;
    auto& w = keyBits[(size_t) (note >> 5)];
    const uint32_t bit = 1u << (note & 31);
    if (on) w.fetch_or (bit, std::memory_order_relaxed); else w.fetch_and (~bit, std::memory_order_relaxed);
}

void FM9Processor::handleMidi (const juce::MidiMessage& m, int pos, juce::MidiBuffer& toEngine)
{
    if (m.isController())
    {
        const int cc = m.getControllerNumber();
        lastCc.store (cc);
        if (learning.load()) { learnCc.store (cc); return; }
        if (ccTarget[(size_t) cc].load() >= 0 || (ctlProfile.load() == 1 && keyStepTarget[(size_t) cc] >= 0))
        {
            ccValue[(size_t) cc].store ((float) m.getControllerValue());
            ccPending[(size_t) cc].store (true);
            return;
        }
        toEngine.addEvent (m, pos);
        return;
    }
    if (m.isSysEx())
    {
        const int n = m.getRawDataSize();
        if (n <= (int) sysexBuf.size() && sysexSize.load() == 0)
        {
            std::memcpy (sysexBuf.data(), m.getRawData(), (size_t) n);
            sysexSize.store (n);
        }
        return;
    }
    if (m.isNoteOn() || m.isNoteOff())
    {
        const bool rec = p[pSeqRec]->load() > 0.5f;
        const bool on = m.isNoteOn();
        const int note = m.getNoteNumber();
        if (rec)
        {
            const int len = juce::roundToInt (p[pSeqLen]->load());
            const int pat = juce::roundToInt (p[pSeqPattern]->load()) - 1;
            const bool poly = p[pSeqMode]->load() > 0.5f;
            if (on)
            {
                if (! poly)
                {
                    SeqStep st; st.count = 1; st.notes[0] = note; st.vel = m.getVelocity();
                    seqData.put (pat, recStep.load(), st);
                    recStep.store ((recStep.load() + 1) % len);
                }
                else
                {
                    if (recChord.count < 6) { recChord.notes[recChord.count++] = note; recChord.vel = juce::jmax (recChord.vel, (int) m.getVelocity()); }
                    ++recHeld;
                }
            }
            else if (poly)
            {
                recHeld = juce::jmax (0, recHeld - 1);
                if (recHeld == 0 && recChord.count > 0)
                {
                    seqData.put (pat, recStep.load(), recChord);
                    recStep.store ((recStep.load() + 1) % len);
                    recChord = {}; recChord.vel = 1;
                }
            }
            toEngine.addEvent (m, pos);   // on entend ce qu'on enregistre
            return;
        }
        if (p[pArpOn]->load() > 0.5f)
        {
            if (on) arp.keyDown (note, m.getVelocity()); else arp.keyUp (note);
            return;
        }
        toEngine.addEvent (m, pos);
        return;
    }
    if (m.isProgramChange()) { pendingProgram.store (m.getProgramChangeNumber() % (numBanks * bankSize)); return; }
    if (m.isPitchWheel() || m.isChannelPressure() || m.isAllNotesOff() || m.isAllSoundOff())
        toEngine.addEvent (m, pos);
}

void FM9Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if ((int) mono.size() < n) { buffer.clear(); return; }

    // firmware de la FM-1 actif : il remplace le synthe (son, MIDI, ecran)
    fwInUse.store (true);
    if (auto* fw = fwActive.load())
    {
        const int midiCh = juce::roundToInt (p[pMidiCh]->load());
        for (const auto meta : midi)
        {
            const auto& m = meta.getMessage();
            if (midiCh > 0 && m.getChannel() > 0 && m.getChannel() != midiCh) continue;
            if (routeButtonSource (m)) continue;                     // touche / Start / MMC apprise sur un bouton
            if (m.isController() && (learning.load() || ccTarget[(size_t) m.getControllerNumber()].load() >= 0))
            {
                juce::MidiBuffer tmp; handleMidi (m, 0, tmp);       // MIDI Learn (volume...)
                continue;
            }
            if (m.isNoteOn()) setKey (m.getNoteNumber(), true);          // la touche jouee s'eclaire aussi en mode firmware
            else if (m.isNoteOff()) setKey (m.getNoteNumber(), false);
            else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& w : keyBits) w.store (0);
            if (const int oc = fwOutCh.load(); oc > 0 && m.getChannel() > 0)
            {
                auto mm = m; mm.setChannel (oc);                      // tout le clavier sur le canal choisi (Melodee : 1 canal par piste)
                fw->handleMidi (mm);
            }
            else fw->handleMidi (m);
        }
        midi.clear();
        // MASTER de la FM-1 toujours a fond (comme l'appareil monte au maximum) ; le Volume de FM-9 (dB) s'applique ensuite.
        // Avant : Volume a 0 dB = potard MASTER du firmware a 75 % seulement, d'ou un son trop faible avec les firmwares.
        fw->master (fwMasterOverride.load() >= 0.0f ? fwMasterOverride.load() : 1.0f);
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : mono.data();
        fw->render (L, R, n, sr);
        {
            const float g = juce::Decibels::decibelsToGain (p[pVolume]->load());
            for (int i = 0; i < n; ++i)
            {
                float l = L[i] * g, r = R[i] * g;
                if (! std::isfinite (l) || ! std::isfinite (r)) l = r = 0;
                const float lg = limiter.gain (juce::jmax (std::abs (l), std::abs (r)));   // limiteur lie gauche / droite
                L[i] = l * lg; R[i] = r * lg;
            }
        }
        float peak = 0;
        for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (L[i]));
        outLevel.store (juce::jmax (peak, outLevel.load() * 0.9f));
        if (buffer.getNumChannels() == 1) for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]);
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
        fwInUse.store (false);
        return;
    }
    fwInUse.store (false);

    // voix et reglages de jeu
    {
        Voice v {};
        for (int i = 0; i < 145; ++i)
            v[(size_t) i] = (uint8_t) juce::jlimit (0, (int) voiceMax (i), (int) (voiceParams[(size_t) i]->load (std::memory_order_relaxed) + 0.5f));
        uint8_t sw = 0;
        for (int op = 1; op <= 6; ++op) if (opOnP[(size_t) op - 1]->load (std::memory_order_relaxed) > 0.5f) sw |= (uint8_t) (1u << (6 - op));
        v[vp::OPSW] = sw;
        engine.setPatch (v);

        EngineSettings es;
        es.polyphony = juce::roundToInt (p[pPoly]->load());
        es.mono = p[pMono]->load() > 0.5f;
        es.glide = juce::roundToInt (p[pGlide]->load());
        es.fingered = p[pGlideMode]->load() > 0.5f;
        es.bendUp = juce::roundToInt (p[pBendUp]->load());
        es.bendDown = juce::roundToInt (p[pBendDown]->load());
        es.wheelRange = juce::roundToInt (p[pWheel]->load());
        es.wheelTarget = juce::roundToInt (p[pWheelDest]->load());
        es.atRange = juce::roundToInt (p[pAt]->load());
        es.ampEnvOn = p[pAdsr]->load() > 0.5f;
        es.att = p[pAtt]->load(); es.dec = p[pDec]->load(); es.sus = p[pSus]->load(); es.rel = p[pRel]->load();
        engine.setSettings (es);
    }

    // horloge
    double hostBpm = 0, ppq = 0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }
    const bool sync = p[pSync]->load() > 0.5f;
    const double tempo = (sync && hostBpm > 1.0) ? hostBpm : (double) p[pTempo]->load();
    const double bps = tempo / 60.0 / sr;
    ClockInfo freeClk { freeBeat, bps, n };
    ClockInfo hostClk { ppq, bps, n };
    freeBeat += bps * n;
    if (freeBeat > 1.0e7) freeBeat = 0;

    // MIDI entrant
    engineEvents.clear();
    const bool recNow = p[pSeqRec]->load() > 0.5f;
    if (recNow && ! wasRec) { recStep.store (0); recHeld = 0; recChord = {}; recChord.vel = 1; }
    wasRec = recNow;
    const int midiCh = juce::roundToInt (p[pMidiCh]->load());     // 0 = tous les canaux
    for (const auto meta : midi)
    {
        const auto& m = meta.getMessage();
        if (midiCh > 0 && m.getChannel() > 0 && m.getChannel() != midiCh && ! m.isSysEx()) continue;
        if (routeButtonSource (m)) continue;                         // touche / Start / MMC apprise sur un bouton
        if (m.isNoteOnOrOff())
        {
            // OCT- / OCT+ et transposition de la facade s'appliquent aussi au clavier maitre
            const int ch = juce::jlimit (1, 16, m.getChannel()) - 1, in = m.getNoteNumber();
            int out;
            if (m.isNoteOn() && kbFollow.load())
            {
                // la note joue a sa hauteur ; hors des 27 touches affichees (F3 + 12 x octave ... + 26), FM-9 change d'octave
                const int oct = kbOct.load();
                int no = oct;
                while (in < 53 + 12 * no && no > -3) --no;
                while (in > 53 + 12 * no + 26 && no < 3) ++no;
                if (no != oct) { kbOct.store (no); kbOctChanged.store (true); }
                out = in; shiftedNote[ch][in] = (uint8_t) out;
            }
            else if (m.isNoteOn()) { out = juce::jlimit (0, 127, in + kbShift.load()); shiftedNote[ch][in] = (uint8_t) out; }
            else out = shiftedNote[ch][in];
            if (out != in)
            {
                auto mm = m.isNoteOn() ? juce::MidiMessage::noteOn (ch + 1, out, m.getVelocity()) : juce::MidiMessage::noteOff (ch + 1, out, m.getVelocity());
                handleMidi (mm, meta.samplePosition, engineEvents);
                continue;
            }
        }
        handleMidi (m, meta.samplePosition, engineEvents);
    }
    kbEvents.clear();
    keyboardState.processNextMidiBuffer (kbEvents, 0, n, true);   // clavier de la facade
    for (const auto meta : kbEvents) handleMidi (meta.getMessage(), meta.samplePosition, engineEvents);
    midi.clear();
    if (restRequest.exchange (false))
    {
        const int len = juce::roundToInt (p[pSeqLen]->load());
        seqData.put (juce::roundToInt (p[pSeqPattern]->load()) - 1, recStep.load(), SeqStep {});
        recStep.store ((recStep.load() + 1) % len);
    }
    for (int t = tieRequests.exchange (0); t > 0 && recNow; --t)
    {
        const int len = juce::roundToInt (p[pSeqLen]->load());
        SeqStep tie; tie.tie = true;
        seqData.put (juce::roundToInt (p[pSeqPattern]->load()) - 1, recStep.load(), tie);
        recStep.store ((recStep.load() + 1) % len);
    }

    auto emit = [this] (int note, int vel, int off)
    {
        engineEvents.addEvent (vel > 0 ? juce::MidiMessage::noteOn (1, note, (juce::uint8) vel) : juce::MidiMessage::noteOff (1, note), off);
    };

    // arpegiateur
    const bool arpOn = p[pArpOn]->load() > 0.5f;
    arp.setLatch (p[pArpLatch]->load() > 0.5f);
    if (! arpOn && wasArpOn) { arp.clear(); arp.process (freeClk, 0, 1, 3, 0.5f, 0.0f, emit); }
    wasArpOn = arpOn;
    if (arpOn)
        arp.process ((sync && playing) ? hostClk : freeClk, juce::roundToInt (p[pArpMode]->load()), juce::roundToInt (p[pArpOct]->load()),
                     juce::roundToInt (p[pArpRate]->load()), p[pArpGate]->load(), p[pArpSwing]->load(), emit);

    // sequenceur
    const bool seqRunning = p[pSeqActive]->load() > 0.5f && p[pSeqOn]->load() > 0.5f && (! sync || playing || hostBpm <= 1.0);
    if (seqRunning && ! wasSeqRunning) seqFree = 0;
    if (! seqRunning && wasSeqRunning) seq.stop (emit);
    wasSeqRunning = seqRunning;
    if (seqRunning)
    {
        ClockInfo sc = (sync && playing) ? hostClk : ClockInfo { seqFree, bps, n };
        seqFree += bps * n;
        seq.process (seqData, sc, juce::roundToInt (p[pSeqPattern]->load()) - 1, juce::roundToInt (p[pSeqChain]->load()), juce::roundToInt (p[pSeqLen]->load()),
                     p[pSeqMode]->load() > 0.5f, juce::roundToInt (p[pSeqRate]->load()), p[pSeqGate]->load(), p[pSeqSwing]->load(),
                     juce::roundToInt (p[pSeqTrans]->load()), emit);
    }

    // rendu du moteur, decoupe aux evenements
    float* out = mono.data();
    int cursor = 0;
    for (const auto meta : engineEvents)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > cursor) { engine.render (out + cursor, at - cursor); cursor = at; }
        const auto& m = meta.getMessage();
        if (m.isNoteOn()) { engine.noteOn (m.getNoteNumber(), m.getVelocity()); setKey (m.getNoteNumber(), true); }
        else if (m.isNoteOff()) { engine.noteOff (m.getNoteNumber()); setKey (m.getNoteNumber(), false); }
        else if (m.isPitchWheel()) engine.pitchBend (m.getPitchWheelValue());
        else if (m.isChannelPressure()) engine.aftertouch (m.getChannelPressureValue());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { engine.allNotesOff(); for (auto& w : keyBits) w.store (0); }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            if (cc == 1) engine.modWheel (m.getControllerValue());
            else if (cc == 64) engine.sustain (m.getControllerValue() >= 64);
            else if (cc == 120 || cc == 123) { engine.allNotesOff(); for (auto& w : keyBits) w.store (0); }
        }
    }
    if (cursor < n) engine.render (out + cursor, n - cursor);

    // effets
    FxSettings fs;
    for (int f = 0; f < fxCount; ++f)
    {
        fs.on[f] = fxP[f][0]->load() > 0.5f;
        fs.mix[f] = fxP[f][1]->load(); fs.a[f] = fxP[f][2]->load(); fs.b[f] = fxP[f][3]->load();
    }
    fs.filterType = juce::roundToInt (p[pFilterType]->load());
    fs.reverbType = juce::roundToInt (p[pReverbType]->load());
    fs.delayMs = p[pDelaySync]->load() > 0.5f ? delayDivBeats (juce::roundToInt (p[pDelayDiv]->load())) * 60000.0 / tempo : 0.0;
    fs.order = getFxOrder();
    fx.process (out, n, fs);

    const float g = juce::Decibels::decibelsToGain (p[pVolume]->load());
    float peak = 0;
    for (int i = 0; i < n; ++i)
    {
        float y = out[i] * g * synthMakeup;
        if (! std::isfinite (y)) y = 0;
        y *= limiter.gain (std::abs (y));                       // jamais au-dessus de -0.5 dBFS
        out[i] = y;
        peak = juce::jmax (peak, std::abs (y));
    }
    outLevel.store (juce::jmax (peak, outLevel.load() * 0.9f));
    {
        int sp = scopePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { scope[(size_t) sp] = out[i]; sp = (sp + 1) % scopeSize; }
        scopePos.store (sp, std::memory_order_release);
    }
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.copyFrom (ch, 0, out, n);
}

// ------------------------------------------------------------------------------------------------
void FM9Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("voiceName", voiceName, nullptr);
    state.setProperty ("seq", seqData.toString(), nullptr);
    state.setProperty ("firmware", firmwareName, nullptr);
    state.setProperty ("fwMhz", fm9::FirmwareHost::hwMhz().load(), nullptr);
    state.setProperty ("fwBuffer", fm9::FirmwareHost::hwMargin().load(), nullptr);
    state.setProperty ("fwAuto", fm9::FirmwareHost::hwAutoMargin().load(), nullptr);
    { juce::StringArray pv; for (auto& v : patternVoice) pv.add (juce::String (v.load())); state.setProperty ("patternVoice", pv.joinIntoString (","), nullptr); }
    juce::StringArray order; for (int o : getFxOrder()) order.add (juce::String (o));
    state.setProperty ("fxOrder", order.joinIntoString (","), nullptr);
    state.setProperty ("midiMap", midiMapString(), nullptr);
    state.setProperty ("ctlProfile", ctlProfile.load(), nullptr);
    state.setProperty ("fwOutCh", fwOutCh.load(), nullptr);
    state.removeChild (state.getChildWithName ("UI"), nullptr);
    state.appendChild (uiState.createCopy(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void FM9Processor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return;
    if (auto ui = state.getChildWithName ("UI"); ui.isValid()) uiState.copyPropertiesFrom (ui, nullptr);
    state.removeChild (state.getChildWithName ("UI"), nullptr);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
    voiceName = state.getProperty ("voiceName", "INIT VOICE").toString();
    seqData.fromString (state.getProperty ("seq").toString());
    if (state.hasProperty ("fwMhz")) fm9::FirmwareHost::hwMhz().store (juce::jlimit (60, 480, (int) state.getProperty ("fwMhz")));
    if (state.hasProperty ("fwAuto")) fm9::FirmwareHost::hwAutoMargin().store ((bool) state.getProperty ("fwAuto"));
    if (state.hasProperty ("fwBuffer")) fm9::FirmwareHost::hwMargin().store (juce::jlimit (512, 8192, (int) state.getProperty ("fwBuffer")));
    {
        const auto fwName = state.getProperty ("firmware").toString();
        pendingFirmware = fwName.isEmpty() ? juce::String ("-") : fwName;    // demarre par le timer (thread de messages)
    }
    {
        auto pv = juce::StringArray::fromTokens (state.getProperty ("patternVoice").toString(), ",", "");
        for (int i = 0; i < 16; ++i) patternVoice[(size_t) i].store (i < pv.size() ? juce::jlimit (-1, 127, pv[i].getIntValue()) : -1);
    }
    auto order = juce::StringArray::fromTokens (state.getProperty ("fxOrder").toString(), ",", "");
    if (order.size() == fxCount)
    {
        std::array<int, fxCount> o {}; bool ok = true; int seen = 0;
        for (int i = 0; i < fxCount; ++i) { o[(size_t) i] = juce::jlimit (0, fxCount - 1, order[i].getIntValue()); seen |= 1 << o[(size_t) i]; }
        ok = seen == (1 << fxCount) - 1;
        if (ok) setFxOrder (o);
    }
    if (state.hasProperty ("ctlProfile")) ctlProfile.store ((int) state.getProperty ("ctlProfile"));
    fwOutCh.store (juce::jlimit (0, 16, (int) state.getProperty ("fwOutCh", 0)));
    applyMidiMapString (state.getProperty ("midiMap").toString());
    pendingDefaultMap = false;                       // le projet a sa propre configuration
    kbShift.store (12 * (int) uiState.getProperty ("octave", 0) + (int) uiState.getProperty ("transpose", 0));
    kbOct.store ((int) uiState.getProperty ("octave", 0));
    kbFollow.store ((bool) uiState.getProperty ("kbFollow", true));
    apvts.replaceState (state);
    sendChangeMessageToUi();
}

juce::AudioProcessorEditor* FM9Processor::createEditor() { return new FM9Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FM9Processor(); }

// ------------------------------------------------------------------------------------------------ Arturia KeyStep 37
// CC d'usine des 4 banques de potards de la KeyStep 37 (manuel Arturia, section 8.2), mode Control (Shift + Chord).
// Profil optionnel, choisi dans le menu MIDI : aucun CC n'est assigne tant qu'il n'est pas active.
const FM9Processor::KeyStepMap FM9Processor::keyStepMap[16] = {
    { 74, "filter_a",   "Cutoff" },     { 71, "filter_b",   "Resonance" },   { 76, "fb",        "Feedback" },     { 77, "glide",     "Glide" },        // banque blanche
    { 73, "attack",     "Attack" },     { 75, "decay",      "Decay" },       { 79, "sustain",   "Sustain" },      { 72, "release",   "Release" },      // banque bleue
    { 18, "reverb_mix", "Reverb" },     { 19, "delay_mix",  "Delay" },       { 16, "chorus_mix", "Chorus" },      { 17, "drive_mix", "Distortion" },   // banque violette
    { 80, "lfs",        "LFO Speed" },  { 81, "lpmd",       "LFO Pitch" },   { 82, "lamd",      "LFO Amp" },      { 83, "phaser_mix", "Phaser" } };   // banque verte

juce::StringArray FM9Processor::keyStepBankNames()
{
    juce::StringArray out;
    const char* banks[4] = { "White", "Blue", "Purple", "Green" };
    for (int b = 0; b < 4; ++b)
    {
        juce::StringArray k;
        for (int i = 0; i < 4; ++i) k.add (juce::String (keyStepMap[b * 4 + i].label) + " (CC" + juce::String (keyStepMap[b * 4 + i].cc) + ")");
        out.add (juce::String (banks[b]) + ": " + k.joinIntoString (", "));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ NKS
juce::File FM9Processor::nksUserFolder()
{
    // bibliotheque utilisateur de Komplete Kontrol / Maschine
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Native Instruments").getChildFile ("User Content").getChildFile ("FM-9");
}

static void nksType (const juce::String& name, juce::String& type, juce::String& sub)
{
    const auto n = name.toUpperCase();
    auto has = [&n] (std::initializer_list<const char*> w) { for (auto* x : w) if (n.contains (x)) return true; return false; };
    if (has ({ "PIANO", "EP", "RHODES", "WURLI", "KEYS", "CLAV", "HARPSI" })) { type = "Piano / Keys"; sub = has ({ "EP", "RHODES", "WURLI" }) ? "Electric Piano" : "Piano"; }
    else if (has ({ "ORGAN" }))                                           { type = "Organ"; sub = "Electric Organ"; }
    else if (has ({ "BASS" }))                                            { type = "Bass"; sub = "Synth Bass"; }
    else if (has ({ "BRASS", "HORN", "TRUMP" }))                          { type = "Brass"; sub = ""; }
    else if (has ({ "STRING", "VIOLIN", "CELLO" }))                       { type = "Bowed Strings"; sub = "Ensemble"; }
    else if (has ({ "BELL", "MARIMBA", "VIBE", "GLOCK", "TUBULAR", "CHIME" })) { type = "Mallet Instruments"; sub = has ({ "BELL", "CHIME" }) ? "Bell" : ""; }
    else if (has ({ "PAD", "SPACE", "WIND", "ATMOS", "CHOIR", "SWEEP" })) { type = "Synth Pad"; sub = ""; }
    else if (has ({ "LEAD", "SYNC", "SQUARE", "SAW" }))                   { type = "Synth Lead"; sub = ""; }
    else if (has ({ "GUITAR", "HARP", "KOTO", "PLUCK" }))                 { type = "Plucked Strings"; sub = ""; }
    else if (has ({ "FLUTE", "SAX", "OBOE", "CLARINET" }))                { type = "Reed Instruments"; sub = ""; }
    else if (has ({ "DRUM", "PERC", "KICK", "SNARE", "TOM" }))            { type = "Drums"; sub = ""; }
    else if (has ({ "FX", "NOISE", "ZAP" }))                              { type = "Sound Effects"; sub = ""; }
    else                                                                  { type = "Synth Misc"; sub = ""; }
}

static std::vector<nks::Page> nksPages()
{
    using P = nks::Param;
    std::vector<nks::Page> pages;
    pages.push_back ({ "Main",      { P { "volume", "Volume" }, P { "alg", "Algorithm" }, P { "fb", "Feedback" }, P { "trnsp", "Transpose" },
                                      P { "filter_a", "Cutoff" }, P { "filter_b", "Resonance" }, P { "reverb_mix", "Reverb" }, P { "delay_mix", "Delay" } } });
    pages.push_back ({ "Operators", { P { fm9::opParamId (1, 16), "OP1 Level" }, P { fm9::opParamId (2, 16), "OP2 Level" }, P { fm9::opParamId (3, 16), "OP3 Level" },
                                      P { fm9::opParamId (4, 16), "OP4 Level" }, P { fm9::opParamId (5, 16), "OP5 Level" }, P { fm9::opParamId (6, 16), "OP6 Level" },
                                      P { "glide", "Glide" }, P { "mono", "Mono" } } });
    pages.push_back ({ "Envelope",  { P { "adsrOn", "ADSR" }, P { "attack", "Attack" }, P { "decay", "Decay" }, P { "sustain", "Sustain" }, P { "release", "Release" },
                                      P { "wheel", "Mod Wheel" }, P { "atRange", "Aftertouch" }, P { "bendUp", "Bend Up" } } });
    pages.push_back ({ "LFO",       { P { "lfs", "Speed" }, P { "lfd", "Delay" }, P { "lpmd", "Pitch Depth" }, P { "lamd", "Amp Depth" }, P { "lfw", "Wave" },
                                      P { "lfks", "Key Sync" }, P { "lpms", "Pitch Sens" }, P { "oks", "Osc Sync" } } });
    pages.push_back ({ "Effects",   { P { "filter_on", "Filter" }, P { "chorus_mix", "Chorus" }, P { "phaser_mix", "Phaser" }, P { "drive_mix", "Distortion" },
                                      P { "drive_a", "Dist Gain" }, P { "delay_a", "Delay Rate" }, P { "delay_b", "Delay Decay" }, P { "reverb_a", "Rev Decay" } } });
    pages.push_back ({ "Arp / Seq", { P { "arpOn", "Arp" }, P { "arpMode", "Arp Mode" }, P { "arpOct", "Arp Oct" }, P { "arpRate", "Arp Rate" }, P { "arpGate", "Arp Gate" },
                                      P { "seqOn", "Seq Play" }, P { "seqPattern", "Pattern" }, P { "tempo", "Tempo" } } });
    return pages;
}

bool FM9Processor::exportNksPreset (int program, const juce::File& file)
{
    // program < 0 : le son en cours, tel qu'il est (retouches comprises), sans toucher aux banques
    nks::Preset pr;
    pr.name = program < 0 ? voiceName.trim() : juce::String (fm9::voiceName (bankVoice (program))).trim();
    pr.bank = program < 0 ? juce::String ("User") : "Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + program / bankSize));
    nksType (pr.name, pr.type, pr.subType);
    if (program >= 0) setCurrentProgram (program);
    // etat du preset : le son seulement (pas de MIDI Learn, de skin ni de firmware)
    auto state = apvts.copyState();
    state.setProperty ("program", program < 0 ? currentProgram : program, nullptr);
    state.setProperty ("voiceName", pr.name, nullptr);
    state.removeChild (state.getChildWithName ("UI"), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, pr.state);
    const auto data = nks::build (pr, nksPages(), "NINE HANDS | DSP", "FM-9", "Wiga", "WgF9");
    file.getParentDirectory().createDirectory();
    return file.replaceWithData (data.getData(), data.getSize());
}

int FM9Processor::exportNksLibrary (const juce::File& root)
{
    juce::MemoryBlock keep;
    getStateInformation (keep);                                  // on remet le son en cours apres l'export
    int n = 0;
    for (int i = 0; i < getNumPrograms(); ++i)
    {
        const auto name = juce::String (fm9::voiceName (bankVoice (i))).trim();
        if (i >= bankSize && name == "INIT VOICE") continue;    // emplacements vides des banques B-D
        const auto bank = "Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + i / bankSize));
        const auto f = root.getChildFile (bank).getChildFile (juce::File::createLegalFileName (programLabel (i) + " " + name) + ".nksf");
        if (exportNksPreset (i, f)) ++n;
    }
    setStateInformation (keep.getData(), (int) keep.getSize());
    return n;
}
