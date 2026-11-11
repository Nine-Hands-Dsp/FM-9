// FM-9 (NINE HANDS | DSP) - synthe FM 6 operateurs d'apres la M-VAVE FM-1
#include "PluginProcessor.h"
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
    for (int i = 0; i < 128; ++i) { ccTarget[(size_t) i].store (-1); ccValue[(size_t) i].store (0); ccPending[(size_t) i].store (false); }

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

bool FM9Processor::activateFirmware (const juce::String& name, juce::String& error)
{
    std::unique_ptr<fm9::FirmwareHost> next;
    if (name.isNotEmpty())
    {
        juce::MemoryBlock wasm;
        const auto file = fm9::FirmwareInstaller::folder().getChildFile (juce::File::createLegalFileName (name) + ".wasm");
        if (file.existsAsFile()) file.loadFileAsData (wasm);
        else if (name.equalsIgnoreCase (builtinFirmware)) wasm.append (BinaryData::hortator_wasm, (size_t) BinaryData::hortator_wasmSize);
        else { error = "firmware not found: " + name; return false; }
        next = fm9::FirmwareHost::create (wasm.getData(), wasm.getSize(),
                                          fm9::FirmwareInstaller::folder().getChildFile (juce::File::createLegalFileName (name) + ".flash"), error);
        if (next == nullptr) return false;
    }
    // echange sans verrou : le thread audio ne garde jamais l'ancien firmware apres cette attente
    auto* old = fwOwner.get();
    fwActive.store (next.get());
    for (int i = 0; i < 400 && fwInUse.load(); ++i) juce::Thread::sleep (1);
    if (old != nullptr) old->saveIfDirty();
    fwOwner = std::move (next);
    firmwareName = name;
    engine.allSoundOff();
    sendChangeMessageToUi();
    return true;
}

bool FM9Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FM9Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
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
void FM9Processor::clearMidiLearn (const juce::String& paramId)
{
    for (int c = 0; c < 128; ++c)
    {
        const int t = ccTarget[(size_t) c].load();
        if (t >= 0 && allParams[t]->getParameterID() == paramId) ccTarget[(size_t) c].store (-1);
    }
}

int FM9Processor::learnedCcFor (const juce::String& paramId) const
{
    for (int c = 0; c < 128; ++c)
    {
        const int t = ccTarget[(size_t) c].load();
        if (t >= 0 && allParams[t]->getParameterID() == paramId) return c;
    }
    return -1;
}

void FM9Processor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // apprentissage MIDI
    const int lc = learnCc.exchange (-1);
    if (lc >= 0 && learning.load())
    {
        int target = -1;
        for (int i = 0; i < allParams.size(); ++i) if (allParams[i]->getParameterID() == learnTarget) target = i;
        if (target >= 0)
        {
            clearMidiLearn (learnTarget);
            ccTarget[(size_t) lc].store (target);
            ramps[(size_t) lc] = {};
        }
        learning.store (false);
        learnTarget.clear();
        sendChangeMessageToUi();
    }

    // CC appris -> parametres, avec rampe et course apprise
    for (int c = 0; c < 128; ++c)
    {
        auto& r = ramps[(size_t) c];
        const int t = ccTarget[(size_t) c].load();
        if (t < 0) { r.active = false; continue; }
        auto* prm = allParams[t];
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
        if (ccTarget[(size_t) cc].load() >= 0)
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
            if (m.isController() && (learning.load() || ccTarget[(size_t) m.getControllerNumber()].load() >= 0))
            {
                juce::MidiBuffer tmp; handleMidi (m, 0, tmp);       // MIDI Learn (volume...)
                continue;
            }
            fw->handleMidi (m);
        }
        midi.clear();
        if (auto* vol = apvts.getParameter ("volume")) fw->master (vol->getValue());
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : mono.data();
        fw->render (L, R, n, sr);
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
        float y = out[i] * g;
        if (! std::isfinite (y)) y = 0;
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
    { juce::StringArray pv; for (auto& v : patternVoice) pv.add (juce::String (v.load())); state.setProperty ("patternVoice", pv.joinIntoString (","), nullptr); }
    juce::StringArray order; for (int o : getFxOrder()) order.add (juce::String (o));
    state.setProperty ("fxOrder", order.joinIntoString (","), nullptr);
    juce::StringArray map;
    for (int c = 0; c < 128; ++c)
        if (const int t = ccTarget[(size_t) c].load(); t >= 0) map.add (juce::String (c) + "=" + allParams[t]->getParameterID());
    state.setProperty ("midiMap", map.joinIntoString (";"), nullptr);
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
    for (int c = 0; c < 128; ++c) ccTarget[(size_t) c].store (-1);
    for (auto& e : juce::StringArray::fromTokens (state.getProperty ("midiMap").toString(), ";", ""))
    {
        const int c = e.upToFirstOccurrenceOf ("=", false, false).getIntValue();
        const auto id = e.fromFirstOccurrenceOf ("=", false, false);
        for (int i = 0; i < allParams.size(); ++i)
            if (allParams[i]->getParameterID() == id && c >= 0 && c < 128) ccTarget[(size_t) c].store (i);
    }
    apvts.replaceState (state);
    sendChangeMessageToUi();
}

juce::AudioProcessorEditor* FM9Processor::createEditor() { return new FM9Editor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FM9Processor(); }
