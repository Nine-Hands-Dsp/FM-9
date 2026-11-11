// FM-9 (NINE HANDS | DSP) - facade d'apres la M-VAVE FM-1 (fonctionnement du manuel de la FM-1)
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Layout.h"
#include "NHKnob.h"
#include "NineHandsMenu.h"
namespace fm9 { class FirmwareCatalogView; }

class FM9Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FM9Editor (FM9Processor&);
    ~FM9Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    // ecran interactif : zones (en unites) et action selon l'endroit
    enum ScreenZone { zNone, zTitle, zBar, zSlot, zFxCell, zCentre };
    ScreenZone screenZoneAt (juce::Point<float> u, int& index) const;
    void showPresetMenu();
    int screenDrag = -1; float screenDragAcc = 0; float screenWheelAcc = 0;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr float baseW = fm9::layout::unitsW, baseH = fm9::layout::unitsH;
    static constexpr int numSizes = 4;
    static int sizeHeight (int idx);
    void setScaledHeight (int h);

    enum Mode { mHome = 0, mFx, mEnv, mLfo, mOp, mPitch, mVoice, mGlobal, mArp, mSeq, mCount };
    // outil de rendu : etat de demonstration + caches a l'echelle de la capture
    void prepareSnapshot (float scale, int mode, int page, int popup = 0);

private:
    struct Slot { juce::String id, label; };
    struct PageInfo { juce::String name; int numPages = 1; Slot s[4]; };

    void timerCallback() override;
    void useSoftwareRenderer();
    void askRepaint (juce::Rectangle<int> r);
    void rebuildCaches (float scale = 0.0f);
    float physicalScale() const;
    juce::Rectangle<int> toPx (juce::Rectangle<float> r) const;
    juce::Point<float> toUnits (juce::Point<float> p) const { return p / ((float) getHeight() / baseH); }

    PageInfo pageInfo() const;
    int& pageRef();
    juce::String formatValue (const juce::String& id) const;
    juce::String slotValue (const Slot& s) const;
    void turnSlot (int k, int steps, bool fine);
    void stepParam (const juce::String& id, int steps, bool fine, const juce::String& label);
    void setParam (const juce::String& id, float plain);
    float getParam (const juce::String& id) const;
    void flash (const juce::String& text);
    void popup (const juce::String& title, const juce::String& value, int kind = 1);
    void goMode (Mode m);
    void fnPress (int idx);
    void fnRelease (int idx, bool longPress);
    void selectTurn (int steps);
    int noteForKey (bool black, int idx) const;
    int keyAt (juce::Point<float> u, bool& black) const;
    void releaseMouseNote();
    bool blinkOn (double periodMs) const;

    void drawPrint (juce::Graphics& g);
    void drawDynamic (juce::Graphics& g);
    void drawScreen (juce::Graphics& g);
    void drawAlgorithm (juce::Graphics& g, juce::Rectangle<float> r, int alg, float u);
    void drawDrum (juce::Graphics& g);
    void drawEmbossed (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float h, bool lit, juce::Colour litCol);
    void drawKeyLed (juce::Graphics& g, juce::Rectangle<float> r, float level, juce::Colour c);

    void showContextMenu (const juce::String& learnId = {}, int ctl = -1);   // ctl : commande physique (FM9Processor::ctlId)
    void midiMapAction (int r);
    void syncShift() { proc.kbShift.store (12 * octave + kbTrans); }
    void openSaveDrum();
    void closeDrum (bool accept);
    void renameVoice();
    void installFromGitHub();
    void installFromFile();
    void activateFirmware (const juce::String& name);

    // mode firmware : ecran du firmware, touches tenues
    juce::Image fwImage;
    uint32_t fwDrawnSerial = 0;
    int fwHeldKey = -1;

    FM9Processor& proc;
    juce::Image background, bgScaled;
public:
    // couleurs de la facade, modifiables a la volee (skins + panneau de couleurs)
    static constexpr int numElements = 5;            // coque, paves, touches, serigraphie, LED
    static const char* elementKey (int e);
    static const char* elementName (int e);
    juce::Colour elementColour (int e) const;
    int glowPercent() const;                          // halo des lumieres des touches et boutons (0 a 100 %)
    void setGlowPercent (int g);
private:
    juce::Colour shellCol, padCol, keyCol { 0xff1e1f22 }, inkCol, ledCol;
    juce::Image faceBase, faceMaps, baseScaled, mapsScaled;
    bool faceDirty = true, logoWhite = false;
    juce::Point<int> lastMenuPos;
    int skinIndex() const;
    void loadColours();
    void updateLogos();
    void recomposeFace();
    void applySkin (const juce::String& id, bool rebuild);
public:
    void setElementColour (int e, juce::Colour c);
    void openColourPanel (juce::Point<int> at, int element = 0);
    void setSnapshotColours (const juce::String& spec);
    static juce::File skinFolder();
    static juce::Array<juce::File> userSkins();
    juce::String currentSkinJson (const juce::String& name) const;
    bool saveUserSkin (const juce::String& name);
    bool loadUserSkin (const juce::File& f);
    void askSaveSkin();
    void openCatalog();
    void firmwareActivated (const juce::String& name);
    std::unique_ptr<fm9::FirmwareCatalogView> catalog;
    juce::PopupMenu buildSkinMenu();
    juce::Image renderMenuPreview (const juce::PopupMenu& m, const juce::String& highlight, float scale, int* highlightY = nullptr);
    juce::PopupMenu buildMainMenuForPreview();
private:
    int cacheW = 0, cacheH = 0;
    float cacheScale = 0.0f, forcedScale = 0.0f;
    int resizeSettle = 0;
    void* lastPeer = nullptr;
    double lastPaintMs = 0, repaintAskedMs = 0, lastScaleCheckMs = 0, lastRendererCheckMs = 0;

    juce::Typeface::Ptr med, bold, light;
    std::unique_ptr<juce::Drawable> logo, fm9Logo;
    NineHandsMenu::MenuLnf menuLnf;
    std::unique_ptr<juce::FileChooser> chooser;

    nh::RotaryLayers::Layers knobLayers, encLayers;
    std::unique_ptr<nh::Knob> master;
    std::unique_ptr<nh::Encoder> enc[7];          // select, presets, algorithm, knob1..4

    // etat de la facade
    Mode mode = mHome;
    int pages[mCount] {};                         // page courante de chaque mode
    int opPages[7] {};                            // page retenue pour chaque operateur
    int op = 1, fxSlot = 0, octave = 0, kbTrans = 0;
    bool editMode = false, fxGrab = false;
    int mouseNote = -1;
    int heldFn = -1, heldBlack = -1, heldOct = -1;
    double heldSince = 0;
    bool holdUsed = false;                        // la touche tenue a servi (SELECT tourne, sauvegarde faite...)
    float octDragAcc = 0;
    int copySrc = 0, resetBank = 0;
    juce::String flashText;
    double flashUntil = 0;
    juce::String popTitle, popValue;
    int popKind = 0;                              // 1 texte, 2 liste de presets, 3 schema d'algorithme
    double popUntil = 0;

    // tambour de sauvegarde vers un autre emplacement (menu clic droit), centre sur la touche SAVE
    bool drumOpen = false;
    int drumIndex = 0;
    float drumScroll = 0;

    juce::String drawnScreenSig, drawnPanelSig;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FM9Editor)
};
