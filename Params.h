// FM-9 (NINE HANDS | DSP) - identifiants et creation des parametres
#pragma once
#include <JuceHeader.h>
#include "Dx7Patch.h"
#include "ArpSeq.h"

namespace fm9
{
inline const char* const opFieldIds[21] = { "r1","r2","r3","r4","l1","l2","l3","l4","bp","ld","rd","lc","rc","rs","ams","kvs","ol","mode","fc","ff","det" };
inline const char* const opFieldNames[21] = { "EG Rate 1","EG Rate 2","EG Rate 3","EG Rate 4","EG Level 1","EG Level 2","EG Level 3","EG Level 4",
    "Break Point","L Depth","R Depth","L Curve","R Curve","Rate Scaling","Amp Mod Sens","Velocity Sens","Output Level","Fixed Freq","Coarse","Fine","Detune" };
inline const char* const globIds[19] = { "pr1","pr2","pr3","pr4","pl1","pl2","pl3","pl4","alg","fb","oks","lfs","lfd","lpmd","lamd","lfks","lfw","lpms","trnsp" };
inline const char* const globNames[19] = { "Pitch EG Rate 1","Pitch EG Rate 2","Pitch EG Rate 3","Pitch EG Rate 4","Pitch EG Level 1","Pitch EG Level 2",
    "Pitch EG Level 3","Pitch EG Level 4","Algorithm","Feedback","Osc Key Sync","LFO Speed","LFO Delay","LFO Pitch Depth","LFO Amp Depth","LFO Key Sync","LFO Wave","LFO Pitch Sens","Transpose" };

inline juce::String opParamId (int op, int field) { return "op" + juce::String (op) + "_" + opFieldIds[field]; }
inline juce::String opOnId (int op) { return "op" + juce::String (op) + "_on"; }

inline const char* const fxKeys[6] = { "filter", "chorus", "phaser", "drive", "delay", "reverb" };
inline const char* const fxLabels[6] = { "FILTER", "CHORUS", "PHASER", "DISTORTION", "DELAY", "REVERB" };
inline const char* const fxALabels[6] = { "Cutoff", "Freq", "Freq", "Gain", "Rate", "Decay" };
inline const char* const fxBLabels[6] = { "Q", "Depth", "Depth", "Tone", "Decay", "Tone" };
inline const char* const fxShort[6] = { "Filter", "Chorus", "Phaser", "Distortion", "Delay", "Reverb" };
// reglages de chaque effet sur KNOB1-3 (KNOB4 = marche / arret), comme le manuel de la FM-1
struct FxKnob { const char* what; const char* label; };
inline const FxKnob fxKnobs[6][3] = {
    { { "type", "TYPE" },   { "a", "CUTOFF" }, { "b", "Q" } },          // filtre
    { { "a", "FREQ" },      { "b", "DEPTH" },  { "mix", "MIX" } },      // chorus
    { { "a", "FREQ" },      { "b", "DEPTH" },  { "mix", "MIX" } },      // phaser
    { { "a", "GAIN" },      { "b", "TONE" },   { "mix", "LEVEL" } },    // distorsion
    { { "b", "DECAY" },     { "a", "RATE" },   { "mix", "MIX" } },      // delay
    { { "type", "TYPE" },   { "a", "DECAY" },  { "mix", "MIX" } } };    // reverb
inline juce::String fxId (int fx, const char* what) { return juce::String (fxKeys[fx]) + "_" + what; }

inline const juce::StringArray& delayDivNames()
{
    static const juce::StringArray n { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1/1" };
    return n;
}
inline double delayDivBeats (int i)
{
    static const double b[] = { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return b[juce::jlimit (0, 11, i)];
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    int version = 1;
    const auto init = initVoice();

    for (int op = 1; op <= 6; ++op)
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("op" + String (op), "OP" + String (op), "|");
        for (int f = 0; f < 21; ++f)
        {
            const int idx = vp::opBase (op) + f;
            grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { opParamId (op, f), version },
                "OP" + String (op) + " " + opFieldNames[f], 0, voiceMax (idx), init[(size_t) idx]));
        }
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { opOnId (op), version }, "OP" + String (op) + " On", true));
        L.add (std::move (grp));
    }
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("voice", "Voice", "|");
        for (int g = 0; g < 19; ++g)
        {
            const int idx = vp::PR1 + g;
            grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { globIds[g], version }, globNames[g], 0, voiceMax (idx), init[(size_t) idx]));
        }
        L.add (std::move (grp));
    }
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("play", "Play", "|");
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "volume", version }, "Volume", NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f,
                                                              AudioParameterFloatAttributes().withLabel ("dB")));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "poly", version }, "Polyphony", 1, 16, 12));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "mono", version }, "Mono", false));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "glideMode", version }, "Glide Mode", StringArray { "FULL TIME", "FINGERED" }, 0));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "glide", version }, "Glide", 0, 100, 0));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "bendUp", version }, "Pitch Bend Up", 1, 24, 2));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "bendDown", version }, "Pitch Bend Down", 1, 24, 2));
        {
            StringArray ch { "ALL" }; for (int c = 1; c <= 16; ++c) ch.add (String (c));
            grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "midiCh", version }, "MIDI Channel", ch, 0));
        }
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "kbdVel", version }, "Keyboard Velocity", 1, 127, 100));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "wheel", version }, "Mod Wheel Range", 0, 99, 50));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "wheelDest", version }, "Mod Wheel Target", StringArray { "PITCH", "PITCH+AMP", "AMP" }, 0));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "atRange", version }, "Aftertouch Range", 0, 99, 0));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "adsrOn", version }, "ADSR", false));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "attack", version }, "Attack", NormalisableRange<float> (0.001f, 5.0f, 0.0f, 0.35f), 0.005f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "decay", version }, "Decay", NormalisableRange<float> (0.01f, 5.0f, 0.0f, 0.35f), 0.3f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", version }, "Sustain", NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "release", version }, "Release", NormalisableRange<float> (0.01f, 8.0f, 0.0f, 0.35f), 0.3f));
        L.add (std::move (grp));
    }
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("fx", "Effects", "|");
        const float defMix[6] { 1.0f, 0.5f, 0.5f, 0.6f, 0.3f, 0.3f };
        const float defA[6] { 0.75f, 0.3f, 0.3f, 0.4f, 0.55f, 0.5f };
        const float defB[6] { 0.2f, 0.5f, 0.5f, 0.6f, 0.35f, 0.5f };
        for (int f = 0; f < 6; ++f)
        {
            const String nm = String (fxLabels[f]).substring (0, 1) + String (fxLabels[f]).substring (1).toLowerCase();
            grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { fxId (f, "on"), version }, nm + " On", false));
            grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { fxId (f, "mix"), version }, nm + (f == fxDrive ? " Level" : " Mix"), NormalisableRange<float> (0, 1), defMix[f]));
            grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { fxId (f, "a"), version }, nm + " " + String (fxALabels[f]).toLowerCase(), NormalisableRange<float> (0, 1), defA[f]));
            grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { fxId (f, "b"), version }, nm + " " + String (fxBLabels[f]).toLowerCase(), NormalisableRange<float> (0, 1), defB[f]));
        }
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "filterType", version }, "Filter Type", StringArray { "LOW-PASS", "HIGH-PASS", "BAND-PASS" }, 0));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "reverbType", version }, "Reverb Type", StringArray { "ROOM", "HALL", "PLATE" }, 1));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "delaySync", version }, "Delay Sync", false));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "delayDiv", version }, "Delay Division", delayDivNames(), 6));
        L.add (std::move (grp));
    }
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("arp", "Arpeggiator", "|");
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "arpOn", version }, "Arp On", false));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "arpMode", version }, "Arp Mode", Arpeggiator::modeNames(), 0));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "arpOct", version }, "Arp Octaves", 1, 4, 1));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "arpRate", version }, "Arp Rate", rateNames(), 3));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "arpGate", version }, "Arp Gate", NormalisableRange<float> (0.2f, 1.0f), 0.5f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "arpSwing", version }, "Arp Swing", NormalisableRange<float> (0.0f, 0.5f), 0.0f));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "arpLatch", version }, "Arp Latch", false));
        L.add (std::move (grp));
    }
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("seq", "Sequencer", "|");
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "seqActive", version }, "Seq Mode", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "seqOn", version }, "Seq Play", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "seqRec", version }, "Seq Rec", false));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "seqMode", version }, "Seq Mode", StringArray { "MONO", "POLY" }, 1));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "seqRate", version }, "Seq Rate", rateNames(), 3));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "seqGate", version }, "Seq Gate", NormalisableRange<float> (0.2f, 1.0f), 0.5f));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "seqSwing", version }, "Seq Swing", NormalisableRange<float> (0.0f, 0.5f), 0.0f));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "seqTrans", version }, "Seq Transpose", -24, 24, 0));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "seqPattern", version }, "Seq Pattern", 1, 16, 1));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "seqLen", version }, "Seq Steps", 1, 16, 16));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { "seqChain", version }, "Seq Chain", 1, 16, 1));
        L.add (std::move (grp));
    }
    {
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("clock", "Clock", "|");
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { "sync", version }, "Tempo Sync", true));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", version }, "Tempo", NormalisableRange<float> (30.0f, 300.0f, 1.0f), 120.0f,
                                                              AudioParameterFloatAttributes().withLabel ("BPM")));
        L.add (std::move (grp));
    }
    return L;
}
} // namespace fm9
