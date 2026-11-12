// FM-9 : tests audio hors DAW + captures de la fenetre
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FirmwareCatalog.h"

struct FakeHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0; bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing); return p;
    }
};

static int fails = 0;
static void check (bool ok, const juce::String& what) { std::printf ("%s %s\n", ok ? "  OK " : "ECHEC", what.toRawUTF8()); if (! ok) ++fails; }

struct Run
{
    FM9Processor p; FakeHead head; juce::AudioBuffer<float> buf { 2, 512 }; double sr = 48000;
    Run() { p.setPlayHead (&head); p.prepareToPlay (sr, 512); }
    // joue nBlocks blocs, renvoie crete et nombre de notes demarrees
    float play (int nBlocks, juce::MidiBuffer first = {}, std::vector<float>* rec = nullptr)
    {
        float pk = 0;
        for (int b = 0; b < nBlocks; ++b)
        {
            juce::MidiBuffer m; if (b == 0) m = first;
            buf.clear(); p.processBlock (buf, m);
            if (head.playing) head.ppq += 512 / sr * head.bpm / 60.0;
            for (int i = 0; i < 512; ++i) { pk = std::max (pk, std::abs (buf.getSample (0, i))); if (rec) rec->push_back (buf.getSample (0, i)); }
        }
        return pk;
    }
    void set (const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
};

static int countOnsets (const std::vector<float>& x, int win = 256)
{
    // compte les attaques : energie qui remonte nettement
    int c = 0; float prev = 0; bool armed = true;
    for (size_t i = 0; i + (size_t) win < x.size(); i += (size_t) win)
    {
        float e = 0; for (int k = 0; k < win; ++k) e += x[i + (size_t) k] * x[i + (size_t) k];
        e = std::sqrt (e / win);
        if (armed && e > prev * 1.8f && e > 0.01f) { ++c; armed = false; }
        if (e < prev * 0.9f) armed = true;
        prev = e;
    }
    return c;
}

static void keyStepTest()
{
    std::printf ("Arturia KeyStep 37\n");
    FM9Processor p; p.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> buf (2, 512);
    auto send = [&] (int cc, int v)
    {
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, cc, v), 0);
        p.processBlock (buf, m);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);    // le timer applique les CC (rampe anti-zip)
    };
    auto val = [&] (const char* id) { return p.apvts.getParameter (id)->getValue(); };
    const float before = val ("reverb_mix");
    send (18, 127);
    check (std::abs (val ("reverb_mix") - before) < 1.0e-4f, "profil desactive par defaut : CC18 ignore (MIDI Learn seulement)");
    p.ctlProfile.store (1);
    send (18, 127);
    check (val ("reverb_mix") > 0.95f && val ("reverb_on") > 0.5f, "banque violette : CC18 -> Reverb mix, reverb mise en marche");
    send (74, 0);
    check (val ("filter_a") < 0.05f && val ("filter_on") > 0.5f, "banque blanche : CC74 -> Cutoff, filtre en marche");
    send (73, 127);
    check (val ("attack") > 0.95f && val ("adsrOn") > 0.5f, "banque bleue : CC73 -> Attack, ADSR en marche");
    send (80, 127);
    check (val ("lfs") > 0.95f, "banque verte : CC80 -> LFO Speed");
    juce::MemoryBlock st; p.getStateInformation (st);
    FM9Processor q; q.setStateInformation (st.getData(), (int) st.getSize());
    check (q.ctlProfile.load() == 1, "profil KeyStep 37 memorise avec le projet");
}

static void midiTest()
{
    std::printf ("Octave du clavier maitre, MIDI Learn des commandes, configurations MIDI\n");
    FM9Processor p; p.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> buf (2, 512);
    auto block = [&] (juce::MidiMessage m) { juce::MidiBuffer mb; mb.addEvent (m, 0); p.processBlock (buf, mb); juce::MessageManager::getInstance()->runDispatchLoopUntil (150); };
    auto on = [&] (int n) { return ((p.keyBits[n >> 5].load() >> (n & 31)) & 1u) != 0; };
    // par defaut : les touches de FM-9 suivent l'octave du clavier maitre
    block (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    check (on (60) && p.kbOct.load() == 0, "clavier maitre : la note 60 joue 60, octave de FM-9 inchangee (F3-G5)");
    block (juce::MidiMessage::noteOff (1, 60));
    block (juce::MidiMessage::noteOn (1, 84, (juce::uint8) 100));
    check (on (84) && p.kbOct.load() == 1 && (int) p.uiState.getProperty ("octave", 0) == 1, "note au-dessus de la plage : FM-9 passe a l'octave +1");
    block (juce::MidiMessage::noteOff (1, 84));
    block (juce::MidiMessage::noteOn (1, 45, (juce::uint8) 100));
    check (on (45) && p.kbOct.load() == -1, "note en dessous : FM-9 descend a l'octave -1");
    block (juce::MidiMessage::noteOff (1, 45));
    p.kbFollow.store (false); p.kbOct.store (0);
    p.kbShift.store (12);
    block (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    check (on (72) && ! on (60), "option OCT transpose : la note 60 du clavier maitre joue 72");
    p.kbShift.store (0);
    block (juce::MidiMessage::noteOff (1, 60));
    check (! on (72), "relachement correct meme si l'octave a change entre-temps");
    p.kbFollow.store (true);
    p.uiState.setProperty ("octave", 0, nullptr); p.kbOct.store (0); p.kbShift.store (0);
    p.applyMidiMapString ("20=ctl:presets;21=ctl:knob1;22=volume");
    check (p.learnedCcFor ("ctl:presets") == 20 && p.learnedCcFor ("ctl:knob1") == 21, "commandes PRESETS et KNOB1 assignables");
    const int prog = p.getCurrentProgram();
    block (juce::MidiMessage::controllerEvent (1, 20, 10));
    block (juce::MidiMessage::controllerEvent (1, 20, 11));
    check (p.getCurrentProgram() == prog + 1, "CC sur PRESETS : preset suivant (interface fermee)");
    p.ctlUi.store (true);
    block (juce::MidiMessage::controllerEvent (1, 21, 40));
    block (juce::MidiMessage::controllerEvent (1, 21, 43));
    check (p.ctlSteps[3].load() == 3, "CC sur KNOB1 : 3 pas transmis a l'interface");
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("t.fm9midi");
    check (p.saveMidiMap (f), "configuration MIDI enregistree");
    FM9Processor q; check (q.loadMidiMap (f) && q.learnedCcFor ("ctl:knob1") == 21 && q.learnedCcFor ("volume") == 22, "configuration MIDI rechargee");
    juce::MemoryBlock st; p.getStateInformation (st);
    FM9Processor r; r.setStateInformation (st.getData(), (int) st.getSize());
    check (r.learnedCcFor ("ctl:presets") == 20, "assignations des commandes gardees avec le projet");
    f.deleteFile();
    p.ctlUi.store (false);
    // boutons de fonction : MIDI Learn sur un CC, une touche, une commande MMC (boutons de transport du clavier)
    p.ctlUi.store (true);
    const int fx = FM9Processor::firstFn;           // FX
    p.startMidiLearn ("ctl:btn_fx"); block (juce::MidiMessage::controllerEvent (1, 64, 127));
    check (p.learnedCcFor ("ctl:btn_fx") == 64, "MIDI Learn de FX sur un CC");
    for (auto& c : p.ctlSteps) c.store (0); for (auto& c : p.ctlRel) c.store (0);
    block (juce::MidiMessage::controllerEvent (1, 64, 0)); block (juce::MidiMessage::controllerEvent (1, 64, 127)); block (juce::MidiMessage::controllerEvent (1, 64, 0));
    check (p.ctlSteps[(size_t) fx].load() == 1 && p.ctlRel[(size_t) fx].load() >= 1, "CC sur FX : appui et relachement transmis");
    p.startMidiLearn ("ctl:btn_play"); block (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100)); block (juce::MidiMessage::noteOff (1, 36));
    check (p.learnedSourceName ("ctl:btn_play") == "Note C1", "MIDI Learn de PLAY/STOP sur une touche (" + p.learnedSourceName ("ctl:btn_play") + ")");
    const int play = fx + 10;
    for (auto& c : p.ctlSteps) c.store (0);
    block (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
    check (p.ctlSteps[(size_t) play].load() == 1 && ! on (36), "la touche apprise appuie sur PLAY/STOP sans jouer de note");
    p.startMidiLearn ("ctl:btn_rec"); const juce::uint8 mmc[] = { 0x7f, 0x7f, 0x06, 0x06 };
    block (juce::MidiMessage::createSysExMessage (mmc, 4));
    check (p.learnedSourceName ("ctl:btn_rec") == "MMC Record", "MIDI Learn de REC sur le bouton Record du clavier (MMC)");
    p.startMidiLearn ("ctl:btn_seq"); block (juce::MidiMessage::midiStart());
    check (p.learnedSourceName ("ctl:btn_seq") == "Start", "MIDI Learn de SEQ sur Start");
    FM9Processor q2; q2.applyMidiMapString (p.midiMapString());
    check (q2.learnedSourceName ("ctl:btn_play") == "Note C1" && q2.learnedSourceName ("ctl:btn_rec") == "MMC Record", "boutons appris gardes dans la configuration MIDI");
    p.ctlUi.store (false);
    p.applyMidiMapString ("30=ctl:octup");
    block (juce::MidiMessage::controllerEvent (1, 30, 127));
    block (juce::MidiMessage::controllerEvent (1, 30, 0));
    block (juce::MidiMessage::controllerEvent (1, 30, 127));
    check (p.kbShift.load() == 24, "OCT+ en MIDI Learn (bouton) : deux appuis = +2 octaves");
    {
        FM9Editor ed (p); ed.setVisible (true);
        const float cut0 = p.apvts.getParameter ("filter_a")->getValue();
        p.ctlSteps[3].store (10);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        check (p.apvts.getParameter ("filter_a")->getValue() > cut0 && p.apvts.getParameter ("filter_on")->getValue() > 0.5f,
               "KNOB1 appris, page d'accueil : regle le Cutoff et met le filtre en marche");
        const float sc = (float) ed.getHeight() / fm9::layout::unitsH;
        for (auto kp : { fm9::layout::knob_knob1, fm9::layout::knob_knob4, fm9::layout::knob_select, fm9::layout::knob_presets })
        {
            auto* c = ed.getComponentAt ((kp * sc).toInt());
            std::printf ("    editeur %dx%d, point %d,%d -> %s %s\n", ed.getWidth(), ed.getHeight(), (kp * sc).toInt().x, (kp * sc).toInt().y, c == nullptr ? "null" : typeid (*c).name(), c == nullptr ? "" : c->getBounds().toString().toRawUTF8());
            check (dynamic_cast<nh::Encoder*> (c) != nullptr, "clic droit au centre du potard : c'est bien le potard qui le recoit");
        }
    }
}

static void deleteTest()
{
    std::printf ("Suppression d'un firmware\n");
    auto d = fm9::FirmwareInstaller::folder(); d.createDirectory(); d.getChildFile ("Previews").createDirectory();
    for (auto ext : { ".wasm", ".fwsc", ".txt", ".hwflash" }) d.getChildFile (juce::String ("ZzTest") + ext).replaceWithText ("x");
    d.getChildFile ("ZzTest [fork].wasm").replaceWithText ("x");
    d.getChildFile ("Previews").getChildFile ("ZzTest.png").replaceWithText ("x");
    check (fm9::FirmwareInstaller::installed().contains ("ZzTest (hardware)"), "firmware de test installe");
    check (fm9::FirmwareInstaller::remove ("ZzTest (hardware)") == 5, "tout est supprime : web, paquet, notes, sauvegardes, apercu");
    check (! fm9::FirmwareInstaller::installed().contains ("ZzTest") && d.getChildFile ("ZzTest [fork].wasm").existsAsFile(), "un firmware au nom voisin n'est pas touche");
    d.getChildFile ("ZzTest [fork].wasm").deleteFile();
}

static void golanaTest()
{
    // la page de Golana contient sa version native : extraite, elle demarre et joue
    juce::MemoryBlock page;
    if (! juce::File ("/tmp/golana/try.html").loadFileAsData (page)) return;
    std::printf ("Golana (page web)\n");
    check (fm9::FirmwareCatalog::extractWasm (page) && fm9::FirmwareInstaller::abiOf (page.getData(), page.getSize()) != 0, "version web extraite de la page Try it in your browser");
    auto entries = fm9::FirmwareCatalog::parse (juce::File ("/home/claude/fm-9/catalog/firmwares.json").loadFileAsString());
    for (auto& e : entries) if (e.name == "Golana")
    {
        check (e.preferWeb && e.web(), "catalogue : Golana prefere sa version native");
        juce::StringArray inst { "Golana (hardware)" };
        check (! e.isInstalled (inst), "Golana materiel seul : Install propose de prendre la version native");
        inst.add ("Golana");
        check (e.loadName (inst) == "Golana", "Load lance la version native");
    }
}

static void firmwareTest()
{
    std::printf ("== firmware Hortator ==\n");
    Run r; juce::String err;
    const bool ok = r.p.activateFirmware ("Hortator", err);
    check (ok, "Hortator demarre dans FM-9 " + err);
    if (! ok) return;
    auto* fw = r.p.firmware();
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    r.play (200);
    fw->button (10, true); r.play (2); fw->button (10, false);
    std::vector<float> x; r.play ((int) (48000 * 4 / 512), {}, &x);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    float pk = 0; for (float v : x) pk = std::max (pk, std::abs (v));
    check (pk > 0.01f && pk < 4.0f, "PLAY : la boite a rythmes joue (crete " + juce::String (pk, 3) + ")");
    check (ms < 6.2 * 1000.0 / 2.0, "temps reel : " + juce::String ((6.2 * 1000.0) / ms, 1) + "x plus vite que le temps reel");
    check (fw->fbSerial.load() > 2, "ecran du firmware mis a jour (" + juce::String ((int) fw->fbSerial.load()) + " images)");
    juce::MemoryBlock mb; r.p.getStateInformation (mb);
    Run b; b.p.setStateInformation (mb.getData(), (int) mb.getSize());
    juce::Thread::sleep (50);
    // le timer du processeur demarre le firmware memorise
    for (int i = 0; i < 20 && b.p.getFirmwareName().isEmpty(); ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }
    check (b.p.getFirmwareName() == "Hortator", "projet : firmware memorise et relance (" + b.p.getFirmwareName() + ")");
    r.p.activateFirmware ({}, err);
    check (r.p.firmware() == nullptr, "retour au synthe FM-9");
}

static void audioTests()
{
    std::printf ("== tests audio ==\n");
    {
        Run r; juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 10);
        const float pk = r.play (40, m);
        check (pk > 0.05f && pk < 2.0f, "note jouee (crete " + juce::String (pk, 3) + ")");
        juce::MidiBuffer off; off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        r.play (1, off); r.play (300); const float tail = r.play (100);
        check (tail < 0.01f, "note relachee, silence ensuite (" + juce::String (tail, 4) + ")");
    }
    {
        Run r; int ok = 0;
        for (int prog = 0; prog < 32; ++prog)
        {
            r.p.setCurrentProgram (prog);
            juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            const float pk = r.play (60, m);
            juce::MidiBuffer off; off.addEvent (juce::MidiMessage::allNotesOff (1), 0); r.play (300, off);
            if (pk > 0.02f && pk < 1.5f) ++ok; else std::printf ("     %s crete %.3f\n", r.p.getProgramName (prog).toRawUTF8(), pk);
        }
        check (ok == 32, "32 sons d'usine audibles sans saturation (" + juce::String (ok) + "/32)");
    }
    {
        Run r; r.set ("arpOn", 1); r.set ("arpRate", 3); r.set ("sync", 0); r.set ("tempo", 120);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        r.p.setCurrentProgram (17); // marimba : attaques nettes
        std::vector<float> x; r.play ((int) (48000 * 2 / 512), m, &x);
        const int on = countOnsets (x);
        check (on >= 12 && on <= 20, "arpegiateur 1/16 a 120 BPM : ~16 notes en 2 s (" + juce::String (on) + ")");
    }
    {
        Run r; r.p.setCurrentProgram (17);
        r.set ("seqActive", 1); r.set ("seqRec", 1); r.set ("seqMode", 0);
        juce::MidiBuffer m;
        for (int i = 0; i < 4; ++i) { m.addEvent (juce::MidiMessage::noteOn (1, 60 + i, (juce::uint8) 100), i * 4); m.addEvent (juce::MidiMessage::noteOff (1, 60 + i), i * 4 + 2); }
        r.play (2, m);
        check (r.p.recStep.load() == 4, "enregistrement pas a pas : 4 pas ecrits (" + juce::String (r.p.recStep.load()) + ")");
        r.set ("seqRec", 0); r.set ("seqLen", 4); r.set ("sync", 1); r.set ("seqOn", 1);
        r.head.playing = true; r.head.ppq = 0;
        std::vector<float> x; r.play ((int) (48000 * 2 / 512), {}, &x);
        const int on = countOnsets (x);
        check (on >= 12 && on <= 20, "sequenceur synchro hote : ~16 pas en 2 s (" + juce::String (on) + ")");
        r.head.playing = false; r.play (20);
        check (r.p.seq.playhead.load() == -1, "sequenceur arrete avec le transport");
    }
    {
        Run r; r.p.setCurrentProgram (0);
        for (int f = 0; f < 6; ++f) r.set (fm9::fxId (f, "on").toRawUTF8(), 1);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 120), 0); m.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 120), 0);
        const float pk = r.play (200, m);
        check (pk > 0.02f && pk < 4.0f && std::isfinite (pk), "6 effets actifs, signal propre (crete " + juce::String (pk, 3) + ")");
    }
    {
        Run a; a.p.setCurrentProgram (8); a.set ("op1_ol", 77); a.set ("alg", 4); a.set ("arpOn", 1);
        fm9::SeqStep st; st.count = 2; st.notes[0] = 60; st.notes[1] = 67; st.vel = 90; a.p.seqData.put (3, 5, st);
        juce::MemoryBlock mb; a.p.getStateInformation (mb);
        Run b; b.p.setStateInformation (mb.getData(), (int) mb.getSize());
        check (juce::roundToInt (b.p.apvts.getRawParameterValue ("op1_ol")->load()) == 77 && juce::roundToInt (b.p.apvts.getRawParameterValue ("alg")->load()) == 4,
               "etat du projet : voix restauree");
        check (b.p.seqData.get (3, 5).count == 2 && b.p.seqData.get (3, 5).notes[1] == 67, "etat du projet : sequences restaurees");
        check (b.p.getCurrentProgram() == 8 && b.p.getVoiceName() == "FM BRASS", "etat du projet : programme et nom (" + b.p.getVoiceName() + ")");
    }
    {
        Run a;
        std::vector<fm9::Voice> bank;
        for (int i = 0; i < 32; ++i) bank.push_back (a.p.bankVoice (i));
        auto bytes = fm9::bankToSysex (bank);
        check (bytes.size() == 4104, "export banque SysEx DX7 : 4104 octets");
        auto back = fm9::parseSysex (bytes.data(), bytes.size());
        bool same = back.size() == 32;
        for (int i = 0; same && i < 32; ++i) same = std::memcmp (back[(size_t) i].data(), bank[(size_t) i].data(), 155) == 0;
        check (same, "relecture SysEx identique (32 voix)");
        auto tmp = juce::File::createTempFile (".syx"); tmp.replaceWithData (bytes.data(), bytes.size());
        check (a.p.importSysexFile (tmp, 2) && a.p.getCurrentProgram() == 64 && a.p.getVoiceName() == "FM PIANO", "import d'une banque dans C");
        a.p.resetBankToFactory (2); tmp.deleteFile();
    }
    {
        Run r; r.set ("mono", 1); r.set ("glide", 40); r.p.setCurrentProgram (24);
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 300);
        const float pk = r.play (50, m);
        check (pk > 0.02f && pk < 1.0f, "mono + portamento (crete " + juce::String (pk, 3) + ")");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--fwcheck"))
    {
        // le firmware demarre-t-il dans l'hote de FM-9 ? (catalogue tools/gen_fw_catalog.py)
        juce::MemoryBlock mb; juce::String err;
        if (! juce::File (args[args.indexOf ("--fwcheck") + 1]).loadFileAsData (mb)) { std::printf ("fwcheck fail: unreadable\n"); return 1; }
        auto fw = fm9::FirmwareHost::create (mb.getData(), mb.getSize(), juce::File(), err);
        if (fw == nullptr) { std::printf ("fwcheck fail: %s\n", err.toRawUTF8()); return 1; }
        std::vector<float> l (512), r (512);
        float peak = 0.0f;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 600; ++i)
        {
            const bool midiOnly = std::getenv ("FWCHECK_MIDIONLY") != nullptr;
            const int ch = std::getenv ("FWCHECK_CH") ? std::atoi (std::getenv ("FWCHECK_CH")) : 1;
            if (! midiOnly && (i == 20 || i == 300)) fw->key (12, true);
            if (! midiOnly && (i == 60 || i == 360)) fw->key (12, false);
            if (i == 400) fw->handleMidi (juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 100));
            if (i == 480) fw->handleMidi (juce::MidiMessage::noteOff (ch, 60));
            fw->render (l.data(), r.data(), 512, 48000.0);
            for (int k = 0; k < 512; ++k) peak = std::max (peak, std::abs (l[(size_t) k]));
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("fwcheck ok %s screen=%u peak=%.3f speed=%.1fx\n", fw->abi() == fm9::FirmwareHost::Abi::sim ? "sim" : "web", (unsigned) fw->fbSerial.load(), peak, (600.0 * 512.0 / 48.0) / ms);
        return 0;
    }
    if (args.contains ("--fwshot"))
    {
        // ecran d'un firmware (.wasm ou .fwsc) apres demarrage, en PNG
        juce::MemoryBlock mb; juce::File (args[args.indexOf ("--fwshot") + 1]).loadFileAsData (mb);
        auto img = fm9::FirmwareHost::renderPreview (mb.getData(), mb.getSize(), 3.0, {});
        juce::File out (args[args.indexOf ("--fwshot") + 2]); out.deleteFile();
        if (img.isValid()) { juce::FileOutputStream os (out); juce::PNGImageFormat().writeImageToStream (img, os); }
        std::printf ("fwshot %s\n", img.isValid() ? "ok" : "fail"); std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--level"))
    {
        // niveau de sortie : chaque preset d'usine, une note puis un accord de 3 notes, crete et RMS en dBFS
        FM9Processor p; p.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (2, 512);
        const int nprog = juce::jmin (p.getNumPrograms(), args[args.indexOf ("--level") + 1].getIntValue() > 0 ? args[args.indexOf ("--level") + 1].getIntValue() : 32);
        double sumPk = 0; int quiet = 0;
        for (int prg = 0; prg < nprog; ++prg)
        {
            p.setCurrentProgram (prg);
            for (int chord = 0; chord < 2; ++chord)
            {
                float pk = 0; double ss = 0; long n = 0;
                for (int i = 0; i < 120; ++i)
                {
                    juce::MidiBuffer m;
                    if (i == 2) { m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); if (chord) { m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0); m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0); } }
                    if (i == 80) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                    p.processBlock (b, m);
                    if (i >= 2 && i < 80) for (int k = 0; k < 512; ++k) { const float v = b.getSample (0, k); pk = juce::jmax (pk, std::abs (v)); ss += v * v; ++n; }
                }
                for (int i = 0; i < 60; ++i) { juce::MidiBuffer m; p.processBlock (b, m); }
                const double pkDb = juce::Decibels::gainToDecibels (pk, -120.0f), rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (ss / juce::jmax (1L, n)), -120.0f);
                if (! chord) { sumPk += pkDb; if (pkDb < -18) ++quiet; }
                std::printf ("%-4s %-14s %s peak %6.1f dB  rms %6.1f dB\n", FM9Processor::programLabel (prg).toRawUTF8(), p.getProgramName (prg).toRawUTF8(), chord ? "chord" : "note ", pkDb, rmsDb);
            }
        }
        std::printf ("moyenne crete (note) %.1f dB, %d presets sous -18 dB\n", sumPk / nprog, quiet);
        std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--cachelist"))
    {
        // copie locale d'une liste ajoutee (test hors ligne)
        const auto url = args[args.indexOf ("--cachelist") + 1];
        auto f = fm9::FirmwareCatalog::userListCache (url); f.getParentDirectory().createDirectory();
        juce::File (args[args.indexOf ("--cachelist") + 2]).copyFileTo (f);
        juce::String d; const auto all = fm9::FirmwareCatalog::load (d);
        std::printf ("catalogue : %d firmwares\n", (int) all.size()); std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--capture"))
    {
        FM9Editor::captureScreenFor (args[args.indexOf ("--capture") + 1]);
        std::printf ("capture %s\n", fm9::FirmwareCatalogView::previewFile ([&] { fm9::CatalogEntry e; e.name = args[args.indexOf ("--capture") + 1]; return e; }()).existsAsFile() ? "ok" : "absente");
        std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--linkfind"))
    {
        juce::String o, r, d; fm9::FirmwareInstaller::parseRepo (args[args.indexOf ("--linkfind") + 1], o, r);
        for (auto& e : fm9::FirmwareCatalog::load (d)) if (e.repo.equalsIgnoreCase (o + "/" + r)) std::printf ("%s/%s -> fiche %s (%s)\n", o.toRawUTF8(), r.toRawUTF8(), e.name.toRawUTF8(), e.pkg.toRawUTF8());
        std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--enctest"))
    {
        // MIDI Learn d'un potard : la commande (ctl:knob1) puis le parametre affiche sous KNOB2 font tourner l'image du potard
        FM9Processor p; p.setRateAndBufferSizeDetails (48000.0, 512); p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* fe = dynamic_cast<FM9Editor*> (ed.get());
        const auto slot = fe->slotIdForTest (1);
        p.applyMidiMapString ("20=ctl:knob1;21=" + slot + ";22=ctl:algorithm");
        const float a3 = fe->encoderAngleForTest (3), a4 = fe->encoderAngleForTest (4), a2 = fe->encoderAngleForTest (2);
        juce::AudioBuffer<float> b (2, 512);
        for (int v = 10; v <= 40; v += 3)
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::controllerEvent (1, 20, v), 0);
            m.addEvent (juce::MidiMessage::controllerEvent (1, 21, v * 2), 0);
            m.addEvent (juce::MidiMessage::controllerEvent (1, 22, v), 0);
            p.processBlock (b, m);
            p.tickForTest(); fe->tickForTest();
        }
        for (int i = 0; i < 20; ++i) { juce::MidiBuffer m; p.processBlock (b, m); p.tickForTest(); fe->tickForTest(); }
        if (const char* dir = std::getenv ("ENCGIF"))
            for (int v = 0, f = 0; v < 120; v += 2, ++f)
            {
                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::controllerEvent (1, 20, 40 + (v < 60 ? v : 120 - v)), 0);
                m.addEvent (juce::MidiMessage::controllerEvent (1, 21, 10 + (v < 60 ? v : 120 - v) * 2), 0);
                p.processBlock (b, m); p.tickForTest(); fe->tickForTest();
                fe->prepareSnapshot (1.0f, 0, 0, 0);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::File out (juce::String (dir) + "/e" + juce::String (f).paddedLeft ('0', 4) + ".png");
                juce::FileOutputStream os (out); os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os);
            }
        std::printf ("enctest knob1 %.2f rad, knob2 (%s) %.2f rad, algorithm %.2f rad\n", fe->encoderAngleForTest (3) - a3, slot.toRawUTF8(), fe->encoderAngleForTest (4) - a4, fe->encoderAngleForTest (2) - a2);
        return 0;
    }
    if (args.contains ("--calibrate"))
    {
        // calibrage d'un paquet materiel : horloge mini sans retard et canaux MIDI (outil du catalogue)
        juce::MemoryBlock mb; juce::File (args[args.indexOf ("--calibrate") + 1]).loadFileAsData (mb);
        int mhz = -1; uint32_t ch = 0;
        if (fm9::FirmwareHost::isHardwarePackage (mb.getData(), mb.getSize()) && fm9::FirmwareHost::calibrate (mb.getData(), mb.getSize(), mhz, ch))
            std::printf ("calib %d %u\n", mhz, (unsigned) ch);
        else std::printf ("calib fail\n");
        return 0;
    }
    if (args.contains ("--autotest"))
    {
        // horloge Auto + canal MIDI Auto : 1er chargement (calibrage en arriere-plan), puis rechargement
        FM9Processor p; p.setRateAndBufferSizeDetails (48000.0, 512); p.prepareToPlay (48000.0, 512);
        juce::String err;
        const auto dest = fm9::FirmwareInstaller::installPackage (juce::File (args[args.indexOf ("--autotest") + 1]), err);
        const auto name = dest.getFileNameWithoutExtension() + fm9::FirmwareInstaller::hardwareSuffix;
        const auto stem = dest.getFileNameWithoutExtension();
        fm9::FirmwareHost::storeProfile (stem, {});            // profil vide : comme un firmware jamais vu
        fm9::FirmwareHost::hwMhz().store (0);
        if (! p.activateFirmware (name, err)) { std::printf ("autotest fail %s\n", err.toRawUTF8()); return 1; }
        std::printf ("1er chargement : %d MHz, calibrage %s\n", p.firmware()->liveMhz.load(), fm9::FirmwareHost::isCalibrating (stem) ? "en cours" : "non");
        juce::AudioBuffer<float> b (2, 512);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        while (fm9::FirmwareHost::isCalibrating (stem) && juce::Time::getMillisecondCounterHiRes() - t0 < 180000.0)
        {
            juce::MidiBuffer m; p.processBlock (b, m); juce::Thread::sleep (10);
        }
        p.tickForTest();
        const auto prof = fm9::FirmwareHost::profileOf (stem);
        std::printf ("calibre en %.0f s : mini %d MHz, canaux %x -> Auto %d MHz ; canaux du processeur %x\n",
                     (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, prof.minMhz, (unsigned) prof.channels,
                     fm9::FirmwareHost::recommendedMhz (prof.minMhz), (unsigned) p.fwChannels.load());
        p.activateFirmware (name, err);
        std::printf ("2e chargement : %d MHz\n", p.firmware()->liveMhz.load());
        // clavier sur le canal 10 : Auto l'envoie sur le 1er canal ecoute
        p.fwOutCh.store (FM9Processor::fwChAuto);
        float before = 0, after = 0;
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 420; ++i)
        {
            juce::MidiBuffer e;
            if (i == 300) e.addEvent (juce::MidiMessage::noteOn (10, 60, (juce::uint8) 110), 0);
            if (i == 400) e.addEvent (juce::MidiMessage::noteOff (10, 60), 0);
            p.processBlock (b, e);
            (i < 300 ? before : after) = juce::jmax (i < 300 ? before : after, i > 150 ? b.getMagnitude (0, 0, 512) : 0.0f);
            const double due = t1 + (i + 1) * 512.0 / 48.0, now = juce::Time::getMillisecondCounterHiRes();
            if (due > now) juce::Thread::sleep ((int) (due - now));
            if (i % 94 == 0) p.tickForTest();
        }
        std::printf ("canal 10 en Auto : silence %.3f, note %.3f ; surveillance : %.2fx temps reel, trous %d, retard %d\n", before, after,
                     p.firmware()->liveSpeed.load(), p.firmware()->liveStarved.load(), p.firmware()->liveLate.load());
        return 0;
    }
    if (args.contains ("--hubkeys"))
    {
        for (auto l : { "https://thefirmwarehub.com/firmware/argo/", "https://play.thefirmwarehub.com/?fw=zp12n", "https://thefirmwarehub.com/fw/argo-1.8.3.fwsc", "Argo", "https://thefirmwarehub.com/" })
            std::printf ("%s -> [%s]\n", l, fm9::FirmwareHub::keyOf (l).toRawUTF8());
        std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--webtrial"))
    {
        juce::MemoryBlock mb; juce::File (args[args.indexOf ("--webtrial") + 1]).loadFileAsData (mb);
        juce::String err; const double t0 = juce::Time::getMillisecondCounterHiRes();
        const bool ok = fm9::FirmwareCatalog::webBuildStarts (mb, err, nullptr, 3000);
        std::printf ("webtrial %s '%s' %.1f s\n", ok ? "ok" : "fail", err.toRawUTF8(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000);
        std::fflush (stdout); std::_Exit (0);
    }
    if (args.contains ("--catinstall"))
    {
        juce::String date; const auto name = args[args.indexOf ("--catinstall") + 1];
        for (auto& e : fm9::FirmwareCatalog::load (date))
            if (e.name == name)
            {
                juce::String err; const double t0 = juce::Time::getMillisecondCounterHiRes(); float last = -1;
                const bool ok = fm9::FirmwareCatalog::install (e, err, [&] (float f) { if (f - last > 0.1f || f >= 1.0f) { last = f; std::printf ("  %.0f %% (%.1f s)\n", f * 100, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000); std::fflush (stdout); } });
                std::printf ("catinstall %s : %s %s (%.1f s)\n", name.toRawUTF8(), ok ? "ok" : "fail", err.toRawUTF8(), (juce::Time::getMillisecondCounterHiRes() - t0) / 1000);
                return 0;
            }
        std::printf ("catinstall: not found\n"); return 1;
    }
    if (args.contains ("--hwrt"))
    {
        // firmware materiel en temps reel, comme dans un DAW : blocs de 512 a 48 kHz au rythme reel, notes MIDI, sortie en wav
        juce::MemoryBlock mb; juce::String err;
        if (args.contains ("--mhz")) fm9::FirmwareHost::hwMhz().store (args[args.indexOf ("--mhz") + 1].getIntValue());
        juce::File (args[args.indexOf ("--hwrt") + 1]).loadFileAsData (mb);
        auto fw = fm9::FirmwareHost::createHardware (mb.getData(), mb.getSize(), juce::File(), err);
        if (fw == nullptr) { std::printf ("hwrt fail %s\n", err.toRawUTF8()); return 1; }
        const int bs = 512; const double sr = 48000.0;
        if (args.contains ("--margin")) fm9::FirmwareHost::hwMargin().store (args[args.indexOf ("--margin") + 1].getIntValue());
        fw->prepare (sr, bs);
        juce::AudioBuffer<float> out (2, (int) (sr * 12));
        std::vector<float> l (bs), r (bs);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        int pos = 0, blk = 0;
        while (pos + bs <= out.getNumSamples())
        {
            // comme Reaper (calcul anticipe) : toutes les ~0,5 s une pause de 90 ms, puis une rafale pour rattraper
            const bool bursty = args.contains ("--bursty");
            double due = t0 + blk * bs / sr * 1000.0;
            if (bursty) { const int ph = blk % 47; due = t0 + (blk - ph) * bs / sr * 1000.0 + (ph < 9 ? 90.0 + ph * 1.0 : ph * bs / sr * 1000.0); }
            while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep (1);
            const double sec = pos / sr;
            if (blk % 47 == 0 && sec > 3.0) fw->handleMidi (juce::MidiMessage::noteOn (1, 60 + (blk / 47) % 7, (juce::uint8) 100));
            if (blk % 47 == 30 && sec > 3.0) fw->handleMidi (juce::MidiMessage::noteOff (1, 60 + (blk / 47) % 7));
            fw->render (l.data(), r.data(), bs, sr);
            out.copyFrom (0, pos, l.data(), bs); out.copyFrom (1, pos, r.data(), bs);
            pos += bs; ++blk;
        }
        juce::WavAudioFormat wav;
        juce::File f (args[args.indexOf ("--hwrt") + 2]); f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 16, {}, 0));
        w->writeFromAudioSampleBuffer (out, 0, pos);
        std::printf ("hwrt ok %s\n", fw->status().toRawUTF8());
        return 0;
    }
    if (args.isEmpty() || args.contains ("--test")) { audioTests(); keyStepTest();
        midiTest(); deleteTest(); golanaTest(); firmwareTest(); }
    using juce::StringArray; using juce::String;
    if (args.contains ("--shot"))
    {
        auto arg = [&] (const char* k, float d) { const int i = args.indexOf (k); return i >= 0 ? args[i + 1].getFloatValue() : d; };
        FM9Processor p; p.prepareToPlay (48000, 512);
        p.setCurrentProgram ((int) arg ("--program", 0));
        for (auto& kv : StringArray::fromTokens (args.indexOf ("--set") >= 0 ? args[args.indexOf ("--set") + 1] : String(), ",", ""))
            if (auto* q = p.apvts.getParameter (kv.upToFirstOccurrenceOf ("=", false, false)))
                q->setValueNotifyingHost (q->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
        // quelques notes jouees : touches allumees et oscilloscope
        juce::AudioBuffer<float> buf (2, 512);
        if (args.contains ("--notes"))
        {
            juce::MidiBuffer m;
            for (int n : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
            p.processBlock (buf, m);
            for (int i = 0; i < 20; ++i) { juce::MidiBuffer e; p.processBlock (buf, e); }
        }
        if (args.contains ("--seqdemo"))
        {
            const int notes[16] = { 48, 0, 55, 60, 48, 0, 58, 0, 46, 0, 53, 58, 46, 53, 0, 0 };
            for (int i = 0; i < 16; ++i) if (notes[i] > 0) { fm9::SeqStep st; st.count = 1; st.notes[0] = notes[i]; st.vel = 100; p.seqData.put (0, i, st); }
        }
        if (args.contains ("--hwmidi"))
        {
            // canal MIDI envoye au firmware : note sur le canal HWMIDI_CH, option fwOutCh = HWMIDI_OUT
            juce::String err;
            const auto dest = fm9::FirmwareInstaller::installPackage (juce::File (args[args.indexOf ("--hwmidi") + 1]), err);
            const auto name = dest.getFileNameWithoutExtension() + fm9::FirmwareInstaller::hardwareSuffix;
            if (dest == juce::File() || ! p.activateFirmware (name, err)) { std::printf ("hwmidi fail %s\n", err.toRawUTF8()); return 1; }
            const int ch = std::getenv ("HWMIDI_CH") ? std::atoi (std::getenv ("HWMIDI_CH")) : 1;
            p.fwOutCh.store (std::getenv ("HWMIDI_OUT") ? std::atoi (std::getenv ("HWMIDI_OUT")) : 0);
            if (std::getenv ("HWMIDI_MASTER")) p.fwMasterOverride.store ((float) std::atof (std::getenv ("HWMIDI_MASTER")));
            juce::AudioBuffer<float> b2 (2, 512); float before = 0, after = 0;
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 420; ++i)
            {
                juce::MidiBuffer e;
                if (i == 300) e.addEvent (juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 110), 0);
                if (i == 400) e.addEvent (juce::MidiMessage::noteOff (ch, 60), 0);
                p.processBlock (b2, e);
                (i < 300 ? before : after) = juce::jmax (i < 300 ? before : after, i > 150 ? b2.getMagnitude (0, 0, 512) : 0.0f);
                const double due = t0 + (i + 1) * 512.0 / 48.0, now = juce::Time::getMillisecondCounterHiRes();
                if (due > now) juce::Thread::sleep ((int) (due - now));
            }
            std::printf ("hwmidi ch=%d out=%d silence=%.3f note=%.3f\n", ch, p.fwOutCh.load(), before, after);
            return 0;
        }
        if (args.contains ("--hwfw"))
        {
            // paquet materiel .fwsc : installe, lance, joue en temps reel (le thread d'emulation avance seul)
            juce::String err;
            const auto dest = fm9::FirmwareInstaller::installPackage (juce::File (args[args.indexOf ("--hwfw") + 1]), err);
            const auto name = dest.getFileNameWithoutExtension() + fm9::FirmwareInstaller::hardwareSuffix;
            const bool ok = dest != juce::File() && p.activateFirmware (name, err);
            std::printf ("hwfw %s : %s (latence %d)\n", name.toRawUTF8(), ok ? "ok" : err.toRawUTF8(), p.getLatencySamples());
            if (ok && args.contains ("--notes"))
            {
                juce::MidiBuffer m; juce::AudioBuffer<float> b2 (2, 512);
                for (int n : { 60, 64, 67 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
                p.processBlock (b2, m);
                std::printf ("keyBits %08x %08x\n", (unsigned) p.keyBits[1].load(), (unsigned) p.keyBits[2].load());
            }
            if (auto* fw = p.firmware())
            {
                juce::AudioBuffer<float> b2 (2, 512); float peak = 0;
                std::unique_ptr<juce::AudioProcessorEditor> ged;
                const char* gifDir = std::getenv ("LEDGIF");
                if (gifDir != nullptr)
                {
                    // fil audio separe (comme un DAW) ; le fil principal capture l'editeur toutes les 66 ms
                    ged.reset (p.createEditor());
                    std::atomic<bool> stopAudio { false };
                    std::thread audio ([&]
                    {
                        juce::AudioBuffer<float> ab (2, 512);
                        const double ta = juce::Time::getMillisecondCounterHiRes();
                        for (int i = 0; ! stopAudio; ++i)
                        {
                            juce::MidiBuffer e;
                            if (i == 150) fw->button (10, true);
                            if (i == 155) fw->button (10, false);
                            p.processBlock (ab, e);
                            const double due = ta + (i + 1) * 512.0 / 48.0, now = juce::Time::getMillisecondCounterHiRes();
                            if (due > now) juce::Thread::sleep ((int) (due - now));
                        }
                    });
                    juce::Thread::sleep (2200);
                    auto* fe = dynamic_cast<FM9Editor*> (ged.get());
                    for (int k = 0; k < 75 && fe != nullptr; ++k)
                    {
                        const double tk = juce::Time::getMillisecondCounterHiRes();
                        fe->prepareSnapshot (1.0f, 0, 0, 0);
                        auto img = ged->createComponentSnapshot (ged->getLocalBounds(), true, 1.0f);
                        juce::File f (juce::String (gifDir) + "/f" + juce::String (k).paddedLeft ('0', 4) + ".png");
                        juce::FileOutputStream os (f); os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os);
                        const double w = 66.0 - (juce::Time::getMillisecondCounterHiRes() - tk);
                        if (w > 0) juce::Thread::sleep ((int) w);
                    }
                    stopAudio = true; audio.join();
                    ged.reset();
                    return 0;
                }
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 470; ++i)                     // ~5 s de musique a 48 kHz
                {
                    juce::MidiBuffer e;
                    if (i == 150) fw->button (10, true);
                    if (i == 200) fw->button (10, false);
                    if (i >= 250 && i % 20 == 0) fw->key ((i / 20) % 8, true);            // frappe des touches (pads)
                    if (i >= 250 && i % 20 == 10) fw->key ((i / 20) % 8, false);
                    if (i == 420) e.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);   // et une note MIDI
                    if (i == 440) e.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                    p.processBlock (b2, e);
                    if (i > 100) peak = juce::jmax (peak, b2.getMagnitude (0, 0, 512));
                    if (std::getenv ("LEDTRACE") && i % 5 == 0) std::printf ("t=%4d keys=%08x dim=%08x btn=%04x\n", i, (unsigned) fw->litKeys.load(), (unsigned) fw->dimKeys.load(), (unsigned) fw->litButtons.load());
                    const double due = t0 + (i + 1) * 512.0 / 48.0;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (due > now) juce::Thread::sleep ((int) (due - now));
                }
                std::printf ("hwfw son crete %.3f, ecran %u images, LED touches %x boutons %x, etat '%s'\n", peak, (unsigned) fw->fbSerial.load(),
                             (unsigned) fw->litKeys.load(), (unsigned) fw->litButtons.load(), fw->status().toRawUTF8());
            }
        }
        if (args.contains ("--ledtrace"))
        {
            // LED des touches d'un firmware installe pendant quelques secondes (bouton PLAY = argument 2)
            juce::String err; const bool ok = p.activateFirmware (args[args.indexOf ("--ledtrace") + 1], err);
            std::printf ("ledtrace %s\n", ok ? "ok" : err.toRawUTF8());
            const int play = args[args.indexOf ("--ledtrace") + 2].getIntValue();
            if (auto* fw = p.firmware())
            {
                juce::AudioBuffer<float> b2 (2, 512); float peak = 0;
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 400; ++i)
                {
                    juce::MidiBuffer e;
                    if (i == 100) fw->button (play, true);
                    if (i == 105) fw->button (play, false);
                    p.processBlock (b2, e);
                    if (i > 100) peak = juce::jmax (peak, b2.getMagnitude (0, 0, 512));
                    if (i % 6 == 0) std::printf ("t=%4d keys=%08x dim=%08x btn=%04x\n", i, (unsigned) fw->litKeys.load(), (unsigned) fw->dimKeys.load(), (unsigned) fw->litButtons.load());
                    const double due = t0 + (i + 1) * 512.0 / 48.0, now = juce::Time::getMillisecondCounterHiRes();
                    if (due > now) juce::Thread::sleep ((int) (due - now));
                }
                std::printf ("peak %.3f\n", peak);
            }
            return 0;
        }
        if (args.contains ("--firmware"))
        {
            juce::String err; p.activateFirmware (args[args.indexOf ("--firmware") + 1], err);
            if (auto* fw = p.firmware())
            {
                juce::AudioBuffer<float> b2 (2, 512); juce::MidiBuffer e;
                for (int i = 0; i < 100; ++i) p.processBlock (b2, e);
                fw->button (10, true); p.processBlock (b2, e); fw->button (10, false);
                for (int i = 0; i < 120; ++i) p.processBlock (b2, e);
            }
        }
        if (args.contains ("--emumhz")) fm9::FirmwareHost::hwMhz().store (args[args.indexOf ("--emumhz") + 1].getIntValue());
        if (args.contains ("--emubuf")) { fm9::FirmwareHost::hwAutoMargin().store (false); fm9::FirmwareHost::hwMargin().store (args[args.indexOf ("--emubuf") + 1].getIntValue()); }
        if (args.contains ("--nks")) { std::printf ("nks %d\n", p.exportNksLibrary (juce::File (args[args.indexOf ("--nks") + 1]))); }
        if (args.contains ("--glow")) p.uiState.setProperty ("glow", args[args.indexOf ("--glow") + 1].getIntValue(), nullptr);
        if (args.contains ("--skin")) p.uiState.setProperty ("skin", args[args.indexOf ("--skin") + 1], nullptr);
        if (args.contains ("--startup")) FM9Editor::setStartupSkin (args[args.indexOf ("--startup") + 1]);   // skin des nouvelles instances
        if (auto* fw0 = p.firmware()) std::printf ("avant capture keys=%08x dim=%08x\n", (unsigned) fw0->litKeys.load(), (unsigned) fw0->dimKeys.load());
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        const float scale = arg ("--scale", 1.0f);
        if (auto* fe = dynamic_cast<FM9Editor*> (ed.get())) fe->prepareSnapshot (scale, (int) arg ("--mode", 0), (int) arg ("--page", 0), (int) arg ("--popup", 0));
        if (auto* fe = dynamic_cast<FM9Editor*> (ed.get()))
        {
            if (args.contains ("--colours")) fe->setSnapshotColours (args[args.indexOf ("--colours") + 1]);
            if (args.contains ("--saveskin")) std::printf ("save %d\n", (int) fe->saveUserSkin (args[args.indexOf ("--saveskin") + 1]));
            if (args.contains ("--loadskin")) std::printf ("load %d\n", (int) fe->loadUserSkin (FM9Editor::skinFolder().getChildFile (args[args.indexOf ("--loadskin") + 1] + ".fm9skin")));
            if (args.contains ("--catalog")) fe->openCatalog();
            if ((args.contains ("--catsearch") || args.contains ("--catcompat") || args.contains ("--catinst") || args.contains ("--catmode")) && fe->catalog != nullptr)
                fe->catalog->setFilterForTest (args.contains ("--catsearch") ? args[args.indexOf ("--catsearch") + 1] : String(), args.contains ("--catmode") ? args[args.indexOf ("--catmode") + 1].getIntValue() : args.contains ("--catinst") ? 2 : args.contains ("--catcompat") ? 1 : 0);
            if (args.contains ("--delskin")) fe->deleteUserSkin (FM9Editor::skinFolder().getChildFile (args[args.indexOf ("--delskin") + 1] + ".fm9skin"));
            if (args.contains ("--confirmdel")) fe->deleteFirmware (args[args.indexOf ("--confirmdel") + 1]);
            if (args.contains ("--loading") && fe->catalog != nullptr)
            {
                const int k = args.indexOf ("--loading");
                fe->catalog->showLoadingForTest (args[k + 1], args[k + 2].getIntValue(), args[k + 3].getFloatValue(), { 120, 250 }, args[k + 4], args[k + 5].getFloatValue());
            }
            if (args.contains ("--preview") && fe->catalog != nullptr)
            {
                auto xy = StringArray::fromTokens (args[args.indexOf ("--preview") + 2], ",", "");
                fe->catalog->showPreviewNow (args[args.indexOf ("--preview") + 1], { xy[0].getIntValue(), xy[1].getIntValue() });
            }
            if (args.contains ("--panel"))
            {
                auto xy = StringArray::fromTokens (args[args.indexOf ("--panel") + 1], ",", "");
                fe->openColourPanel ({ xy[0].getIntValue(), xy[1].getIntValue() }, xy.size() > 2 ? xy[2].getIntValue() : 0);
            }
        }
        if (auto* fw = p.firmware()) { juce::ignoreUnused (fw); }
        if (args.contains ("--addmenu"))
            if (auto* fe = dynamic_cast<FM9Editor*> (ed.get()); fe != nullptr && fe->catalog != nullptr)
            {
                // catalogue agrandi + menu Add... ouvert au-dessus, a la meme echelle
                fe->catalog->setBounds (0, 0, fm9::FirmwareCatalogView::bigW, fm9::FirmwareCatalogView::bigH);
                fe->catalog->setFilterForTest ({}, 0);
                auto ci = fe->catalog->createComponentSnapshot (fe->catalog->getLocalBounds(), true, 1.0f);
                auto menu = fe->catalog->buildAddMenu();
                auto mi = fe->renderMenuPreview (menu, "Add a firmware list (link to its JSON)...", 1.0f, nullptr, &fe->catalog->menuLnf);
                juce::Image out (juce::Image::ARGB, 900, 300, true);
                juce::Graphics g (out);
                g.drawImageAt (ci, 0, 0);
                g.drawImageAt (mi, 420, 70);
                juce::File o (args[args.indexOf ("--addmenu") + 1]); o.deleteFile(); juce::FileOutputStream os (o); juce::PNGImageFormat().writeImageToStream (out, os);
                std::printf ("addmenu ok %.3f\n", fe->catalog->menuLnf.textScale);
            }
        if (args.contains ("--catbig"))
            if (auto* fe = dynamic_cast<FM9Editor*> (ed.get()); fe != nullptr && fe->catalog != nullptr)
            {
                // fenetre agrandie du catalogue, rendue seule a sa taille
                fe->catalog->setBounds (0, 0, fm9::FirmwareCatalogView::bigW, fm9::FirmwareCatalogView::bigH);
                fe->catalog->setFilterForTest (args.contains ("--catsearch") ? args[args.indexOf ("--catsearch") + 1] : String(), args.contains ("--catmode") ? args[args.indexOf ("--catmode") + 1].getIntValue() : 0);
                auto ci = fe->catalog->createComponentSnapshot (fe->catalog->getLocalBounds(), true, scale);
                juce::File (args[args.indexOf ("--catbig") + 1]).deleteFile();
                juce::FileOutputStream os (juce::File (args[args.indexOf ("--catbig") + 1])); juce::PNGImageFormat().writeImageToStream (ci, os);
            }
        if (args.contains ("--emumenu"))
            if (auto* fe = dynamic_cast<FM9Editor*> (ed.get()))
            {
                // sous-menus Chip clock et Emulation buffer, cote a cote
                std::function<const juce::PopupMenu* (const juce::PopupMenu&, const juce::String&)> find = [&] (const juce::PopupMenu& m, const juce::String& t) -> const juce::PopupMenu*
                {
                    for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
                    {
                        auto& item = it.getItem();
                        if (item.subMenu == nullptr) continue;
                        if (item.text.startsWith (t)) return item.subMenu.get();
                        if (auto* r = find (*item.subMenu, t)) return r;
                    }
                    return nullptr;
                };
                const auto mainMenu = fe->buildContextMenu ({}, -1);
                if (args.contains ("--pump"))
                    if (p.firmware() != nullptr)
                    {
                        // quelques secondes de jeu en temps reel, pour la surveillance de l'emulation
                        juce::AudioBuffer<float> b2 (2, 512);
                        const double t1 = juce::Time::getMillisecondCounterHiRes();
                        for (int i = 0; i < (int) (args[args.indexOf ("--pump") + 1].getDoubleValue() * 93.75); ++i)
                        {
                            juce::MidiBuffer e;
                            if (i % 40 == 0) e.addEvent (juce::MidiMessage::noteOn (1, 60 + (i / 40) % 5, (juce::uint8) 100), 0);
                            if (i % 40 == 30) e.addEvent (juce::MidiMessage::noteOff (1, 60 + (i / 40) % 5), 0);
                            p.processBlock (b2, e);
                            const double due = t1 + (i + 1) * 512.0 / 48.0, now = juce::Time::getMillisecondCounterHiRes();
                            if (due > now) juce::Thread::sleep ((int) (due - now));
                        }
                        p.tickForTest();
                    }
                const auto mainMenu2 = fe->buildContextMenu ({}, -1);
                if (auto* fm = find (mainMenu2, "Firmware"))
                {
                    auto* c2 = find (*fm, "Chip clock"); auto* mc = find (*fm, "MIDI channel sent");
                    auto fi = fe->renderMenuPreview (*fm, "Chip clock", scale), ci2 = fe->renderMenuPreview (*c2, {}, scale), mi2 = fe->renderMenuPreview (*mc, {}, scale);
                    const int pad = juce::roundToInt (20 * scale);
                    juce::Image canvas (juce::Image::ARGB, fi.getWidth() + ci2.getWidth() + mi2.getWidth() + pad * 4, juce::jmax (fi.getHeight(), juce::jmax (ci2.getHeight(), mi2.getHeight())) + pad * 2, true);
                    juce::Graphics g (canvas); g.fillAll (juce::Colour (0xff2a2b2e));
                    g.drawImageAt (fi, pad, pad); g.drawImageAt (ci2, pad * 2 + fi.getWidth(), pad); g.drawImageAt (mi2, pad * 3 + fi.getWidth() + ci2.getWidth(), pad);
                    juce::File o (args[args.indexOf ("--shot") + 1]); o.deleteFile(); juce::FileOutputStream os (o); juce::PNGImageFormat().writeImageToStream (canvas, os);
                    std::printf ("emumenu ok\n");
                    return 0;
                }
                auto* c = find (mainMenu, "Chip clock"); auto* bb = find (mainMenu, "Emulation buffer");
                if (c != nullptr && bb != nullptr)
                {
                    auto ci = fe->renderMenuPreview (*c, args[args.indexOf ("--emumenu") + 1], scale), bi = fe->renderMenuPreview (*bb, args[args.indexOf ("--emumenu") + 2], scale);
                    const int pad = juce::roundToInt (20 * scale);
                    juce::Image canvas (juce::Image::ARGB, ci.getWidth() + bi.getWidth() + pad * 3, juce::jmax (ci.getHeight(), bi.getHeight()) + pad * 2, true);
                    juce::Graphics g (canvas); g.fillAll (juce::Colour (0xff2a2b2e));
                    g.drawImageAt (ci, pad, pad); g.drawImageAt (bi, pad * 2 + ci.getWidth(), pad);
                    juce::File o (args[args.indexOf ("--shot") + 1]); o.deleteFile(); juce::FileOutputStream os (o); juce::PNGImageFormat().writeImageToStream (canvas, os);
                    std::printf ("emumenu ok\n");
                }
                else std::printf ("emumenu: sous-menus introuvables\n");
                return 0;
            }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        if (args.contains ("--skinmenu"))
            if (auto* fe = dynamic_cast<FM9Editor*> (ed.get()))
            {
                // menu principal ouvert au clic, sous-menu Skin deplie a cote de son entree
                auto xy = StringArray::fromTokens (args[args.indexOf ("--skinmenu") + 1], ",", "");
                const int x = xy[0].getIntValue(), y = xy[1].getIntValue();
                int hy = 0;
                if (args.contains ("--ctxmenu"))
                {
                    const auto a = juce::StringArray::fromTokens (args[args.indexOf ("--ctxmenu") + 1], ",", "");
                    auto img = fe->renderMenuPreview (fe->buildContextMenu (a[0] == "-" ? juce::String() : a[0], a[1].getIntValue()), {}, scale);
                    juce::PNGImageFormat png; juce::FileOutputStream os (juce::File (args[args.indexOf ("--shot") + 1]).getSiblingFile ("ctxmenu.png"));
                    os.setPosition (0); os.truncate(); png.writeImageToStream (img, os);
                }
                auto main = fe->renderMenuPreview (fe->buildMainMenuForPreview(), "Skin", scale, &hy);
                auto sub = fe->renderMenuPreview (fe->buildSkinMenu(), args.contains ("--hl") ? args[args.indexOf ("--hl") + 1] : String(), scale);
                const int sy = juce::roundToInt ((y + hy - 7) * scale);
                juce::Image canvas (juce::Image::ARGB, img.getWidth(), juce::jmax (img.getHeight(), sy + sub.getHeight() + juce::roundToInt (12 * scale)), true);
                juce::Graphics g (canvas);
                g.fillAll (juce::Colour (0xff2a2b2e));
                g.drawImageAt (img, 0, 0);
                g.drawImageAt (main, juce::roundToInt (x * scale), juce::roundToInt (y * scale));
                g.drawImageAt (sub, juce::roundToInt (x * scale) + main.getWidth() - juce::roundToInt (6 * scale), sy);
                img = canvas;
            }
        juce::File out (args[args.indexOf ("--shot") + 1]);
        out.deleteFile(); juce::FileOutputStream os (out); juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("capture %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
    std::printf ("%d echec(s)\n", fails);
    return fails == 0 ? 0 : 1;
}
