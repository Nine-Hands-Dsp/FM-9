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
    p.kbShift.store (12);
    block (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    check (on (72) && ! on (60), "OCT+ : la note 60 du clavier maitre joue 72");
    p.kbShift.store (0);
    block (juce::MidiMessage::noteOff (1, 60));
    check (! on (72), "relachement correct meme si l'octave a change entre-temps");
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
    p.applyMidiMapString ("30=ctl:octup");
    block (juce::MidiMessage::controllerEvent (1, 30, 127));
    block (juce::MidiMessage::controllerEvent (1, 30, 0));
    block (juce::MidiMessage::controllerEvent (1, 30, 127));
    check (p.kbShift.load() == 24, "OCT+ en MIDI Learn (bouton) : deux appuis = +2 octaves");
    {
        FM9Editor ed (p); ed.setVisible (true);
        const float sc = (float) ed.getHeight() / fm9::layout::unitsH;
        for (auto kp : { fm9::layout::knob_knob1, fm9::layout::knob_knob4, fm9::layout::knob_select, fm9::layout::knob_presets })
        {
            auto* c = ed.getComponentAt ((kp * sc).toInt());
            std::printf ("    editeur %dx%d, point %d,%d -> %s %s\n", ed.getWidth(), ed.getHeight(), (kp * sc).toInt().x, (kp * sc).toInt().y, c == nullptr ? "null" : typeid (*c).name(), c == nullptr ? "" : c->getBounds().toString().toRawUTF8());
            check (dynamic_cast<nh::Encoder*> (c) != nullptr, "clic droit au centre du potard : c'est bien le potard qui le recoit");
        }
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
        for (int i = 0; i < 200; ++i)
        {
            if (i == 20) fw->key (12, true);
            if (i == 60) fw->key (12, false);
            fw->render (l.data(), r.data(), 512, 48000.0);
            for (int k = 0; k < 512; ++k) peak = std::max (peak, std::abs (l[(size_t) k]));
        }
        std::printf ("fwcheck ok %s screen=%u peak=%.3f\n", fw->abi() == fm9::FirmwareHost::Abi::sim ? "sim" : "web", (unsigned) fw->fbSerial.load(), peak);
        return 0;
    }
    if (args.isEmpty() || args.contains ("--test")) { audioTests(); keyStepTest();
        midiTest(); firmwareTest(); }
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
        if (args.contains ("--hwfw"))
        {
            // paquet materiel .fwsc : installe, lance, joue en temps reel (le thread d'emulation avance seul)
            juce::String err;
            const auto dest = fm9::FirmwareInstaller::installPackage (juce::File (args[args.indexOf ("--hwfw") + 1]), err);
            const auto name = dest.getFileNameWithoutExtension() + fm9::FirmwareInstaller::hardwareSuffix;
            const bool ok = dest != juce::File() && p.activateFirmware (name, err);
            std::printf ("hwfw %s : %s (latence %d)\n", name.toRawUTF8(), ok ? "ok" : err.toRawUTF8(), p.getLatencySamples());
            if (auto* fw = p.firmware())
            {
                juce::AudioBuffer<float> b2 (2, 512); float peak = 0;
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
                    const double due = t0 + (i + 1) * 512.0 / 48.0;
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (due > now) juce::Thread::sleep ((int) (due - now));
                }
                std::printf ("hwfw son crete %.3f, ecran %u images, LED touches %x boutons %x, etat '%s'\n", peak, (unsigned) fw->fbSerial.load(),
                             (unsigned) fw->litKeys.load(), (unsigned) fw->litButtons.load(), fw->status().toRawUTF8());
            }
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
        if (args.contains ("--nks")) { std::printf ("nks %d\n", p.exportNksLibrary (juce::File (args[args.indexOf ("--nks") + 1]))); }
        if (args.contains ("--glow")) p.uiState.setProperty ("glow", args[args.indexOf ("--glow") + 1].getIntValue(), nullptr);
        if (args.contains ("--skin")) p.uiState.setProperty ("skin", args[args.indexOf ("--skin") + 1], nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        const float scale = arg ("--scale", 1.0f);
        if (auto* fe = dynamic_cast<FM9Editor*> (ed.get())) fe->prepareSnapshot (scale, (int) arg ("--mode", 0), (int) arg ("--page", 0), (int) arg ("--popup", 0));
        if (auto* fe = dynamic_cast<FM9Editor*> (ed.get()))
        {
            if (args.contains ("--colours")) fe->setSnapshotColours (args[args.indexOf ("--colours") + 1]);
            if (args.contains ("--saveskin")) std::printf ("save %d\n", (int) fe->saveUserSkin (args[args.indexOf ("--saveskin") + 1]));
            if (args.contains ("--loadskin")) std::printf ("load %d\n", (int) fe->loadUserSkin (FM9Editor::skinFolder().getChildFile (args[args.indexOf ("--loadskin") + 1] + ".fm9skin")));
            if (args.contains ("--catalog")) fe->openCatalog();
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
