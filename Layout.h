// FM-9 - reperes de la facade (genere par tools/make_art.py d'apres FM-9_mockup_Design_v001.svg), repere 1500 x 900 unites
#pragma once
#include <JuceHeader.h>

namespace fm9::layout
{
constexpr float unitsW = 1500.0f, unitsH = 900.0f;
inline const juce::Rectangle<float> shell = juce::Rectangle<float> { 0.00f, 0.00f, 1500.00f, 900.00f };
constexpr float shellR = 76.22f;
inline const juce::Rectangle<float> screen = juce::Rectangle<float> { 381.05f, 67.52f, 386.00f, 386.00f };
inline const juce::Rectangle<float> bezel = juce::Rectangle<float> { 372.05f, 58.52f, 404.00f, 404.00f };
constexpr float knobD = 86.40f, knobR = 33.75f;
constexpr juce::Point<float> knob_master { 128.00f, 145.00f };
constexpr juce::Point<float> knob_select { 283.00f, 145.00f };
constexpr juce::Point<float> knob_presets { 128.00f, 277.71f };
constexpr juce::Point<float> knob_algorithm { 283.00f, 277.71f };
constexpr juce::Point<float> knob_knob1 { 906.50f, 145.00f };
constexpr juce::Point<float> knob_knob2 { 1061.50f, 145.00f };
constexpr juce::Point<float> knob_knob3 { 1216.50f, 145.00f };
constexpr juce::Point<float> knob_knob4 { 1371.50f, 145.00f };
inline const juce::Rectangle<float> whiteKeys[16] = {
    juce::Rectangle<float> { 90.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 174.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 258.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 342.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 426.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 510.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 594.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 678.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 762.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 846.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 930.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1014.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1098.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1182.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1266.00f, 688.25f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1350.00f, 688.25f, 60.00f, 133.00f } };
inline const juce::Rectangle<float> blackKeys[11] = {
    juce::Rectangle<float> { 132.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 216.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 300.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 468.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 552.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 720.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 804.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 888.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1056.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1140.00f, 540.75f, 60.00f, 133.00f },
    juce::Rectangle<float> { 1308.00f, 540.75f, 60.00f, 133.00f } };
inline const juce::Rectangle<float> fnKeys[12] = {
    juce::Rectangle<float> { 860.88f, 271.38f, 70.00f, 70.00f },
    juce::Rectangle<float> { 958.12f, 271.38f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1055.38f, 271.38f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1152.62f, 271.38f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1249.88f, 271.38f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1347.12f, 271.38f, 70.00f, 70.00f },
    juce::Rectangle<float> { 860.88f, 368.62f, 70.00f, 70.00f },
    juce::Rectangle<float> { 958.12f, 368.62f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1055.38f, 368.62f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1152.62f, 368.62f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1249.88f, 368.62f, 70.00f, 70.00f },
    juce::Rectangle<float> { 1347.12f, 368.62f, 70.00f, 70.00f } };
inline const juce::Rectangle<float> octKeys[2] = {
    juce::Rectangle<float> { 121.88f, 403.50f, 70.00f, 35.00f },
    juce::Rectangle<float> { 216.12f, 403.50f, 70.00f, 35.00f } };
inline const juce::Rectangle<float> logoNH = juce::Rectangle<float> { 667.50f, 854.50f, 165.00f, 15.00f };
inline const juce::Rectangle<float> logoFM9 = juce::Rectangle<float> { 1187.50f, 37.50f, 209.00f, 22.00f };
}
