// FM-9 - reperes de la facade (genere par tools/make_art.py), repere 1500 x 900 unites
#pragma once
#include <JuceHeader.h>

namespace fm9::layout
{
constexpr float unitsW = 1500.0f, unitsH = 900.0f;
inline const juce::Rectangle<float> screen = juce::Rectangle<float> { 371.67f, 80.00f, 394.16f, 394.17f };
inline const juce::Rectangle<float> bezel = juce::Rectangle<float> { 362.50f, 70.83f, 412.50f, 412.50f };
constexpr float knobD = 74.28f;
constexpr juce::Point<float> knob_master { 126.67f, 140.83f };
constexpr juce::Point<float> knob_select { 283.33f, 140.83f };
constexpr juce::Point<float> knob_presets { 126.67f, 298.33f };
constexpr juce::Point<float> knob_algorithm { 283.33f, 298.33f };
constexpr juce::Point<float> knob_knob1 { 845.00f, 139.17f };
constexpr juce::Point<float> knob_knob2 { 1013.33f, 139.17f };
constexpr juce::Point<float> knob_knob3 { 1181.67f, 139.17f };
constexpr juce::Point<float> knob_knob4 { 1351.67f, 139.17f };
inline const juce::Rectangle<float> whiteKeys[16] = {
    juce::Rectangle<float> { 80.83f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 165.38f, 675.00f, 66.66f, 133.33f },
    juce::Rectangle<float> { 249.92f, 675.00f, 66.66f, 133.33f },
    juce::Rectangle<float> { 334.46f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 419.00f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 503.54f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 588.08f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 672.62f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 757.17f, 675.00f, 66.66f, 133.33f },
    juce::Rectangle<float> { 841.71f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 926.25f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 1010.79f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 1095.33f, 675.00f, 66.67f, 133.33f },
    juce::Rectangle<float> { 1179.88f, 675.00f, 66.66f, 133.33f },
    juce::Rectangle<float> { 1264.42f, 675.00f, 66.66f, 133.33f },
    juce::Rectangle<float> { 1348.96f, 675.00f, 66.66f, 133.33f } };
inline const juce::Rectangle<float> blackKeys[11] = {
    juce::Rectangle<float> { 126.67f, 535.00f, 66.66f, 131.67f },
    juce::Rectangle<float> { 210.83f, 535.00f, 66.67f, 131.67f },
    juce::Rectangle<float> { 294.17f, 535.00f, 66.66f, 131.67f },
    juce::Rectangle<float> { 464.17f, 535.00f, 66.66f, 131.67f },
    juce::Rectangle<float> { 547.50f, 535.00f, 66.67f, 131.67f },
    juce::Rectangle<float> { 716.67f, 535.00f, 66.66f, 131.67f },
    juce::Rectangle<float> { 800.83f, 535.00f, 66.67f, 131.67f },
    juce::Rectangle<float> { 885.00f, 535.00f, 66.67f, 131.67f },
    juce::Rectangle<float> { 1053.33f, 535.00f, 66.67f, 131.67f },
    juce::Rectangle<float> { 1137.50f, 535.00f, 66.67f, 131.67f },
    juce::Rectangle<float> { 1305.83f, 535.00f, 66.67f, 131.67f } };
inline const juce::Rectangle<float> fnKeys[12] = {
    juce::Rectangle<float> { 829.17f, 270.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 921.67f, 270.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1015.83f, 270.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1109.17f, 270.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1203.33f, 270.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1297.50f, 270.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 829.17f, 365.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 921.67f, 365.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1015.83f, 365.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1109.17f, 365.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1203.33f, 365.83f, 75.00f, 75.00f },
    juce::Rectangle<float> { 1297.50f, 365.83f, 75.00f, 75.00f } };
inline const juce::Rectangle<float> octKeys[2] = {
    juce::Rectangle<float> { 112.08f, 396.67f, 83.34f, 55.00f },
    juce::Rectangle<float> { 213.75f, 396.67f, 83.33f, 55.00f } };
constexpr juce::Point<float> logoCentre { 750.00f, 870.00f };
constexpr float logoW = 166.67f;
constexpr float labelsTopY = 72.50f, labelsMidY = 229.17f;
}
