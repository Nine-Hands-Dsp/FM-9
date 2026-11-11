// FM-9 : tests audio hors DAW + captures de la fenetre
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    if (args.isEmpty() || args.contains ("--test")) { audioTests(); firmwareTest(); }
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
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        const float scale = arg ("--scale", 1.0f);
        if (auto* fe = dynamic_cast<FM9Editor*> (ed.get())) fe->prepareSnapshot (scale, (int) arg ("--mode", 0), (int) arg ("--page", 0), (int) arg ("--popup", 0));
        if (auto* fw = p.firmware()) { juce::ignoreUnused (fw); }
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        juce::File out (args[args.indexOf ("--shot") + 1]);
        out.deleteFile(); juce::FileOutputStream os (out); juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("capture %s (%dx%d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
    std::printf ("%d echec(s)\n", fails);
    return fails == 0 ? 0 : 1;
}
