// FM-9 (NINE HANDS | DSP) - facade d'apres la M-VAVE FM-1, fonctionnement du manuel de la FM-1
#include "PluginEditor.h"
#include "FirmwareCatalog.h"
#include "WigaWindowSize.h"

using namespace juce;
namespace L = fm9::layout;

// --------------------------------------------------------------------------------------------- couleurs
// --------------------------------------------------------------------------------------------- skins
// coloris officiels de la M-VAVE FM-1 (m-vave.com) : coque, silicone, serigraphie
struct FM9Skin { const char* id; const char* name; uint32 shell, pad, keys, ink; };
static const FM9Skin skins[] = {             // memes valeurs que tools/make_art.py (SKINS)
    { "black",      "Black",       0xff3d3d3c, 0xff303032, 0xff1e1f22, 0xffe9e7e3 },
    { "purple",     "Purple",      0xffb48cc6, 0xff685e9e, 0xff7a6fb2, 0xfff7f3f9 },
    { "orange",     "Orange",      0xffbe6b4a, 0xff7c4a3f, 0xff91574a, 0xfff8f1ec },
    { "blackgreen", "Black-Green", 0xff464548, 0xff62b2a6, 0xff79d1c4, 0xffe9e7e3 },
    { "coolgray",   "Cool Gray",   0xffe2dad1, 0xff3c4044, 0xff1e1f22, 0xff2b2b2d },
    { "whiteblue",  "White-Blue",  0xffeeebe5, 0xff48637c, 0xff567592, 0xff2b2d33 },
};
static constexpr int numSkins = (int) (sizeof (skins) / sizeof (skins[0]));
static constexpr int defaultSkin = 4;              // Cool Gray : la FM-9 d'origine
static const uint32 defaultLed = 0xfff6f8fb;
static const Colour cScreen (0xff06080b), cText (0xffe9eef2), cDim (0xff7f8a96), cFaint (0xff262d36);
static const Colour cAccent (0xff4fc3f7), cGreen (0xff5be08a), cRed (0xffff5a52), cOrange (0xffffa23a), cYellow (0xfff2d64b);

static const char* fnNames[12] = { "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
static const char* blackNames[11] = { "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "PIT", "GLO", "MONO", "POLY", "" };
static const int whiteOffs[16] = { 0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26 };
static const int blackOffs[11] = { 1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25 };
static constexpr int baseNote = 53;               // FM-1 : note = position + 53 + octave x 12 + transposition

static Font fontOf (const Typeface::Ptr& tf, float h) { return Font (FontOptions (tf).withHeight (h)); }

static String noteName (int midi)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return String (n[((midi % 12) + 12) % 12]) + String (midi / 12 - 1);
}

// --------------------------------------------------------------------------------------------- construction
FM9Editor::FM9Editor (FM9Processor& p) : AudioProcessorEditor (p), proc (p)
{
    setOpaque (true);
    med   = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
    bold  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    light = Typeface::createSystemTypefaceFor (BinaryData::PoppinsLight_ttf, BinaryData::PoppinsLight_ttfSize);
    menuLnf.reg = light; menuLnf.med = med; menuLnf.bold = bold;
    faceBase = ImageCache::getFromMemory (BinaryData::face_base_png, BinaryData::face_base_pngSize);
    faceMaps = ImageCache::getFromMemory (BinaryData::face_maps_png, BinaryData::face_maps_pngSize);
    loadColours();

    auto img = [] (const char* d, int s) { return ImageCache::getFromMemory (d, s); };
    knobLayers.shadow    = img (BinaryData::knob_shadow_png, BinaryData::knob_shadow_pngSize);
    knobLayers.crown     = img (BinaryData::knob_crown_png, BinaryData::knob_crown_pngSize);
    knobLayers.base      = img (BinaryData::knob_base_png, BinaryData::knob_base_pngSize);           // centre gris, fixe
    knobLayers.highlight = img (BinaryData::knob_highlight_png, BinaryData::knob_highlight_pngSize);     // diffuse de la couronne, fixe
    knobLayers.indicator = img (BinaryData::knob_indicator_png, BinaryData::knob_indicator_pngSize);     // point blanc chaud, tourne
    encLayers = knobLayers;

    nh::RotaryLayers::Style ms;                    // MASTER : potentiometre 300 deg
    ms.startAngle = degreesToRadians (-150.0f); ms.endAngle = degreesToRadians (150.0f);
    master = std::make_unique<nh::Knob> (*proc.apvts.getParameter ("volume"), knobLayers, ms);
    master->onValueChange = [this] { if (isShowing() && proc.firmware() == nullptr) popup ("Volume", formatValue ("volume")); };
    master->onRightClick = [this] (const MouseEvent&) { showContextMenu ("volume"); };
    addAndMakeVisible (*master);

    for (int i = 0; i < 7; ++i) { enc[i] = std::make_unique<nh::Encoder> (encLayers); addAndMakeVisible (*enc[i]); }
    enc[0]->onStep = [this] (int s, bool) { if (auto* fw = proc.firmware()) { fw->encoder (0, s); return; } selectTurn (s); };
    enc[0]->onPress = [this]
    {
        // SELECT (appui) : confirme la copie de voix / la reinitialisation d'une banque
        const auto pi = pageInfo();
        if (mode == mVoice && pages[mVoice] == 1)
        {
            proc.applyVoiceToParams (proc.bankVoice (copySrc));
            popup ("Copy Voice", FM9Processor::programLabel (copySrc) + " " + String (fm9::voiceName (proc.bankVoice (copySrc))));
        }
        else if (mode == mGlobal && pages[mGlobal] == 1)
        {
            proc.resetBankToFactory (resetBank);
            popup ("Reset Success", "Bank " + String::charToString ((juce_wchar) ('A' + resetBank)));
        }
        ignoreUnused (pi);
    };
    enc[0]->onRightClick = [this] (const MouseEvent&) { showContextMenu(); };
    enc[1]->onStep = [this] (int s, bool)
    {
        if (auto* fw = proc.firmware()) { fw->encoder (2, s); return; }
        if (getParam ("seqActive") > 0.5f) { popup ("SEQ", "Presets inactive"); return; }
        proc.nextProgram (s);
        popup ({}, {}, 2);
    };
    enc[1]->onRightClick = [this] (const MouseEvent&) { showContextMenu(); };
    enc[2]->onStep = [this] (int s, bool)
    {
        if (auto* fw = proc.firmware()) { fw->encoder (1, s); return; }
        stepParam ("alg", s, false, "Algorithm");
        popup ({}, {}, 3);
    };
    enc[2]->onRightClick = [this] (const MouseEvent&) { showContextMenu ("alg"); };
    for (int k = 0; k < 4; ++k)
    {
        enc[3 + k]->onStep = [this, k] (int s, bool fine) { if (auto* fw = proc.firmware()) { fw->encoder (3 + k, s); return; } turnSlot (k, s, fine); };
        enc[3 + k]->onRightClick = [this, k] (const MouseEvent&)
        {
            const auto id = pageInfo().s[k].id;
            showContextMenu (id.startsWith ("!") ? String() : id);
        };
    }

    setWantsKeyboardFocus (true);
    const int defH = sizeHeight (0);
    setResizable (true, false);
    setResizeLimits (roundToInt (defH * baseW / baseH), defH, roundToInt (sizeHeight (numSizes - 1) * 1.2f * baseW / baseH), roundToInt (sizeHeight (numSizes - 1) * 1.2f));
    getConstrainer()->setFixedAspectRatio (baseW / baseH);
    setSize (roundToInt (defH * baseW / baseH), defH);
    setScaledHeight (sizeHeight (WigaWindowSize::defaultIndex ("FM-9", numSizes)));
    octave = (int) proc.uiState.getProperty ("octave", 0);
    kbTrans = (int) proc.uiState.getProperty ("transpose", 0);
    if (getParam ("seqActive") > 0.5f) mode = mSeq;
    startTimerHz (30);
}

FM9Editor::~FM9Editor()
{
    stopTimer();
    releaseMouseNote();
}

int FM9Editor::sizeHeight (int idx)
{
    const int h[numSizes] = { 495, 619, 825, 1031 };    // 60 % (defaut, minimum) / 75 % / 100 % / 125 %
    return h[jlimit (0, numSizes - 1, idx)];
}

void FM9Editor::setScaledHeight (int h) { setSize (roundToInt ((float) h * baseW / baseH), h); }

Rectangle<int> FM9Editor::toPx (Rectangle<float> r) const
{
    const float s = (float) getHeight() / baseH;
    return r.transformedBy (AffineTransform::scale (s)).getSmallestIntegerContainer().expanded (2);
}

float FM9Editor::physicalScale() const
{
    if (forcedScale > 0.0f) return forcedScale;
    return jlimit (1.0f, 4.0f, (float) Component::getApproximateScaleFactorForComponent (this));
}

void FM9Editor::resized()
{
    // anti-gel : jamais de setSize ici
    if (catalog != nullptr) catalog->setBounds (getLocalBounds());
    const float s = (float) getHeight() / baseH;
    auto place = [s] (Component& c, Point<float> centre)
    {
        const float d = L::knobD;
        c.setBounds (Rectangle<float> (centre.x - d * 0.5f, centre.y - d * 0.5f, d, d).transformedBy (AffineTransform::scale (s)).toNearestInt());
    };
    place (*master, L::knob_master);
    place (*enc[0], L::knob_select);
    place (*enc[1], L::knob_presets);
    place (*enc[2], L::knob_algorithm);
    place (*enc[3], L::knob_knob1);
    place (*enc[4], L::knob_knob2);
    place (*enc[5], L::knob_knob3);
    place (*enc[6], L::knob_knob4);
    resizeSettle = 4;
}

// --------------------------------------------------------------------------------------------- anti-gel v2
void FM9Editor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer != lastPeer) { lastPeer = peer; repaint(); }
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw) peer->setCurrentRenderingEngine (sw);
    }
   #endif
}
void FM9Editor::parentHierarchyChanged() { useSoftwareRenderer(); }
void FM9Editor::visibilityChanged() { useSoftwareRenderer(); if (isShowing()) repaint(); }
void FM9Editor::askRepaint (Rectangle<int> r)
{
    if (repaintAskedMs <= 0.0) repaintAskedMs = Time::getMillisecondCounterHiRes();
    repaint (r);
}

// --------------------------------------------------------------------------------------------- couleurs de la facade
const char* FM9Editor::elementKey (int e) { static const char* k[numElements] = { "c_shell", "c_pad", "c_keys", "c_ink", "c_led" }; return k[e]; }
const char* FM9Editor::elementName (int e) { static const char* n[numElements] = { "Shell", "Pads", "Keys", "Labels", "LEDs" }; return n[e]; }

int FM9Editor::skinIndex() const
{
    const auto id = proc.uiState.getProperty ("skin", skins[defaultSkin].id).toString();
    for (int i = 0; i < numSkins; ++i) if (id == skins[i].id) return i;
    return id == "custom" ? -1 : defaultSkin;
}

Colour FM9Editor::elementColour (int e) const
{
    switch (e) { case 0: return shellCol; case 1: return padCol; case 2: return keyCol; case 3: return inkCol; default: return ledCol; }
}

void FM9Editor::loadColours()
{
    const int k = jmax (0, skinIndex());
    const auto& sk = skins[skinIndex() < 0 ? defaultSkin : k];
    const uint32 def[numElements] = { sk.shell, sk.pad, sk.keys, sk.ink, defaultLed };
    for (int e = 0; e < numElements; ++e)
    {
        const auto v = proc.uiState.getProperty (elementKey (e)).toString();
        auto c = (skinIndex() < 0 && v.isNotEmpty()) ? Colour::fromString (v) : Colour (def[e]);
        // serigraphie automatique (blanche sur coque sombre ou coloree, noire sur coque claire) tant qu'elle n'est pas reglee a la main
        if (e == 3 && skinIndex() < 0 && v.isEmpty())
            c = shellCol.getPerceivedBrightness() > 0.72f ? Colour (0xff2b2b2d) : Colour (0xffeceae6);
        switch (e) { case 0: shellCol = c; break; case 1: padCol = c; break; case 2: keyCol = c; break; case 3: inkCol = c; break; default: ledCol = c; }
    }
    updateLogos();
    faceDirty = true;
}

void FM9Editor::applySkin (const String& id, bool rebuild)
{
    proc.uiState.setProperty ("skin", id, nullptr);
    proc.uiState.removeProperty ("skinName", nullptr);
    for (int e = 0; e < numElements; ++e) proc.uiState.removeProperty (elementKey (e), nullptr);
    loadColours();
    if (rebuild) { recomposeFace(); repaint(); }
}

void FM9Editor::setElementColour (int e, Colour c)
{
    // premier reglage : on part du coloris en cours, puis on passe en skin "Custom"
    if (skinIndex() >= 0)
        for (int i = 0; i < numElements; ++i) if (i != 3) proc.uiState.setProperty (elementKey (i), elementColour (i).toString(), nullptr);
    proc.uiState.setProperty ("skin", "custom", nullptr);
    proc.uiState.removeProperty ("skinName", nullptr);          // modifie : ce n'est plus le preset enregistre
    proc.uiState.setProperty (elementKey (e), c.withAlpha (1.0f).toString(), nullptr);
    loadColours();                                  // recomposition faite par le timer (anti-gel)
}

// --------------------------------------------------------------------------------------------- skins utilisateur (presets de couleurs)
// un fichier .fm9skin (JSON) par skin dans <reglages>/Nine Hands DSP/FM-9/Skins : facile a partager
File FM9Editor::skinFolder() { return FM9Processor::userFolder().getChildFile ("Skins"); }

Array<File> FM9Editor::userSkins()
{
    auto list = skinFolder().findChildFiles (File::findFiles, false, "*.fm9skin");
    list.sort();
    return list;
}

String FM9Editor::currentSkinJson (const String& name) const
{
    auto* o = new DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("plugin", "FM-9");
    const char* keys[numElements] = { "shell", "pads", "keys", "labels", "leds" };
    for (int e = 0; e < numElements; ++e)
    {
        // "labels" absent = serigraphie automatique (suit la coque)
        if (e == 3 && skinIndex() < 0 && proc.uiState.getProperty (elementKey (3)).toString().isEmpty()) continue;
        o->setProperty (keys[e], "#" + elementColour (e).toDisplayString (false));
    }
    return JSON::toString (var (o));
}

bool FM9Editor::saveUserSkin (const String& rawName)
{
    const auto name = File::createLegalFileName (rawName.trim()).substring (0, 40);
    if (name.isEmpty()) return false;
    skinFolder().createDirectory();
    if (! skinFolder().getChildFile (name + ".fm9skin").replaceWithText (currentSkinJson (name))) return false;
    // le skin en cours devient ce preset (coche dans le menu)
    for (int e = 0; e < numElements; ++e)
        if (! (e == 3 && skinIndex() < 0 && proc.uiState.getProperty (elementKey (3)).toString().isEmpty()))
            proc.uiState.setProperty (elementKey (e), elementColour (e).toString(), nullptr);
    proc.uiState.setProperty ("skin", "custom", nullptr);
    proc.uiState.setProperty ("skinName", name, nullptr);
    return true;
}

bool FM9Editor::loadUserSkin (const File& f)
{
    const auto v = JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return false;
    const char* keys[numElements] = { "shell", "pads", "keys", "labels", "leds" };
    proc.uiState.setProperty ("skin", "custom", nullptr);
    int found = 0;
    for (int e = 0; e < numElements; ++e)
    {
        const auto t = v.getProperty (keys[e], {}).toString().trim().removeCharacters ("#");
        if (t.length() == 6) { proc.uiState.setProperty (elementKey (e), "ff" + t, nullptr); ++found; }
        else proc.uiState.removeProperty (elementKey (e), nullptr);
    }
    if (proc.uiState.getProperty (elementKey (4)).toString().isEmpty()) proc.uiState.setProperty (elementKey (4), Colour (defaultLed).toString(), nullptr);
    if (found == 0) { applySkin (skins[defaultSkin].id, true); return false; }
    proc.uiState.setProperty ("skinName", v.getProperty ("name", f.getFileNameWithoutExtension()).toString(), nullptr);
    loadColours();
    recomposeFace();
    return true;
}

void FM9Editor::askSaveSkin()
{
    const auto cur = proc.uiState.getProperty ("skinName").toString();
    auto* w = new AlertWindow ("Save skin", "Skin preset name", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", cur.isNotEmpty() ? cur : String ("My skin"));
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([safe = Component::SafePointer<FM9Editor> (this), w] (int r)
    {
        if (safe == nullptr || r != 1) return;
        const auto name = w->getTextEditorContents ("name");
        safe->popup ("Skin", safe->saveUserSkin (name) ? "Saved" : "Not saved");
    }), true);
}

void FM9Editor::updateLogos()
{
    // logos officiels : NINE HANDS | DSP blanc ou noir selon la serigraphie (jamais recolore autrement)
    const bool white = inkCol.getPerceivedBrightness() > 0.5f;
    if (white == logoWhite && logo != nullptr) return;
    logoWhite = white;
    if (auto xml = XmlDocument::parse (white ? String::fromUTF8 (BinaryData::NineHandsLogoWhite_svg, BinaryData::NineHandsLogoWhite_svgSize)
                                             : String::fromUTF8 (BinaryData::NineHandsLogoBlack_svg, BinaryData::NineHandsLogoBlack_svgSize)))
        logo = Drawable::createFromSVG (*xml);
    auto fm9 = String::fromUTF8 (BinaryData::FM9Logo_svg, BinaryData::FM9Logo_svgSize);
    if (white) fm9 = fm9.replace ("#000000", "#FFFFFF");
    if (auto xml = XmlDocument::parse (fm9))
        fm9Logo = Drawable::createFromSVG (*xml);
}

// fond = base + coque x carte coque + paves x carte paves + touches x carte touches (cartes de tools/make_art.py)
void FM9Editor::recomposeFace()
{
    faceDirty = false;
    if (! baseScaled.isValid() || ! mapsScaled.isValid()) return;
    const int pw = baseScaled.getWidth(), ph = baseScaled.getHeight();
    Image face (Image::RGB, pw, ph, false);
    {
        Image::BitmapData bb (baseScaled, Image::BitmapData::readOnly), bm (mapsScaled, Image::BitmapData::readOnly), bo (face, Image::BitmapData::writeOnly);
        // ~1,5 million de pixels en entier : quelques ms, fait par le timer seulement
        auto q = [] (uint8 c) { return (int) roundToInt ((float) c * 256.0f / 160.0f); };     // poids en 1/256
        const int s0 = q (shellCol.getRed()), s1 = q (shellCol.getGreen()), s2 = q (shellCol.getBlue());
        const int p0 = q (padCol.getRed()),   p1 = q (padCol.getGreen()),   p2 = q (padCol.getBlue());
        const int k0 = q (keyCol.getRed()),   k1 = q (keyCol.getGreen()),   k2 = q (keyCol.getBlue());
        for (int y = 0; y < ph; ++y)
        {
            auto* b = reinterpret_cast<const PixelARGB*> (bb.getLinePointer (y));
            auto* m = reinterpret_cast<const PixelARGB*> (bm.getLinePointer (y));
            auto* o = reinterpret_cast<PixelRGB*> (bo.getLinePointer (y));
            for (int x = 0; x < pw; ++x)
            {
                const int ms = m[x].getRed(), mp = m[x].getGreen(), mk = m[x].getBlue();
                auto ch = [&] (int base, int a, int bq, int c) { return (uint8) jlimit (0, 255, base - 64 + ((ms * a + mp * bq + mk * c + 128) >> 8)); };
                o[x].setARGB (255, ch (b[x].getRed(), s0, p0, k0), ch (b[x].getGreen(), s1, p1, k1), ch (b[x].getBlue(), s2, p2, k2));
            }
        }
    }
    bgScaled = Image (Image::ARGB, pw, ph, true);
    {
        Graphics g (bgScaled);
        const float k = (float) ph / baseH;              // coins arrondis du boitier
        Path shell; shell.addRoundedRectangle (L::shell.getX() * k, L::shell.getY() * k, L::shell.getWidth() * k, L::shell.getHeight() * k, L::shellR * k);
        g.reduceClipRegion (shell);
        g.drawImageAt (face, 0, 0);
        g.addTransform (AffineTransform::scale (k));
        drawPrint (g);
    }
    repaint();
}

void FM9Editor::rebuildCaches (float sc)
{
    if (sc <= 0.0f) sc = physicalScale();
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    if (w == cacheW && h == cacheH && std::abs (sc - cacheScale) < 0.01f && bgScaled.isValid()) return;
    cacheW = w; cacheH = h; cacheScale = sc;
    const int pw = roundToInt ((float) w * sc), ph = roundToInt ((float) h * sc);
    // cartes de la facade a la resolution physique de l'ecran (reduction seulement), puis recomposition
    auto fit = [pw, ph] (const Image& src)
    {
        Image out (Image::ARGB, pw, ph, false);
        Graphics g (out);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (src, Rectangle<float> (0, 0, (float) pw, (float) ph));
        return out;
    };
    baseScaled = fit (faceBase);
    mapsScaled = fit (faceMaps);
    recomposeFace();
}

bool FM9Editor::blinkOn (double periodMs) const
{
    return std::fmod (Time::getMillisecondCounterHiRes(), periodMs) < periodMs * 0.5;
}

// --------------------------------------------------------------------------------------------- pages (manuel FM-1)
int& FM9Editor::pageRef() { return mode == mOp ? opPages[op] : pages[mode]; }

FM9Editor::PageInfo FM9Editor::pageInfo() const
{
    PageInfo pi;
    auto set = [&pi] (std::initializer_list<std::pair<const char*, const char*>> l)
    {
        int i = 0;
        for (auto& e : l) { if (i < 4) { pi.s[i].id = e.first; pi.s[i].label = e.second; } ++i; }
    };
    const int pg = mode == mOp ? opPages[op] : pages[mode];
    switch (mode)
    {
        case mHome: pi.name = "Home"; break;
        case mFx:
        {
            const int f = proc.getFxOrder()[(size_t) jlimit (0, 5, fxSlot)];
            pi.name = String ("FX ") + fm9::fxShort[f];
            for (int k = 0; k < 3; ++k)
            {
                const auto& fk = fm9::fxKnobs[f][k];
                const String w = fk.what;
                pi.s[k].id = w == "type" ? String (f == fm9::fxFilter ? "filterType" : "reverbType") : fm9::fxId (f, fk.what);
                pi.s[k].label = fk.label;
            }
            pi.s[3] = { fm9::fxId (f, "on"), "ON/OFF" };
            break;
        }
        case mEnv:  pi.name = "Envelope"; set ({ { "attack", "ATTACK" }, { "decay", "DECAY" }, { "sustain", "SUSTAIN" }, { "release", "RELEASE" } }); break;
        case mLfo:
            pi.name = "LFO"; pi.numPages = 2;
            if (pg == 0) set ({ { "lfw", "WAVE" }, { "lfs", "SPEED" }, { "lpmd", "PMD" }, { "lamd", "AMD" } });
            else         set ({ { "lfd", "DELAY" }, { "lpms", "PITCH SENS" }, { "lfks", "LFO SYNC" }, { "oks", "OSC SYNC" } });
            break;
        case mOp:
        {
            pi.name = "OP" + String (op); pi.numPages = 6;
            const String o = "op" + String (op) + "_";
            const char* ids[6][4] = { { "det", "fc", "ff", "ol" }, { "r1", "r2", "r3", "r4" }, { "l1", "l2", "l3", "l4" },
                                      { "ams", "kvs", "mode", "" }, { "ld", "rd", "bp", "" }, { "lc", "rc", "rs", "" } };
            const char* labs[6][4] = { { "DETUNE", "COARSE", "FINE", "OUT LEVEL" }, { "RATE 1", "RATE 2", "RATE 3", "RATE 4" },
                                       { "LEVEL 1", "LEVEL 2", "LEVEL 3", "LEVEL 4" }, { "A MODSENS", "KEY VELO", "OSC MODE", "" },
                                       { "L DEPTH", "R DEPTH", "BREAKPOINT", "" }, { "L CURVE", "R CURVE", "RATESCALE", "" } };
            const int p = jlimit (0, 5, pg);
            for (int i = 0; i < 4; ++i) if (*ids[p][i] != 0) { pi.s[i].id = o + ids[p][i]; pi.s[i].label = labs[p][i]; }
            break;
        }
        case mPitch:
            pi.name = "Pitch EG"; pi.numPages = 2;
            if (pg == 0) set ({ { "pr1", "RATE 1" }, { "pr2", "RATE 2" }, { "pr3", "RATE 3" }, { "pr4", "RATE 4" } });
            else         set ({ { "pl1", "LEVEL 1" }, { "pl2", "LEVEL 2" }, { "pl3", "LEVEL 3" }, { "pl4", "LEVEL 4" } });
            break;
        case mVoice:
            pi.name = "Voice"; pi.numPages = 2;
            if (pg == 0) set ({ { "alg", "ALGORITHM" }, { "fb", "FEEDBACK" }, { "oks", "OSC SYNC" }, { "trnsp", "TRANSPOSE" } });
            else         set ({ { "!init", "INIT VOICE" }, { "!rename", "RENAME" }, { "!copy", "COPY VOICE" } });
            break;
        case mGlobal:
            pi.name = "Globe"; pi.numPages = 2;
            if (pg == 0) set ({ { "midiCh", "MIDI CH" }, { "-", "EFFECT CH" }, { "bendUp", "BEND UP" }, { "bendDown", "BEND DOWN" } });
            else         set ({ { "!reset", "RESET" }, { "kbdVel", "VELOCITY" }, { "glideMode", "GLIDE" }, { "glide", "GLIDE VAL" } });
            break;
        case mArp:
            pi.name = "Arpeggio"; pi.numPages = 2;
            if (pg == 0) set ({ { "arpMode", "MODE" }, { "arpOct", "OCTAVE" }, { "arpRate", "RATE" }, { "tempo", "TEMPO" } });
            else         set ({ { "arpGate", "GATE" }, { "arpSwing", "SWING" }, { "arpLatch", "LATCH" }, { "sync", "SYNC" } });
            break;
        case mSeq:
            pi.name = "Sequencer"; pi.numPages = 3;
            if (pg == 0)      set ({ { "seqPattern", "PATTERN" }, { "!clear", "CLEAR" }, { "seqChain", "CHAIN" }, { "seqLen", "STEP" } });
            else if (pg == 1) set ({ { "!voice", "VOICE" }, { "seqRate", "RATE" }, { "tempo", "TEMPO" }, { "seqGate", "GATE" } });
            else              set ({ { "seqSwing", "SWING" }, { "sync", "SYNC" }, { "seqTrans", "TRANSPOSE" } });
            break;
        default: break;
    }
    return pi;
}

float FM9Editor::getParam (const String& id) const
{
    if (auto* v = proc.apvts.getRawParameterValue (id)) return v->load();
    return 0.0f;
}

void FM9Editor::setParam (const String& id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

String FM9Editor::formatValue (const String& id) const
{
    auto* p = proc.apvts.getParameter (id);
    if (p == nullptr) return {};
    const float v = getParam (id);
    const int iv = roundToInt (v);
    if (auto* c = dynamic_cast<AudioParameterChoice*> (p))
    {
        const auto s = c->choices[jlimit (0, c->choices.size() - 1, iv)];
        return id == "filterType" ? StringArray { "Low Pass", "High Pass", "Band Pass" }[iv]
             : id == "reverbType" ? StringArray { "Room", "Hall", "Plate" }[iv] : s;
    }
    if (dynamic_cast<AudioParameterBool*> (p) != nullptr)
    {
        if (id.endsWith ("_mode")) return v > 0.5f ? "Fixed" : "Ratio";
        if (id == "seqMode") return v > 0.5f ? "Poly" : "Mono";
        return v > 0.5f ? "On" : "Off";
    }
    if (id == "alg") return String (iv + 1).paddedLeft ('0', 2);
    if (id == "trnsp") return String (iv) + " " + noteName (iv + 36);
    if (id == "volume") return String (roundToInt (p->getValue() * 100.0f));
    if (id == "tempo") return String (iv) + " BPM";
    if (id == "lfw") { static const char* w[6] = { "Triangle", "Saw Down", "Saw Up", "Square", "Sine", "S&Hold" }; return w[jlimit (0, 5, iv)]; }
    if (id.endsWith ("_lc") || id.endsWith ("_rc")) { static const char* c[4] = { "-LN", "-EX", "+EX", "+LN" }; return c[jlimit (0, 3, iv)]; }
    if (id.endsWith ("_bp")) return noteName (iv + 21);
    if (id.endsWith ("_det")) return (iv > 7 ? "+" : "") + String (iv - 7);
    if (id.endsWith ("_fc") || id.endsWith ("_ff"))
    {
        const String o = id.upToLastOccurrenceOf ("_", true, false);
        const int fc = roundToInt (getParam (o + "fc")), ff = roundToInt (getParam (o + "ff"));
        if (getParam (o + "mode") > 0.5f)
        {
            const double hz = std::pow (10.0, (fc & 3) + ff / 100.0);
            return hz >= 1000 ? String (hz / 1000.0, 2) + "k" : String (hz, hz < 10 ? 2 : 1) + "Hz";
        }
        if (id.endsWith ("_fc")) return String (fc);
        return String (ff) + " x" + String ((fc == 0 ? 0.5 : (double) fc) * (1.0 + ff / 100.0), 2);
    }
    if (id == "seqTrans") return (iv > 0 ? "+" : "") + String (iv);
    if (id == "attack" || id == "decay" || id == "release" || id == "sustain" || id == "arpGate" || id == "seqGate") return String (roundToInt (p->getValue() * 100)) + "%";
    if (id == "arpSwing" || id == "seqSwing") return String (roundToInt (50 + v * 50)) + "%";
    if (id == "filter_a") { const double hz = fm9::FxChain::cutoffHz (v); return String (roundToInt (hz)) + "Hz"; }
    if (id == "filter_b") return String (roundToInt (v * 100));
    if (id == "delay_a" && getParam ("delaySync") > 0.5f) return fm9::delayDivNames()[roundToInt (getParam ("delayDiv"))];
    if (id.endsWith ("_mix") || id.endsWith ("_a") || id.endsWith ("_b")) return String (roundToInt (v * 100)) + "%";
    if (id == "glide") return String (iv);
    return String (iv);
}

String FM9Editor::slotValue (const Slot& s) const
{
    if (s.id == "-") return "-";
    if (s.id == "!init") return "Turn >";
    if (s.id == "!rename") return proc.getVoiceName();
    if (s.id == "!copy") return FM9Processor::programLabel (copySrc);
    if (s.id == "!clear") return "Turn >";
    if (s.id == "!reset") return "Bank " + String::charToString ((juce_wchar) ('A' + resetBank));
    if (s.id == "!voice")
    {
        const int v = proc.patternVoice[(size_t) jlimit (0, 15, roundToInt (getParam ("seqPattern")) - 1)].load();
        return v < 0 ? String ("-") : FM9Processor::programLabel (v);
    }
    return formatValue (s.id);
}

void FM9Editor::turnSlot (int k, int steps, bool fine)
{
    const auto pi = pageInfo();
    const auto& s = pi.s[k];
    if (s.id.isEmpty() || s.id == "-") return;
    if (s.id == "!init") { if (steps > 0) { proc.applyVoiceToParams (fm9::initVoice()); popup ("Init Voice", "Done"); } return; }
    if (s.id == "!rename") { if (steps > 0) renameVoice(); return; }
    if (s.id == "!copy") { copySrc = (copySrc + steps + 128) % 128; popup ("Copy Voice", FM9Processor::programLabel (copySrc) + " " + String (fm9::voiceName (proc.bankVoice (copySrc))) + "\nSELECT = OK"); return; }
    if (s.id == "!reset") { resetBank = (resetBank + steps + 4) % 4; popup ("Reset Patches", "Bank " + String::charToString ((juce_wchar) ('A' + resetBank)) + "\nSELECT = OK"); return; }
    if (s.id == "!clear")
    {
        if (steps > 0) { proc.seqData.clearPattern (roundToInt (getParam ("seqPattern")) - 1); popup ("Pattern", "Cleared"); }
        return;
    }
    if (s.id == "!voice")
    {
        auto& pv = proc.patternVoice[(size_t) jlimit (0, 15, roundToInt (getParam ("seqPattern")) - 1)];
        const int v = jlimit (-1, 127, pv.load() + steps);
        pv.store (v);
        if (v >= 0) proc.setCurrentProgram (v);
        flash ("Voice: " + slotValue (s));
        return;
    }
    if (s.id.endsWith ("_on") && mode == mFx)        // KNOB 4 : horaire = ON, anti-horaire = OFF
    {
        setParam (s.id, steps > 0 ? 1.0f : 0.0f);
        flash (pi.name.fromFirstOccurrenceOf (" ", false, false) + ": " + formatValue (s.id));
        return;
    }
    stepParam (s.id, steps, fine, s.label);
}

void FM9Editor::stepParam (const String& id, int steps, bool fine, const String& label)
{
    auto* p = proc.apvts.getParameter (id);
    if (p == nullptr) return;
    const float cur = getParam (id);
    if (dynamic_cast<AudioParameterBool*> (p) != nullptr) setParam (id, steps > 0 ? 1.0f : 0.0f);
    else if (dynamic_cast<AudioParameterInt*> (p) != nullptr || dynamic_cast<AudioParameterChoice*> (p) != nullptr)
    {
        const auto range = p->getNormalisableRange();
        setParam (id, jlimit (range.start, range.end, (float) roundToInt (cur) + (float) steps));
    }
    else
    {
        const float n = p->convertTo0to1 (cur) + (float) steps * (fine ? 0.002f : 0.01f);
        setParam (id, p->convertFrom0to1 (jlimit (0.0f, 1.0f, n)));
    }
    String lab = label.toLowerCase();
    lab = lab.substring (0, 1).toUpperCase() + lab.substring (1);
    flash (lab + ": " + formatValue (id));
}

void FM9Editor::flash (const String& text) { flashText = text; flashUntil = Time::getMillisecondCounterHiRes() + 1500.0; }

void FM9Editor::popup (const String& title, const String& value, int kind)
{
    popTitle = title; popValue = value; popKind = kind;
    popUntil = Time::getMillisecondCounterHiRes() + (kind == 1 ? 1100.0 : 1300.0);
}

void FM9Editor::goMode (Mode m)
{
    mode = m; fxGrab = false;
    if (m == mHome) editMode = false;
}

// SELECT : pages du mode courant ; en FX, emplacement de la chaine (ou deplacement de l'effet saisi par SEL) ;
// REC tenu : position d'enregistrement ; note tenue pendant l'enregistrement : note longue
void FM9Editor::selectTurn (int s)
{
    if (getParam ("seqRec") > 0.5f && mode == mSeq)
    {
        if (heldFn == 11)
        {
            holdUsed = true;
            const int len = roundToInt (getParam ("seqLen"));
            proc.recStep.store (((proc.recStep.load() + s) % len + len) % len);
            flash ("Step " + String (proc.recStep.load() + 1));
            return;
        }
        if (mouseNote >= 0 && s > 0) { proc.tieRequests.fetch_add (s); flash ("Long note"); return; }
    }
    if (mode == mFx)
    {
        const int to = jlimit (0, 5, fxSlot + s);
        if (fxGrab && to != fxSlot)
        {
            auto o = proc.getFxOrder();
            std::swap (o[(size_t) fxSlot], o[(size_t) to]);
            proc.setFxOrder (o);
        }
        fxSlot = to;
        flash (pageInfo().name.fromFirstOccurrenceOf (" ", false, false) + (fxGrab ? "  (moving)" : ""));
        return;
    }
    const auto pi = pageInfo();
    if (pi.numPages > 1)
    {
        pageRef() = jlimit (0, pi.numPages - 1, pageRef() + s);
        flash (String (pageRef() + 1) + "/" + String (pi.numPages) + "  " + pi.name);
    }
}

void FM9Editor::fnPress (int idx)
{
    const bool seqActive = getParam ("seqActive") > 0.5f, arpOn = getParam ("arpOn") > 0.5f;
    switch (idx)
    {
        case 0: goMode (mode == mFx ? mHome : mFx); break;                                  // FX
        case 1:                                                                               // SEL : saisir / poser l'effet
            if (mode == mFx) { fxGrab = ! fxGrab; flash (fxGrab ? "Turn SELECT to move" : "Effect placed"); }
            break;
        case 2:                                                                               // ENV : ADSR marche / arret
            if (getParam ("adsrOn") > 0.5f) { setParam ("adsrOn", 0.0f); goMode (mHome); popup ("Envelope", "Off"); }
            else { setParam ("adsrOn", 1.0f); goMode (mEnv); }
            break;
        case 3: goMode (mode == mLfo ? mHome : mLfo); break;
        case 4:                                                                               // EDIT
            editMode = ! editMode;
            goMode (editMode ? mOp : mHome);
            if (editMode) editMode = true;
            break;
        case 5: goMode (mode == mGlobal ? mHome : mGlobal); break;                          // GLO (reglages globaux)
        case 6: goMode (mHome); break;                                                        // HOME
        case 7: break;                                                                        // SAVE : appui long (fnRelease / timer)
        case 8:                                                                               // ARP
            if (seqActive) { popup ("ARP", "Disable SEQ to enter"); break; }
            if (arpOn && mode == mArp) { setParam ("arpOn", 0.0f); goMode (mHome); }
            else { setParam ("arpOn", 1.0f); goMode (mArp); }
            break;
        case 9:                                                                               // SEQ
            if (arpOn) { popup ("SEQ", "Disable ARP to enter"); break; }
            if (seqActive && mode == mSeq) { setParam ("seqRec", 0.0f); setParam ("seqOn", 0.0f); setParam ("seqActive", 0.0f); goMode (mHome); }
            else { setParam ("seqActive", 1.0f); goMode (mSeq); }
            break;
        case 10:                                                                              // PLAY/STOP
            if (! seqActive) { popup ("SEQ", "Enable SEQ first"); break; }
            if (getParam ("seqOn") > 0.5f || getParam ("seqRec") > 0.5f) { setParam ("seqOn", 0.0f); setParam ("seqRec", 0.0f); }
            else setParam ("seqOn", 1.0f);
            break;
        case 11:                                                                              // REC : bascule au relachement
            if (! seqActive) { popup ("SEQ", "Enable SEQ first"); break; }
            if (mode != mSeq) goMode (mSeq);
            break;
        default: break;
    }
}

void FM9Editor::fnRelease (int idx, bool longPress)
{
    if (idx == 7 && ! holdUsed) flash ("Hold SAVE to save");
    if (idx == 11 && getParam ("seqActive") > 0.5f && ! holdUsed)
    {
        const bool rec = getParam ("seqRec") < 0.5f;
        setParam ("seqRec", rec ? 1.0f : 0.0f);
        flash (rec ? "Recording: play notes" : "Recording stopped");
    }
    ignoreUnused (longPress);
}

void FM9Editor::renameVoice()
{
    auto* w = new AlertWindow ("Rename", "Voice name (10 characters)", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.getVoiceName());
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([safe = Component::SafePointer<FM9Editor> (this), w] (int r)
    {
        if (safe != nullptr && r == 1) safe->proc.setVoiceName (w->getTextEditorContents ("name"));
    }), true);
}

// --------------------------------------------------------------------------------------------- clavier et souris
int FM9Editor::noteForKey (bool black, int idx) const
{
    return jlimit (0, 127, baseNote + 12 * octave + kbTrans + (black ? blackOffs[idx] : whiteOffs[idx]));
}

int FM9Editor::keyAt (Point<float> u, bool& black) const
{
    for (int i = 0; i < 11; ++i) if (L::blackKeys[i].contains (u)) { black = true; return i; }
    for (int i = 0; i < 16; ++i) if (L::whiteKeys[i].contains (u)) { black = false; return i; }
    return -1;
}

void FM9Editor::releaseMouseNote()
{
    if (mouseNote >= 0) proc.keyboardState.noteOff (1, mouseNote, 0.0f);
    mouseNote = -1;
}

void FM9Editor::mouseDown (const MouseEvent& e)
{
    const auto u = toUnits (e.position);
    if (drumOpen)
    {
        const auto fr = L::fnKeys[7];
        const auto drum = Rectangle<float> (fr.getCentreX() - 110, fr.getCentreY() - 150, 220, 300);
        if (drum.contains (u))
        {
            const int rowOff = (int) std::floor ((u.y - drum.getCentreY() + 20.0f) / 40.0f);
            if (rowOff == 0) closeDrum (true); else drumIndex = jlimit (0, proc.getNumPrograms() - 1, drumIndex + rowOff);
        }
        else closeDrum (false);
        repaint();
        return;
    }
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    heldSince = Time::getMillisecondCounterHiRes();
    holdUsed = false;

    if (auto* fw = proc.firmware())              // firmware de la FM-1 : la facade lui appartient
    {
        for (int i = 0; i < 12; ++i) if (L::fnKeys[i].contains (u)) { heldFn = i; fw->button (i, true); repaint(); return; }
        for (int i = 0; i < 2; ++i) if (L::octKeys[i].contains (u)) { heldOct = i; fw->button (12 + i, true); repaint(); return; }
        bool black = false;
        const int k = keyAt (u, black);
        if (k >= 0) { fwHeldKey = black ? blackOffs[k] : whiteOffs[k]; fw->key (fwHeldKey, true); }
        return;
    }

    for (int i = 0; i < 12; ++i)
        if (L::fnKeys[i].contains (u)) { heldFn = i; fnPress (i); repaint(); return; }
    for (int i = 0; i < 2; ++i)
        if (L::octKeys[i].contains (u))
        {
            if (e.mods.isShiftDown() || e.mods.isAltDown())          // OCT- + OCT+ ensemble : remise a zero
            {
                octave = 0; kbTrans = 0; holdUsed = true;
                proc.uiState.setProperty ("octave", 0, nullptr); proc.uiState.setProperty ("transpose", 0, nullptr);
                popup ("Octave", "0  Transpose 0");
            }
            heldOct = i; octDragAcc = 0;
            repaint();
            return;
        }
    bool black = false;
    const int k = keyAt (u, black);
    if (k >= 0)
    {
        if (editMode && black && k < 10) { heldBlack = k; return; }     // selection au relachement (appui long = mute)
        const float vel = (float) getParam ("kbdVel") / 127.0f;
        mouseNote = noteForKey (black, k);
        proc.keyboardState.noteOn (1, mouseNote, vel);
        return;
    }
    if (L::screen.contains (u)) selectTurn (1);                     // clic sur l'ecran : page suivante
}

void FM9Editor::mouseDrag (const MouseEvent& e)
{
    if (auto* fw = proc.firmware())
    {
        if (fwHeldKey < 0) return;
        bool black = false;
        const int k = keyAt (toUnits (e.position), black);
        if (k < 0) return;
        const int n = black ? blackOffs[k] : whiteOffs[k];
        if (n != fwHeldKey) { fw->key (fwHeldKey, false); fwHeldKey = n; fw->key (n, true); }
        return;
    }
    if (heldOct >= 0)
    {
        // OCT tenu + rotation (ici : glisser vertical ou molette) : transposition 0..-12 / 0..+12
        octDragAcc += (float) -e.getDistanceFromDragStartY() / 14.0f - octDragAcc;
        const int steps = (int) octDragAcc;
        if (steps != 0)
        {
            const int t = heldOct == 0 ? jlimit (-12, 0, -std::abs (steps)) : jlimit (0, 12, std::abs (steps));
            if (t != kbTrans || ! holdUsed) { kbTrans = t; holdUsed = true; proc.uiState.setProperty ("transpose", kbTrans, nullptr); popup ("Transpose", (kbTrans > 0 ? "+" : "") + String (kbTrans)); }
        }
        return;
    }
    if (mouseNote < 0) return;
    bool black = false;
    const int k = keyAt (toUnits (e.position), black);
    if (k < 0 || (editMode && black)) return;
    const int n = noteForKey (black, k);
    if (n != mouseNote) { releaseMouseNote(); mouseNote = n; proc.keyboardState.noteOn (1, n, (float) getParam ("kbdVel") / 127.0f); }
}

void FM9Editor::mouseUp (const MouseEvent&)
{
    if (auto* fw = proc.firmware())
    {
        if (heldFn >= 0) fw->button (heldFn, false);
        if (heldOct >= 0) fw->button (12 + heldOct, false);
        if (fwHeldKey >= 0) fw->key (fwHeldKey, false);
        heldFn = heldOct = fwHeldKey = -1;
        repaint();
        return;
    }
    const bool longPress = Time::getMillisecondCounterHiRes() - heldSince > 600.0;
    releaseMouseNote();
    if (heldFn >= 0) { fnRelease (heldFn, longPress); heldFn = -1; }
    if (heldOct >= 0)
    {
        if (! holdUsed)
        {
            octave = jlimit (-3, 3, octave + (heldOct == 0 ? -1 : 1));
            proc.uiState.setProperty ("octave", octave, nullptr);
            popup ("Octave", (octave > 0 ? "+" : "") + String (octave));
        }
        heldOct = -1;
    }
    if (heldBlack >= 0)
    {
        const int k = heldBlack; heldBlack = -1;
        if (k < 6)
        {
            if (longPress && ! holdUsed)                       // appui long : mute de l'operateur
            {
                const String id = "op" + String (k + 1) + "_on";
                setParam (id, getParam (id) > 0.5f ? 0.0f : 1.0f);
                popup ("OP" + String (k + 1), getParam (id) > 0.5f ? "On" : "Muted");
            }
            else { op = k + 1; goMode (mOp); editMode = true; }
        }
        else if (k == 6) { goMode (mPitch); editMode = true; }
        else if (k == 7) { goMode (mVoice); editMode = true; }
        else { setParam ("mono", k == 8 ? 1.0f : 0.0f); popup (k == 8 ? "Mono" : "Poly", k == 8 ? "1 voice" : "12 voices"); }
    }
    repaint();
}

void FM9Editor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (drumOpen)
    {
        drumScroll += w.deltaY * 6.0f;
        while (drumScroll >= 1.0f) { drumIndex = jmax (0, drumIndex - 1); drumScroll -= 1.0f; }
        while (drumScroll <= -1.0f) { drumIndex = jmin (proc.getNumPrograms() - 1, drumIndex + 1); drumScroll += 1.0f; }
        repaint();
        return;
    }
    if (L::screen.contains (toUnits (e.position)))                  // molette sur l'ecran = SELECT
    {
        static float acc = 0; acc += w.deltaY * 8.0f;
        while (acc >= 1.0f) { selectTurn (1); acc -= 1.0f; }
        while (acc <= -1.0f) { selectTurn (-1); acc += 1.0f; }
    }
}

bool FM9Editor::keyPressed (const KeyPress& k)
{
    if (! drumOpen) return false;
    if (k == KeyPress::returnKey) { closeDrum (true); return true; }
    if (k == KeyPress::escapeKey) { closeDrum (false); return true; }
    if (k == KeyPress::upKey) { drumIndex = jmax (0, drumIndex - 1); repaint(); return true; }
    if (k == KeyPress::downKey) { drumIndex = jmin (proc.getNumPrograms() - 1, drumIndex + 1); repaint(); return true; }
    return false;
}

void FM9Editor::openSaveDrum() { drumOpen = true; drumIndex = proc.getCurrentProgram(); drumScroll = 0; grabKeyboardFocus(); repaint(); }

void FM9Editor::closeDrum (bool accept)
{
    drumOpen = false;
    if (accept) { proc.storeCurrentVoice (drumIndex); popup ("Preset Saved", FM9Processor::programLabel (drumIndex)); }
    repaint();
}

// --------------------------------------------------------------------------------------------- timer
void FM9Editor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    if (getPeer() != lastPeer || now - lastRendererCheckMs > 1000.0) { lastRendererCheckMs = now; useSoftwareRenderer(); }
    if (resizeSettle > 0 && --resizeSettle == 0) rebuildCaches();
    else if (faceDirty && resizeSettle == 0) recomposeFace();      // couleur changee a la volee
    else if (resizeSettle == 0 && now - lastScaleCheckMs > 250.0)
    {
        lastScaleCheckMs = now;
        if (std::abs (physicalScale() - cacheScale) > 0.01f) rebuildCaches();
    }
    if (repaintAskedMs > 0.0 && now - repaintAskedMs > 1500.0 && isShowing())     // chien de garde
    {
        repaintAskedMs = 0.0; lastPeer = nullptr; useSoftwareRenderer(); repaint();
    }

    if (auto* fw = proc.firmware())
    {
        if (fw->fbSerial.load() != fwDrawnSerial) { fwDrawnSerial = fw->fbSerial.load(); fw->copyScreen (fwImage); askRepaint (toPx (L::bezel)); }
        String ps; ps << (int64) fw->litButtons.load() << ',' << (int64) fw->litKeys.load() << ',' << (int64) fw->dimKeys.load() << ',' << heldFn << heldOct << fwHeldKey
                      << (int) proc.uiState.getProperty ("glow", 75);
        if (ps != drawnPanelSig) { drawnPanelSig = ps; repaint(); }
        return;
    }
    fwImage = {};

    // SAVE tenu : sauvegarde du preset dans son emplacement
    if (heldFn == 7 && ! holdUsed && now - heldSince > 600.0)
    {
        holdUsed = true;
        proc.storeCurrentVoice (proc.getCurrentProgram());
        popup ("Preset Saved", FM9Processor::programLabel (proc.getCurrentProgram()));
    }

    // ecran : redessine si son contenu change (et en continu pour l'oscilloscope)
    String sig;
    const auto pi = pageInfo();
    sig << (int) mode << '|' << pageRef() << '|' << op << '|' << fxSlot << (fxGrab ? "g" : "") << '|' << (now < flashUntil ? flashText : String())
        << '|' << (now < popUntil ? popTitle + popValue + String (popKind) : String()) << '|' << proc.getCurrentProgram() << proc.getVoiceName() << (proc.isEdited() ? "*" : "")
        << '|' << proc.seq.playhead.load() << '|' << proc.recStep.load() << '|' << (proc.isLearning() ? 1 : 0) << '|' << (int) blinkOn (500);
    for (auto& s : pi.s) sig << '|' << slotValue (s);
    for (auto id : { "alg", "fb", "seqRec", "seqOn", "seqPattern", "seqLen", "adsrOn" }) sig << '|' << getParam (id);
    if (mode == mSeq) { const int pat = roundToInt (getParam ("seqPattern")) - 1; for (int i = 0; i < 16; ++i) sig << ',' << (int64) proc.seqData.data[pat][i].load(); }
    if (mode == mFx) { for (int f = 0; f < 6; ++f) sig << ',' << getParam (fm9::fxId (f, "on")); for (int o : proc.getFxOrder()) sig << o; }
    if (mode == mHome && proc.outLevel.load() > 1.0e-4f) sig << '|' << proc.scopePos.load();
    if (sig != drawnScreenSig) { drawnScreenSig = sig; askRepaint (toPx (L::bezel)); }

    // LED des touches (notes, mode EDIT, pas du sequenceur, octave), touches de fonction
    String ps;
    for (auto& w : proc.keyBits) ps << (int64) w.load() << ',';
    ps << (int) mode << (int) editMode << op << heldFn << heldBlack << heldOct << octave << (int) blinkOn (900) << (int) blinkOn (300) << (int) blinkOn (250)
       << getParam ("arpOn") << getParam ("seqActive") << getParam ("seqOn") << getParam ("seqRec") << getParam ("adsrOn") << getParam ("mono") << (int) fxGrab
       << (int) proc.uiState.getProperty ("glow", 75) << '|' << mouseNote;
    if (getParam ("seqActive") > 0.5f) { ps << proc.seq.playhead.load(); const int pat = roundToInt (getParam ("seqPattern")) - 1; for (int i = 0; i < 16; ++i) ps << (int) (proc.seqData.get (pat, i).count > 0); }
    for (int o = 1; o <= 6; ++o) ps << getParam ("op" + String (o) + "_on");
    if (ps != drawnPanelSig)
    {
        drawnPanelSig = ps;
        askRepaint (toPx (Rectangle<float> (L::whiteKeys[0].getX() - 10, L::blackKeys[0].getY() - 10, L::whiteKeys[15].getRight() - L::whiteKeys[0].getX() + 20, L::whiteKeys[0].getBottom() - L::blackKeys[0].getY() + 20)));
        askRepaint (toPx (L::fnKeys[0].getUnion (L::fnKeys[11]).expanded (10)));
        askRepaint (toPx (L::octKeys[0].getUnion (L::octKeys[1]).expanded (10)));
    }
}

// --------------------------------------------------------------------------------------------- dessin
void FM9Editor::drawEmbossed (Graphics& g, const String& t, Rectangle<float> r, float h, bool lit, Colour litCol)
{
    if (t.isEmpty()) return;
    g.setFont (fontOf (bold, h).withExtraKerningFactor (0.04f));
    if (! lit)
    {
        // gravure en relief dans le silicone : arete claire en haut a gauche, ombre en bas a droite
        // occlusion douce tout autour des lettres
        g.setColour (Colours::black.withAlpha (0.10f));
        for (auto d : { Point<float> (-0.7f, 0), Point<float> (0.7f, 0), Point<float> (0, -0.7f), Point<float> (0, 0.7f),
                        Point<float> (-0.5f, -0.5f), Point<float> (0.5f, 0.5f), Point<float> (-0.5f, 0.5f), Point<float> (0.5f, -0.5f) })
            g.drawText (t, r.translated (d.x, d.y), Justification::centred, false);
        // ombre portee a 45 deg vers le bas a droite (lumiere en haut a gauche), qui s'adoucit en s'eloignant
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (Colours::black.withAlpha (0.30f / (float) i));
            g.drawText (t, r.translated (0.55f * (float) i, 0.55f * (float) i), Justification::centred, false);
        }
        // couleurs tirees du silicone du skin : plus claires sur un silicone sombre, plus sombres sur un silicone clair
        const bool lightKeys = keyCol.getPerceivedBrightness() > 0.5f;
        g.setColour ((lightKeys ? Colours::white : keyCol.interpolatedWith (Colours::white, 0.40f)).withAlpha (0.45f));
        g.drawText (t, r.translated (-0.5f, -0.5f), Justification::centred, false);
        // silicone colore (violet, brique, bleu) : gravure claire bien lisible comme sur la FM-1 ; silicone noir : gris discret
        const bool coloured = keyCol.getSaturation() > 0.2f;
        g.setColour (lightKeys ? keyCol.interpolatedWith (Colours::black, 0.38f) : keyCol.interpolatedWith (Colours::white, coloured ? 0.74f : 0.17f));
        g.drawText (t, r, Justification::centred, false);
        return;
    }
    // gravure retroeclairee par la LED de la touche
    const float glow = jlimit (0.25f, 1.0f, (float) (int) proc.uiState.getProperty ("glow", 75) / 100.0f);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (litCol.withAlpha (0.10f * glow));
        for (auto d : { Point<float> (-0.6f * (float) i, 0), Point<float> (0.6f * (float) i, 0), Point<float> (0, -0.6f * (float) i), Point<float> (0, 0.6f * (float) i) })
            g.drawText (t, r.translated (d.x, d.y), Justification::centred, false);
    }
    g.setColour (litCol.interpolatedWith (Colours::white, 0.55f));
    g.drawText (t, r, Justification::centred, false);
}

void FM9Editor::drawKeyLed (Graphics& g, Rectangle<float> r, float level, Colour c)
{
    // la LED eclaire la rainure de la touche et diffuse dans le silicone
    if (level <= 0.0f) return;
    const float glow = jlimit (0.25f, 1.0f, (float) (int) proc.uiState.getProperty ("glow", 75) / 100.0f) * level;
    const auto ctr = r.getCentre();
    const bool tall = r.getHeight() > r.getWidth() * 1.3f;
    const auto slot = tall ? Rectangle<float> (3.6f, r.getHeight() * 0.40f).withCentre ({ ctr.x, r.getY() + r.getHeight() * (r.getY() < L::whiteKeys[0].getY() ? 0.35f : 0.5f) })
                           : Rectangle<float> (r.getWidth() * 0.5f, 3.0f).withCentre (ctr);
    ColourGradient grad (c.withAlpha (0.30f * glow), slot.getCentreX(), slot.getCentreY(), c.withAlpha (0.0f), slot.getCentreX() + r.getWidth() * 0.62f, slot.getCentreY(), true);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.reduced (2.0f), jmin (r.getWidth(), r.getHeight()) * 0.45f);
    g.setColour (c.withAlpha (0.85f * glow));
    g.fillRoundedRectangle (slot, 1.8f);
}

void FM9Editor::drawPrint (Graphics& g)
{
    // inscriptions sur la coque (Poppins, vectoriel)
    g.setColour (inkCol);
    g.setFont (fontOf (med, 22.0f).withExtraKerningFactor (0.08f));
    // texte centre au-dessus de chaque potard, meme distance pour tous (bas du texte a labelGap de la couronne)
    static constexpr float labelGap = 9.0f, labelH = 22.0f;
    auto label = [&g] (const String& t, Point<float> knob)
    {
        const float bottom = knob.y - L::knobR - labelGap;
        g.drawText (t, Rectangle<float> (knob.x - 100, bottom - labelH, 200, labelH), Justification::centredBottom, false);
    };
    label ("MASTER", L::knob_master);
    label ("SELECT", L::knob_select);
    label ("PRESETS", L::knob_presets);
    label ("ALGORITHM", L::knob_algorithm);
    label ("KNOB1", L::knob_knob1);
    label ("KNOB2", L::knob_knob2);
    label ("KNOB3", L::knob_knob3);
    label ("KNOB4", L::knob_knob4);

    // logos aux emplacements du mockup : NINE HANDS | DSP en bas au centre, FM-9 en haut a droite
    if (logo != nullptr)
        logo->drawWithin (g, L::logoNH, RectanglePlacement::centred, 0.9f);
    if (fm9Logo != nullptr)
        fm9Logo->drawWithin (g, L::logoFM9, RectanglePlacement::centred, 0.88f);

    // gravures non allumees des touches
    for (int i = 0; i < 12; ++i)
    {
        const auto r = L::fnKeys[i];
        if (i == 10)
        {
            drawEmbossed (g, "PLAY", r.withHeight (r.getHeight() * 0.5f).translated (0, r.getHeight() * 0.10f), 15.0f, false, {});
            drawEmbossed (g, "STOP", r.withTrimmedTop (r.getHeight() * 0.5f).translated (0, -r.getHeight() * 0.06f), 15.0f, false, {});
            g.setColour ((keyCol.getPerceivedBrightness() > 0.5f ? keyCol.interpolatedWith (Colours::black, 0.38f) : keyCol.interpolatedWith (Colours::white, keyCol.getSaturation() > 0.2f ? 0.74f : 0.17f))); g.fillRect (r.getCentreX() - 16.0f, r.getCentreY() + 1.0f, 32.0f, 1.2f);
        }
        else drawEmbossed (g, fnNames[i], r, 17.0f, false, {});
    }
    drawEmbossed (g, "OCT-", L::octKeys[0], 17.0f, false, {});
    drawEmbossed (g, "OCT+", L::octKeys[1], 17.0f, false, {});
    for (int i = 0; i < 11; ++i)
    {
        const auto r = L::blackKeys[i];
        drawEmbossed (g, blackNames[i], Rectangle<float> (r.getX() - 10, r.getY() + r.getHeight() * 0.62f, r.getWidth() + 20, 22), 15.0f, false, {});
    }
}

void FM9Editor::drawDynamic (Graphics& g)
{
    if (auto* fw = proc.firmware())
    {
        const uint32_t lb = fw->litButtons.load(), lk = fw->litKeys.load(), dk = fw->dimKeys.load();
        for (int i = 0; i < 16; ++i)
        {
            const int n = whiteOffs[i];
            drawKeyLed (g, L::whiteKeys[i], (lk >> n) & 1u ? 1.0f : ((dk >> n) & 1u ? 0.35f : 0.0f), ledCol);
            if (fwHeldKey == n) { g.setColour (Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (L::whiteKeys[i].reduced (2), 30); }
        }
        for (int i = 0; i < 11; ++i)
        {
            const int n = blackOffs[i];
            drawKeyLed (g, L::blackKeys[i], (lk >> n) & 1u ? 1.0f : ((dk >> n) & 1u ? 0.35f : 0.0f), ledCol);
            if (fwHeldKey == n) { g.setColour (Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (L::blackKeys[i].reduced (2), 30); }
        }
        for (int i = 0; i < 12; ++i)
        {
            const auto r = L::fnKeys[i];
            if (heldFn == i) { g.setColour (Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (r.reduced (3), 14); }
            if (! ((lb >> i) & 1u)) continue;
            if (i == 10)
            {
                drawEmbossed (g, "PLAY", r.withHeight (r.getHeight() * 0.5f).translated (0, r.getHeight() * 0.10f), 15.0f, true, Colour (0xffdfe6ea));
                drawEmbossed (g, "STOP", r.withTrimmedTop (r.getHeight() * 0.5f).translated (0, -r.getHeight() * 0.06f), 15.0f, true, Colour (0xffdfe6ea));
            }
            else drawEmbossed (g, fnNames[i], r, 17.0f, true, Colour (0xffdfe6ea));
        }
        for (int i = 0; i < 2; ++i)
        {
            if (heldOct == i) { g.setColour (Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (L::octKeys[i].reduced (2), 8); }
            if ((lb >> (12 + i)) & 1u) drawEmbossed (g, i == 0 ? "OCT-" : "OCT+", L::octKeys[i], 17.0f, true, Colour (0xffdfe6ea));
        }
        return;
    }
    auto noteOn = [this] (int n) { return n >= 0 && n < 128 && (proc.keyBits[(size_t) (n >> 5)].load() >> (n & 31) & 1u) != 0; };
    const bool seqActive = getParam ("seqActive") > 0.5f;
    const int pat = roundToInt (getParam ("seqPattern")) - 1, ph = proc.seq.playhead.load(), len = roundToInt (getParam ("seqLen"));
    const bool rec = getParam ("seqRec") > 0.5f;

    // touches blanches : notes jouees ; en SEQ, LED des 16 pas (allumee = note, clignote = pas joue)
    for (int i = 0; i < 16; ++i)
    {
        float lv = noteOn (noteForKey (false, i)) ? 1.0f : 0.0f;
        Colour c = ledCol;
        if (seqActive && i < len)
        {
            const bool filled = proc.seqData.get (pat, i).count > 0 || proc.seqData.get (pat, i).tie;
            if (filled) lv = jmax (lv, 0.7f);
            if (i == ph && getParam ("seqOn") > 0.5f) lv = blinkOn (250) ? 1.0f : 0.15f;
            if (rec && i == proc.recStep.load()) { lv = jmax (lv, blinkOn (400) ? 1.0f : 0.3f); c = Colour (0xffff8f87); }
        }
        drawKeyLed (g, L::whiteKeys[i], lv, c);
        if (mouseNote >= 0 && noteForKey (false, i) == mouseNote) { g.setColour (Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (L::whiteKeys[i].reduced (2), 30); }
    }
    // touches noires : notes jouees, ou selecteurs du mode EDIT (clignotent ; selection allumee fixe)
    for (int i = 0; i < 11; ++i)
    {
        const auto r = L::blackKeys[i];
        if (editMode && i < 10)
        {
            bool sel = (i < 6 && mode == mOp && op == i + 1) || (i == 6 && mode == mPitch) || (i == 7 && mode == mVoice)
                    || (i == 8 && getParam ("mono") > 0.5f) || (i == 9 && getParam ("mono") < 0.5f);
            const bool muted = i < 6 && getParam ("op" + String (i + 1) + "_on") < 0.5f;
            const float lv = sel ? 1.0f : (blinkOn (900) ? 0.55f : 0.0f);
            drawKeyLed (g, r, muted ? (blinkOn (300) ? 0.6f : 0.0f) : lv, muted ? cRed : ledCol);
            drawEmbossed (g, blackNames[i], Rectangle<float> (r.getX() - 10, r.getY() + r.getHeight() * 0.62f, r.getWidth() + 20, 22), 15.0f, lv > 0.3f, muted ? cRed : Colour (0xffdfe6ea));
            if (heldBlack == i) { g.setColour (Colours::black.withAlpha (0.14f)); g.fillRoundedRectangle (r.reduced (2), 30); }
            continue;
        }
        drawKeyLed (g, r, noteOn (noteForKey (true, i)) ? 1.0f : 0.0f, ledCol);
        if (mouseNote >= 0 && noteForKey (true, i) == mouseNote) { g.setColour (Colours::black.withAlpha (0.12f)); g.fillRoundedRectangle (r.reduced (2), 30); }
    }

    // touches de fonction : LED allumee pour le mode actif et les etats (couleurs du manuel)
    const Mode fnMode[12] = { mFx, mCount, mEnv, mLfo, mCount, mGlobal, mHome, mCount, mArp, mSeq, mCount, mCount };
    for (int i = 0; i < 12; ++i)
    {
        const auto r = L::fnKeys[i];
        if (heldFn == i) { g.setColour (Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (r.reduced (3), 14); }
        bool lit = fnMode[i] == mode && i != 6;
        Colour col = Colour (0xffdfe6ea);
        if (i == 1) lit = fxGrab;
        if (i == 2) lit = getParam ("adsrOn") > 0.5f;
        if (i == 4) lit = editMode;
        if (i == 6) lit = mode == mHome;
        if (i == 7) lit = heldFn == 7 && holdUsed;
        if (i == 8) lit = getParam ("arpOn") > 0.5f;
        if (i == 9) lit = seqActive;
        if (i == 10 && seqActive)                                   // PLAY vert, STOP orange
        {
            const bool play = getParam ("seqOn") > 0.5f && ! rec;
            drawEmbossed (g, "PLAY", r.withHeight (r.getHeight() * 0.5f).translated (0, r.getHeight() * 0.10f), 15.0f, play, cGreen);
            drawEmbossed (g, "STOP", r.withTrimmedTop (r.getHeight() * 0.5f).translated (0, -r.getHeight() * 0.06f), 15.0f, ! play, cOrange);
            continue;
        }
        if (i == 11) { lit = rec; col = cRed; }
        if (lit) drawEmbossed (g, fnNames[i], r, 17.0f, true, col);
    }
    // OCT : LED du cote du decalage ; +-1 clignote lentement, +-2 vite, +-3 fixe
    for (int i = 0; i < 2; ++i)
    {
        if (heldOct == i) { g.setColour (Colours::black.withAlpha (0.18f)); g.fillRoundedRectangle (L::octKeys[i].reduced (2), 8); }
        const int a = std::abs (octave);
        const bool side = (i == 0 && octave < 0) || (i == 1 && octave > 0);
        const bool on = side && (a >= 3 || (a == 2 && blinkOn (300)) || (a == 1 && blinkOn (900)));
        if (on) drawEmbossed (g, i == 0 ? "OCT-" : "OCT+", L::octKeys[i], 17.0f, true, Colour (0xffdfe6ea));
    }
}

// schema de l'algorithme, deduit des bus de la table msfa : porteuses en bas, modulateurs au dessus
void FM9Editor::drawAlgorithm (Graphics& g, Rectangle<float> area, int alg, float u)
{
    const auto& A = FmCore::getAlgorithm (alg);
    int mods[6][6] {}; int nm[6] {};
    bool carrier[6] {}; int fbOp = -1;
    int bus1[6], n1 = 0, bus2[6], n2 = 0;
    for (int i = 0; i < 6; ++i)                     // i = 0 : OP6 ... i = 5 : OP1
    {
        const int f = A.ops[i];
        const int opn = 6 - i;
        if (f & IN_BUS_ONE) for (int k = 0; k < n1; ++k) mods[opn - 1][nm[opn - 1]++] = bus1[k];
        if (f & IN_BUS_TWO) for (int k = 0; k < n2; ++k) mods[opn - 1][nm[opn - 1]++] = bus2[k];
        if (f & FB_IN) fbOp = opn;
        if (f & OUT_BUS_ONE) { if (! (f & OUT_BUS_ADD)) n1 = 0; bus1[n1++] = opn; }
        else if (f & OUT_BUS_TWO) { if (! (f & OUT_BUS_ADD)) n2 = 0; bus2[n2++] = opn; }
        else carrier[opn - 1] = true;
    }
    // niveaux : porteuse = 0, modulateur = 1 + niveau de sa cible
    int level[6]; for (auto& l : level) l = -1;
    int target[6]; for (auto& t : target) t = -1;
    for (int o = 0; o < 6; ++o) if (carrier[o]) level[o] = 0;
    for (int pass = 0; pass < 6; ++pass)
        for (int o = 0; o < 6; ++o)
            if (level[o] >= 0)
                for (int k = 0; k < nm[o]; ++k)
                {
                    const int m = mods[o][k] - 1;
                    if (level[m] < 0 || level[m] > level[o] + 1) { level[m] = level[o] + 1; target[m] = o; }
                }
    int maxL = 0; for (int l : level) maxL = jmax (maxL, l);
    // positions : porteuses de gauche a droite ; un modulateur se place au dessus de sa cible
    float xs[6] {};
    int nc = 0; for (int o = 0; o < 6; ++o) if (carrier[o]) xs[o] = (float) nc++;
    for (int l = 1; l <= maxL; ++l)
    {
        int ids[6], n = 0;
        for (int o = 0; o < 6; ++o) if (level[o] == l) { xs[o] = xs[target[o]]; ids[n++] = o; }
        // modulateurs de la meme cible : etales autour d'elle
        for (int a = 0; a < n; ++a)
        {
            int same[6], ns = 0;
            for (int b = 0; b < n; ++b) if (target[ids[b]] == target[ids[a]]) same[ns++] = ids[b];
            if (ns > 1) for (int s = 0; s < ns; ++s) xs[same[s]] = xs[target[same[s]]] + ((float) s - (ns - 1) * 0.5f);
        }
        // pas de chevauchement sur une ligne
        for (int it = 0; it < 6; ++it)
            for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b)
                if (a != b && std::abs (xs[ids[a]] - xs[ids[b]]) < 0.99f)
                {
                    const int lo = xs[ids[a]] <= xs[ids[b]] ? ids[a] : ids[b], hi = lo == ids[a] ? ids[b] : ids[a];
                    const float mid = (xs[lo] + xs[hi]) * 0.5f;
                    xs[lo] = mid - 0.5f; xs[hi] = mid + 0.5f;
                }
    }
    float minX = 1e9f, maxX = -1e9f; for (float x : xs) { minX = jmin (minX, x); maxX = jmax (maxX, x); }
    const float cols = jmax (1.0f, maxX - minX + 1.0f), rows = (float) maxL + 1.0f;
    const float cell = jmin (area.getWidth() / cols, area.getHeight() / (rows + 0.6f));
    const float bs = cell * 0.62f;
    const float ox = area.getCentreX() - (cols - 1) * cell * 0.5f - minX * cell;
    const float oy = area.getBottom() - cell * 1.0f;
    auto centre = [&] (int o) { return Point<float> (ox + xs[o] * cell, oy - (float) level[o] * cell); };

    g.setColour (cDim);
    for (int o = 0; o < 6; ++o)
        for (int k = 0; k < nm[o]; ++k)
        {
            const auto a = centre (mods[o][k] - 1), b = centre (o);
            Path p; p.startNewSubPath (a.x, a.y + bs * 0.5f); p.lineTo (a.x, (a.y + b.y) * 0.5f); p.lineTo (b.x, (a.y + b.y) * 0.5f); p.lineTo (b.x, b.y - bs * 0.5f);
            g.strokePath (p, PathStrokeType (0.45f * u));
        }
    // sortie des porteuses
    float cx0 = 1e9f, cx1 = -1e9f;
    for (int o = 0; o < 6; ++o) if (carrier[o]) { const auto c = centre (o); cx0 = jmin (cx0, c.x); cx1 = jmax (cx1, c.x); g.drawLine (c.x, c.y + bs * 0.5f, c.x, oy + cell * 0.62f, 0.45f * u); }
    g.drawLine (cx0, oy + cell * 0.62f, cx1, oy + cell * 0.62f, 0.45f * u);
    // boucle de feedback
    if (fbOp > 0)
    {
        const auto c = centre (fbOp - 1);
        Path p; p.startNewSubPath (c.x, c.y - bs * 0.5f); p.lineTo (c.x, c.y - bs * 0.75f); p.lineTo (c.x + bs * 0.78f, c.y - bs * 0.75f);
        p.lineTo (c.x + bs * 0.78f, c.y + bs * 0.68f); p.lineTo (c.x, c.y + bs * 0.68f);
        g.setColour (cYellow); g.strokePath (p, PathStrokeType (0.45f * u));
    }
    for (int o = 0; o < 6; ++o)
    {
        const auto c = centre (o);
        const auto box = Rectangle<float> (bs, bs).withCentre (c);
        const bool on = getParam ("op" + String (o + 1) + "_on") > 0.5f;
        const Colour col = ! on ? cFaint : carrier[o] ? cAccent : Colour (0xffc7d2da);
        g.setColour (cScreen); g.fillRoundedRectangle (box, bs * 0.18f);
        g.setColour (col.withAlpha (carrier[o] ? 0.25f : 0.10f)); g.fillRoundedRectangle (box, bs * 0.18f);
        g.setColour (col); g.drawRoundedRectangle (box, bs * 0.18f, 0.45f * u);
        g.setFont (fontOf (bold, bs * 0.62f)); g.drawText (String (o + 1), box, Justification::centred, false);
    }
}

void FM9Editor::drawScreen (Graphics& g)
{
    const auto sr = L::screen;
    if (proc.firmware() != nullptr)
    {
        // ecran 240 x 240 du firmware, mis a l'echelle de la vitre
        g.saveState();
        { Path clip; clip.addRoundedRectangle (sr, 7.5f); g.reduceClipRegion (clip); }
        g.setColour (Colours::black); g.fillRect (sr);
        if (fwImage.isValid())
        {
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (fwImage, sr.reduced (sr.getWidth() * 0.012f));
        }
        g.restoreState();
        ColourGradient refl (Colours::white.withAlpha (0.055f), sr.getX(), sr.getY(), Colours::white.withAlpha (0.0f), sr.getX() + sr.getWidth() * 0.55f, sr.getY() + sr.getHeight() * 0.55f, false);
        g.setGradientFill (refl); g.fillRoundedRectangle (sr, 7.5f);
        return;
    }
    const double now = Time::getMillisecondCounterHiRes();
    g.saveState();
    { Path clip; clip.addRoundedRectangle (sr, 7.5f); g.reduceClipRegion (clip); }    // vitre aux coins arrondis
    g.setColour (cScreen);
    g.fillRect (sr);
    const float u = sr.getWidth() / 100.0f;
    auto txt = [&] (const String& t, Rectangle<float> r, float h, Colour c, Justification j = Justification::centredLeft, const Typeface::Ptr& tf = nullptr)
    {
        g.setColour (c); g.setFont (fontOf (tf != nullptr ? tf : med, h)); g.drawFittedText (t, r.toNearestInt(), j, 1, 0.8f);
    };
    const auto pi = pageInfo();

    // barre du haut : numero + nom du preset
    auto top = Rectangle<float> (sr.getX(), sr.getY(), sr.getWidth(), 13 * u);
    txt (FM9Processor::programLabel (proc.getCurrentProgram()) + " " + proc.getVoiceName(), top.reduced (3 * u, 0), 7.2f * u, cText, Justification::centredLeft, med);
    if (proc.isEdited()) { g.setColour (cAccent); g.fillEllipse (Rectangle<float> (2.4f * u, 2.4f * u).withCentre ({ top.getRight() - 5 * u, top.getCentreY() })); }
    g.setColour (cFaint); g.fillRect (top.getX() + 2 * u, top.getBottom(), top.getWidth() - 4 * u, 0.5f * u);
    // barre du bas : mode + page (ou valeur reglee)
    auto bottom = Rectangle<float> (sr.getX(), sr.getBottom() - 12 * u, sr.getWidth(), 12 * u).reduced (3 * u, 1.6f * u);
    g.setColour (Colour (0xff6d8796).withAlpha (0.55f)); g.fillRoundedRectangle (bottom, 1.4f * u);
    const String barText = now < flashUntil ? flashText : (pi.numPages > 1 ? String (pageRef() + 1) + "/" + String (pi.numPages) + "  " : String()) + pi.name;
    txt (barText, bottom, 5.6f * u, Colours::black, Justification::centred, bold);

    auto centre = Rectangle<float> (sr.getX(), top.getBottom() + 1 * u, sr.getWidth(), bottom.getY() - top.getBottom() - 2.6f * u).reduced (3 * u, 1 * u);

    // cadres de parametres (4 potards) : nom, valeur, barre
    auto frames = [&] (Rectangle<float> r)
    {
        const float fh = r.getHeight() / 4.0f;
        for (int k = 0; k < 4; ++k)
        {
            auto f = Rectangle<float> (r.getX(), r.getY() + fh * k, r.getWidth(), fh).reduced (0, 0.7f * u);
            g.setColour (Colour (0xff10161d)); g.fillRoundedRectangle (f, 1.4f * u);
            if (pi.s[k].id.isEmpty()) continue;
            txt (pi.s[k].label, f.withTrimmedLeft (2.4f * u).withWidth (f.getWidth() * 0.5f), 4.6f * u, cDim);
            txt (slotValue (pi.s[k]), f.withTrimmedRight (2.4f * u), 5.4f * u, cText, Justification::centredRight, bold);
            if (auto* p = proc.apvts.getParameter (pi.s[k].id))
            {
                // valeur en blocs, comme les barres de la FM-1
                const int blocks = 20, on = roundToInt (p->getValue() * blocks);
                auto bar = Rectangle<float> (f.getX() + 2.4f * u, f.getBottom() - 2.2f * u, f.getWidth() - 4.8f * u, 1.2f * u);
                const float bw = bar.getWidth() / blocks;
                for (int b = 0; b < blocks; ++b)
                {
                    g.setColour (b < on ? cAccent : cFaint);
                    g.fillRect (bar.getX() + bw * b, bar.getY(), bw * 0.72f, bar.getHeight());
                }
            }
        }
    };

    const bool bigPopup = now < popUntil && popKind >= 2;
    if (mode == mHome && bigPopup) { g.setColour (Colour (0xff2c3238)); g.fillRoundedRectangle (centre, 1.4f * u); }
    else if (mode == mHome)
    {
        // oscilloscope : trace jaune sur fond gris, declenche sur un passage par zero
        g.setColour (Colour (0xff2c3238)); g.fillRoundedRectangle (centre, 1.4f * u);
        g.setColour (Colour (0xff3a4148));
        for (int i = 1; i < 4; ++i) g.fillRect (centre.getX(), centre.getY() + centre.getHeight() * i / 4.0f, centre.getWidth(), 0.25f * u);
        const int N = 600;
        const int end = proc.scopePos.load (std::memory_order_acquire);
        int start = (end - N - 400 + FM9Processor::scopeSize) % FM9Processor::scopeSize;
        for (int k = 0; k < 400; ++k)
        {
            const int a = (start + k) % FM9Processor::scopeSize, b = (a + 1) % FM9Processor::scopeSize;
            if (proc.scope[(size_t) a] <= 0.0f && proc.scope[(size_t) b] > 0.0f) { start = b; break; }
        }
        float pk = 0.05f;
        for (int k = 0; k < N; ++k) pk = jmax (pk, std::abs (proc.scope[(size_t) ((start + k) % FM9Processor::scopeSize)]));
        Path p;
        for (int k = 0; k < N; ++k)
        {
            const float v = proc.scope[(size_t) ((start + k) % FM9Processor::scopeSize)] / jmax (0.25f, pk);
            const float x = centre.getX() + centre.getWidth() * k / (float) (N - 1);
            const float y = centre.getCentreY() - v * centre.getHeight() * 0.42f;
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (cYellow); g.strokePath (p, PathStrokeType (0.6f * u, PathStrokeType::curved));
    }
    else if (mode == mFx)
    {
        // grille 3 x 2 des emplacements ; cadre clair = effet actif ; point = emplacement choisi
        const auto order = proc.getFxOrder();
        auto grid = centre.removeFromTop (centre.getHeight() * 0.42f);
        const float cw = grid.getWidth() / 3.0f, ch = grid.getHeight() / 2.0f;
        for (int i = 0; i < 6; ++i)
        {
            const int f = order[(size_t) i];
            auto cell = Rectangle<float> (grid.getX() + cw * (i % 3), grid.getY() + ch * (i / 3), cw, ch).reduced (0.9f * u);
            const bool on = getParam (fm9::fxId (f, "on")) > 0.5f;
            g.setColour (on ? Colour (0xff5f7f91) : Colour (0xff1b2229)); g.fillRoundedRectangle (cell, 1.4f * u);
            txt (fm9::fxShort[f], cell.withTrimmedBottom (2 * u), 4.3f * u, on ? Colours::black : cDim, Justification::centred, bold);
            if (i == fxSlot && (fxGrab || blinkOn (500)))
            { g.setColour (on ? Colours::black : cText); g.fillEllipse (Rectangle<float> (1.6f * u, 1.6f * u).withCentre ({ cell.getCentreX(), cell.getBottom() - 1.8f * u })); }
        }
        frames (centre.withTrimmedTop (1 * u));
    }
    else if (mode == mSeq)
    {
        auto grid = centre.removeFromTop (centre.getHeight() * 0.40f);
        const int pat = roundToInt (getParam ("seqPattern")) - 1, len = roundToInt (getParam ("seqLen"));
        const int ph = proc.seq.playhead.load(), rs = proc.recStep.load();
        const bool rec = getParam ("seqRec") > 0.5f, play = getParam ("seqOn") > 0.5f;
        const float cw = grid.getWidth() / 8.0f, ch = grid.getHeight() / 2.0f;
        for (int i = 0; i < 16; ++i)
        {
            auto cell = Rectangle<float> (grid.getX() + cw * (i % 8), grid.getY() + ch * (i / 8), cw, ch).reduced (0.6f * u);
            const auto st = proc.seqData.get (pat, i);
            const bool inLen = i < len;
            Colour c = ! inLen ? Colour (0xff0d1217) : (st.count > 0 || st.tie) ? Colour (0xff27495c) : Colour (0xff151c23);
            if (i == ph && play && inLen) c = cGreen.withAlpha (0.65f);
            g.setColour (c); g.fillRoundedRectangle (cell, 1.0f * u);
            if (rec && i == rs) { g.setColour (cRed); g.drawRoundedRectangle (cell, 1.0f * u, 0.6f * u); }
            if (inLen && st.tie) txt ("-", cell, 4.0f * u, cText, Justification::centred);
            else if (st.count > 0 && inLen) txt (noteName (st.notes[0]).dropLastCharacters (1) + (st.count > 1 ? "+" : ""), cell, 3.6f * u, cText, Justification::centred);
        }
        frames (centre.withTrimmedTop (1 * u));
    }
    else frames (centre);

    // popups temporaires au centre (preset, algorithme, octave, transposition, volume...)
    if (now < popUntil && popKind > 0)
    {
        auto box = popKind == 1 ? centre.reduced (2 * u, centre.getHeight() * 0.22f) : centre;
        g.setColour (Colour (0xff0b0f13).withAlpha (0.97f)); g.fillRoundedRectangle (box.expanded (1 * u), 2 * u);
        if (popKind == 1)
        {
            g.setColour (Colour (0xff8fb4c4)); g.fillRoundedRectangle (box, 2 * u);
            auto lines = StringArray::fromLines (popValue);
            txt (popTitle, box.removeFromTop (box.getHeight() * 0.42f).withTrimmedTop (2 * u), 6.4f * u, Colours::black, Justification::centred, med);
            txt (lines[0], box.removeFromTop (box.getHeight() * (lines.size() > 1 ? 0.6f : 1.0f)), 8.0f * u, Colours::black, Justification::centred, bold);
            if (lines.size() > 1) txt (lines[1], box, 4.6f * u, Colour (0xff20303a), Justification::centred);
        }
        else if (popKind == 2)
        {
            // liste des presets : 5 lignes, la ligne du milieu surlignee
            const float rh = box.getHeight() / 5.0f;
            const int cur = proc.getCurrentProgram();
            for (int d = -2; d <= 2; ++d)
            {
                const int idx = (cur + d + 128) % 128;
                auto row = Rectangle<float> (box.getX(), box.getCentreY() + (d - 0.5f) * rh, box.getWidth(), rh).reduced (0, 0.4f * u);
                if (d == 0) { g.setColour (Colour (0xff8fb4c4)); g.fillRoundedRectangle (row, 1.2f * u); }
                txt (FM9Processor::programLabel (idx) + " " + String (fm9::voiceName (proc.bankVoice (idx))), row.reduced (2 * u, 0), 6.0f * u,
                     d == 0 ? Colours::black : cDim, Justification::centredLeft, d == 0 ? bold : med);
            }
        }
        else if (popKind == 3)
        {
            const int alg = roundToInt (getParam ("alg"));
            txt ("ALGORITHM " + String (alg + 1).paddedLeft ('0', 2), box.removeFromTop (9 * u), 5.4f * u, cText, Justification::centred, bold);
            drawAlgorithm (g, box.reduced (2 * u, 1 * u), alg, u);
        }
    }
    if (proc.isLearning())
    {
        g.setColour (Colours::black.withAlpha (0.85f)); g.fillRect (centre);
        txt ("MIDI LEARN", centre.withTrimmedBottom (centre.getHeight() * 0.5f), 7.0f * u, cOrange, Justification::centredBottom, bold);
        txt ("move a control", centre.withTrimmedTop (centre.getHeight() * 0.5f), 5.0f * u, cDim, Justification::centredTop);
    }
    g.restoreState();

    // vitre : leger reflet fixe en haut a gauche par dessus l'affichage
    ColourGradient refl (Colours::white.withAlpha (0.055f), sr.getX(), sr.getY(), Colours::white.withAlpha (0.0f), sr.getX() + sr.getWidth() * 0.55f, sr.getY() + sr.getHeight() * 0.55f, false);
    g.setGradientFill (refl);
    g.fillRoundedRectangle (sr, 7.5f);
}

void FM9Editor::drawDrum (Graphics& g)
{
    if (! drumOpen) return;
    const auto fr = L::fnKeys[7];
    const auto drum = Rectangle<float> (fr.getCentreX() - 110, fr.getCentreY() - 150, 220, 300);   // centre sur la touche SAVE
    g.setColour (Colours::black.withAlpha (0.985f));
    g.fillRoundedRectangle (drum, 14.0f);
    ColourGradient halo (cAccent.withAlpha (0.10f), drum.getCentreX(), drum.getCentreY(), cAccent.withAlpha (0.0f), drum.getRight(), drum.getCentreY(), true);
    g.setGradientFill (halo); g.fillRoundedRectangle (drum, 14.0f);
    g.setFont (fontOf (bold, 12.0f).withExtraKerningFactor (0.12f));
    g.setColour (cDim);
    g.drawText ("SAVE TO", drum.withHeight (30).translated (0, 6), Justification::centred);
    for (int d = -3; d <= 3; ++d)
    {
        const int idx = drumIndex + d;
        if (idx < 0 || idx >= proc.getNumPrograms()) continue;
        const float y = drum.getCentreY() + d * 40.0f;
        const float a = 1.0f - std::abs ((float) d) * 0.27f;
        auto row = Rectangle<float> (drum.getX() + 12, y - 18, drum.getWidth() - 24, 36);
        if (d == 0) { g.setColour (cAccent.withAlpha (0.22f)); g.fillRoundedRectangle (row, 8.0f); }
        g.setColour ((d == 0 ? Colours::white : cText).withAlpha (a));
        g.setFont (fontOf (d == 0 ? bold : med, d == 0 ? 19.0f : 16.0f));
        g.drawText (FM9Processor::programLabel (idx) + "  " + String (fm9::voiceName (proc.bankVoice (idx))), row, Justification::centred);
    }
    g.setColour (cDim);
    g.setFont (fontOf (med, 10.5f).withExtraKerningFactor (0.10f));
    g.drawText ("SCROLL - CLICK TO SAVE - ESC", drum.withTrimmedTop (drum.getHeight() - 26), Justification::centred);
}

void FM9Editor::paint (Graphics& g)
{
    lastPaintMs = Time::getMillisecondCounterHiRes();
    repaintAskedMs = 0.0;
    g.fillAll (Colour (0xff141416));
    // anti-gel : aucun rescale ici, on dessine le cache existant (ou l'original en attendant le timer)
    if (bgScaled.isValid()) { g.setImageResamplingQuality (Graphics::lowResamplingQuality); g.drawImage (bgScaled, getLocalBounds().toFloat()); }
    else
    {
        const float k = (float) getHeight() / baseH;
        g.setColour (shellCol);
        g.fillRoundedRectangle (L::shell.getX() * k, L::shell.getY() * k, L::shell.getWidth() * k, L::shell.getHeight() * k, L::shellR * k);
    }
    g.addTransform (AffineTransform::scale ((float) getHeight() / baseH));
    if (! bgScaled.isValid()) drawPrint (g);
    drawDynamic (g);
    drawScreen (g);
    drawDrum (g);
}


// --------------------------------------------------------------------------------------------- panneau de couleurs (a la volee)
class FM9ColourPanel : public Component, private ChangeListener
{
public:
    FM9ColourPanel (FM9Editor& e, Typeface::Ptr medium, int startElement) : ed (e), med (medium), sel (startElement)
    {
        picker.setColour (ColourSelector::backgroundColourId, Colours::transparentBlack);
        picker.setColour (ColourSelector::labelTextColourId, Colour (0xffc9cfd6));
        picker.addChangeListener (this);
        addAndMakeVisible (picker);
        btnLnf.med = med;
        save.setLookAndFeel (&btnLnf);
        save.onClick = [this] { ed.askSaveSkin(); };
        save.setColour (TextButton::buttonColourId, Colour (0xff2a3036));
        save.setColour (TextButton::textColourOffId, Colour (0xffdfe6ea));
        addAndMakeVisible (save);
        setSize (390, 236);
        select (sel);
    }
    std::function<Colour (int)> current;
    void select (int e)
    {
        sel = e;
        if (current) { ignore = true; picker.setCurrentColour (current (e), dontSendNotification); ignore = false; }
        repaint();
    }
    void resized() override
    {
        picker.setBounds (getLocalBounds().withTrimmedLeft (124).reduced (4));
        save.setBounds (10, getHeight() - 40, 106, 28);
    }
    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff1b1d21));
        g.setFont (Font (FontOptions (med).withHeight (13.5f)));
        for (int i = 0; i < FM9Editor::numElements; ++i)
        {
            const auto r = row (i);
            if (i == sel) { g.setColour (Colour (0xff4fc3f7).withAlpha (0.22f)); g.fillRoundedRectangle (r.toFloat(), 6.0f); }
            const auto sw = Rectangle<float> (16, 16).withCentre ({ (float) r.getX() + 16.0f, (float) r.getCentreY() });
            g.setColour (current ? current (i) : Colours::grey); g.fillRoundedRectangle (sw, 4.0f);
            g.setColour (Colours::white.withAlpha (0.35f)); g.drawRoundedRectangle (sw, 4.0f, 1.0f);
            g.setColour (i == sel ? Colours::white : Colour (0xffb9c0c8));
            g.drawText (FM9Editor::elementName (i), r.withTrimmedLeft (32), Justification::centredLeft, false);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        for (int i = 0; i < FM9Editor::numElements; ++i) if (row (i).contains (e.getPosition())) select (i);
    }
private:
    Rectangle<int> row (int i) const { return { 6, 10 + i * 34, 114, 30 }; }
    void changeListenerCallback (ChangeBroadcaster*) override
    {
        if (ignore) return;
        ed.setElementColour (sel, picker.getCurrentColour());
        repaint();
    }
    FM9Editor& ed;
    Typeface::Ptr med;
    int sel = 0;
public:
    ~FM9ColourPanel() override { save.setLookAndFeel (nullptr); }
private:
    bool ignore = false;
    struct BtnLnf : LookAndFeel_V4
    {
        Typeface::Ptr med;
        Font getTextButtonFont (TextButton&, int) override { return Font (FontOptions (med).withHeight (13.5f)); }
    } btnLnf;
    TextButton save { "Save skin..." };
    ColourSelector picker { ColourSelector::showColourAtTop | ColourSelector::showSliders | ColourSelector::showColourspace | ColourSelector::editableColour };
};

void FM9Editor::openColourPanel (Point<int> at, int element)
{
    auto panel = std::make_unique<FM9ColourPanel> (*this, med, element);
    panel->current = [safe = Component::SafePointer<FM9Editor> (this)] (int e) { return safe != nullptr ? safe->elementColour (e) : Colours::grey; };
    panel->select (element);
    // ouvert la ou l'on a clique (bulle pointee sur le clic), a l'interieur de la fenetre du plugin
    CallOutBox::launchAsynchronously (std::move (panel), Rectangle<int> (at.x, at.y, 1, 1), this);
}

// rendu de demonstration : "shell=#rrggbb,pad=...,keys=...,ink=...,led=..."
void FM9Editor::setSnapshotColours (const String& spec)
{
    const char* names[numElements] = { "shell", "pad", "keys", "ink", "led" };
    for (auto& kv : StringArray::fromTokens (spec, ",", ""))
        for (int e = 0; e < numElements; ++e)
            if (kv.upToFirstOccurrenceOf ("=", false, false) == names[e])
                setElementColour (e, Colour::fromString ("ff" + kv.fromFirstOccurrenceOf ("#", false, false)));
    recomposeFace();
}

PopupMenu FM9Editor::buildSkinMenu()
{
    PopupMenu skinMenu;
    skinMenu.addSectionHeader ("FM-1 colors");
    for (int i = 0; i < numSkins; ++i) skinMenu.addItem (900 + i, skins[i].name, true, skinIndex() == i);
    const auto userList = userSkins();
    const auto curName = proc.uiState.getProperty ("skinName").toString();
    if (! userList.isEmpty())
    {
        skinMenu.addSeparator();
        skinMenu.addSectionHeader ("My skins");
        for (int i = 0; i < userList.size() && i < 90; ++i)
            skinMenu.addItem (1900 + i, userList[i].getFileNameWithoutExtension(), true, skinIndex() < 0 && curName == userList[i].getFileNameWithoutExtension());
    }
    if (skinIndex() < 0 && curName.isEmpty()) skinMenu.addItem (920, "Custom (not saved)", false, true);
    skinMenu.addSeparator();
    skinMenu.addItem (950, "Edit colours...");
    skinMenu.addItem (951, "Save skin as preset...");
    if (! userList.isEmpty())
    {
        PopupMenu del;
        for (int i = 0; i < userList.size() && i < 90; ++i) del.addItem (2000 + i, userList[i].getFileNameWithoutExtension());
        skinMenu.addSubMenu ("Delete skin preset", del);
    }
    skinMenu.addItem (952, "Import skin (.fm9skin)...");
    skinMenu.addItem (953, "Export skin (.fm9skin)...");
    skinMenu.addItem (954, "Open skins folder");
    return skinMenu;
}

// apercu : un menu dessine avec le LookAndFeel des menus du plugin (meme rendu que le vrai menu)
Image FM9Editor::renderMenuPreview (const PopupMenu& m, const String& highlight, float scale, int* highlightY)
{
    struct Row { const PopupMenu::Item* it; int h; };
    std::vector<Row> rows;
    int w = 120, h = 0;
    const int border = menuLnf.getPopupMenuBorderSize();
    for (PopupMenu::MenuItemIterator i (m); i.next();)
    {
        auto& it = i.getItem();
        int iw = 0, ih = 0;
        if (it.isSectionHeader) { iw = 160; ih = 30; }
        else menuLnf.getIdealPopupMenuItemSize (it.text, it.isSeparator, 32, iw, ih);
        if (it.subMenu != nullptr) iw += 20;
        w = jmax (w, iw); rows.push_back ({ &it, ih }); h += ih;
    }
    Image img (Image::ARGB, roundToInt ((float) (w + 2 * border) * scale), roundToInt ((float) (h + 2 * border) * scale), true);
    Graphics g (img);
    g.addTransform (AffineTransform::scale (scale));
    menuLnf.drawPopupMenuBackground (g, w + 2 * border, h + 2 * border);
    int y = border;
    for (auto& r : rows)
    {
        const Rectangle<int> a (border, y, w, r.h);
        if (r.it->isSectionHeader) menuLnf.drawPopupMenuSectionHeader (g, a, r.it->text);
        else
        {
            const bool hi = r.it->text == highlight;
            if (hi && highlightY != nullptr) *highlightY = y;
            menuLnf.drawPopupMenuItem (g, a, r.it->isSeparator, r.it->isEnabled, hi, r.it->isTicked, r.it->subMenu != nullptr, r.it->text, {}, nullptr, nullptr);
        }
        y += r.h;
    }
    return img;
}

PopupMenu FM9Editor::buildMainMenuForPreview()
{
    PopupMenu m;
    m.addItem (1, "Load preset"); m.addItem (2, "Save preset to...");
    m.addSeparator();
    m.addItem (3, "Firmware"); m.addItem (4, "Plugin size"); m.addItem (5, "Glow");
    PopupMenu sk = buildSkinMenu(); m.addSubMenu ("Skin", sk);
    m.addItem (6, "Effects"); m.addItem (7, "MIDI"); m.addItem (8, "NKS");
    return m;
}

// --------------------------------------------------------------------------------------------- menu clic droit
void FM9Editor::showContextMenu (const String& learnId)
{
    PopupMenu menu;
    if (learnId.isNotEmpty() && proc.apvts.getParameter (learnId) != nullptr)
    {
        const String name = proc.apvts.getParameter (learnId)->getName (40);
        const int cc = proc.learnedCcFor (learnId);
        menu.addItem (1, "MIDI Learn: " + name + (cc >= 0 ? "  (CC " + String (cc) + ")" : String()), true, proc.isLearning() && proc.getLearnTarget() == learnId);
        menu.addItem (2, "Clear MIDI: " + name, cc >= 0);
        if (proc.isLearning()) menu.addItem (3, "Cancel MIDI learn");
        menu.addSeparator();
    }

    PopupMenu banks;
    for (int b = 0; b < FM9Processor::numBanks; ++b)
    {
        PopupMenu bank;
        const String bn = String::charToString ((juce_wchar) ('A' + b));
        for (int i = 0; i < FM9Processor::bankSize; ++i)
        {
            const int idx = b * FM9Processor::bankSize + i;
            bank.addItem (1000 + idx, FM9Processor::programLabel (idx) + "  " + String (fm9::voiceName (proc.bankVoice (idx))), true, proc.getCurrentProgram() == idx);
        }
        bank.addSeparator();
        bank.addItem (300 + b, "Load DX7 SysEx bank (.syx) into " + bn + "...");
        bank.addItem (310 + b, "Save bank " + bn + " as SysEx (.syx)...");
        bank.addItem (320 + b, b == 0 ? "Reset bank A to factory presets" : "Reset bank " + bn + " (init voices)");
        banks.addSubMenu ("Bank " + bn + "  (" + FM9Processor::programLabel (b * 32) + "-" + FM9Processor::programLabel (b * 32 + 31) + ")", bank);
    }
    menu.addSubMenu ("Load preset", banks);
    menu.addItem (330, "Save preset to...");
    menu.addItem (331, "Load single voice (.syx)...");
    menu.addItem (332, "Save voice as SysEx (.syx)...");
    menu.addItem (333, "Rename voice...");
    menu.addSeparator();

    PopupMenu firmware;
    {
        const auto cur = proc.getFirmwareName();
        firmware.addItem (700, "FM-9 synthesizer (built-in)", true, cur.isEmpty());
        firmware.addSeparator();
        const auto list = proc.availableFirmwares();
        for (int i = 0; i < list.size() && i < 90; ++i)
            firmware.addItem (710 + i, list[i] + (list[i] == FM9Processor::builtinFirmware ? "  (built-in)" : ""), true, cur == list[i]);
        firmware.addSeparator();
        firmware.addItem (804, "Catalog (all FM-1 firmwares)...");
        firmware.addItem (801, "Install from GitHub...");
        firmware.addItem (802, "Install from file (.fwsc or .wasm)...");
        firmware.addItem (803, "Open firmware folder");
    }
    menu.addSubMenu ("Firmware", firmware);
    menu.addSeparator();

    PopupMenu sizes;
    {
        const char* names[numSizes] = { "60 %", "75 %", "100 %", "125 %" };
        const int def = WigaWindowSize::defaultIndex ("FM-9", numSizes);
        PopupMenu defSizes;
        for (int i = 0; i < numSizes; ++i)
        {
            sizes.addItem (10 + i, String (names[i]) + (i == def ? "  (default)" : ""), true, std::abs (getHeight() - sizeHeight (i)) <= 2);
            defSizes.addItem (20 + i, names[i], true, i == def);
        }
        sizes.addSeparator();
        sizes.addSubMenu ("Default size (new windows)", defSizes);
    }
    menu.addSubMenu ("Plugin size", sizes);
    PopupMenu glow;
    const int gl = (int) proc.uiState.getProperty ("glow", 75);
    for (int v : { 25, 50, 75, 100 }) glow.addItem (500 + v, String (v) + " %", true, gl == v);
    menu.addSubMenu ("Glow", glow);
    PopupMenu skinMenu = buildSkinMenu();
    menu.addSubMenu ("Skin", skinMenu);
    PopupMenu fx;
    fx.addItem (470, "Reset effects order (FX + SEL to move)");
    fx.addItem (471, "Delay synced to tempo", true, getParam ("delaySync") > 0.5f);
    PopupMenu div;
    for (int i = 0; i < fm9::delayDivNames().size(); ++i) div.addItem (480 + i, fm9::delayDivNames()[i], true, roundToInt (getParam ("delayDiv")) == i);
    fx.addSubMenu ("Delay division", div);
    menu.addSubMenu ("Effects", fx);

    PopupMenu midi;
    midi.addItem (600, "Clear all MIDI assignments");
    midi.addSeparator();
    {
        PopupMenu ctl;
        ctl.addItem (610, "None (MIDI Learn only)", true, proc.ctlProfile.load() == 0);
        ctl.addItem (611, "Arturia KeyStep 37", true, proc.ctlProfile.load() == 1);
        ctl.addSeparator();
        ctl.addSectionHeader ("KeyStep 37 knobs (Shift + Chord = CC banks)");
        for (auto& b : FM9Processor::keyStepBankNames()) ctl.addItem (612, b, false);
        ctl.addItem (612, "Mod strip = mod wheel, pitch strip = bend, aftertouch, sustain", false);
        ctl.addItem (612, "MIDI Learn always has priority", false);
        midi.addSubMenu ("Controller", ctl);
    }
    midi.addSeparator();
    midi.addItem (601, "Right-click a knob to learn its parameter", false);
    midi.addItem (602, "Program Change 0-127 = presets 001-128", false);
    midi.addItem (603, "DX7 SysEx voice / bank dumps are accepted", false);
    menu.addSubMenu ("MIDI", midi);
    PopupMenu nksMenu;
    nksMenu.addItem (620, "Export NKS library (Komplete Kontrol / Maschine)");
    nksMenu.addItem (621, "Save current preset as NKS (.nksf)...");
    nksMenu.addItem (622, "Open NKS user folder");
    menu.addSubMenu ("NKS", nksMenu);
    menu.addSeparator();
    menu.addItem (999, "FM-9 v" + String (JucePlugin_VersionString) + " - NINE HANDS | DSP", false);

    menu.setLookAndFeel (&menuLnf);
    lastMenuPos = getMouseXYRelative();
    menu.showMenuAsync (PopupMenu::Options().withMousePosition(),       // toujours la ou l'on clique
        [safe = Component::SafePointer<FM9Editor> (this), learnId] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& ed = *safe;
            auto& pr = ed.proc;
            auto syxChooser = [&ed] (bool save, const String& title, std::function<void (const File&)> fn)
            {
                ed.chooser = std::make_unique<FileChooser> (title, File::getSpecialLocation (File::userDocumentsDirectory), "*.syx");
                const int flags = (save ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting : FileBrowserComponent::openMode)
                                  | FileBrowserComponent::canSelectFiles;
                ed.chooser->launchAsync (flags, [fn] (const FileChooser& fc) { if (fc.getResult() != File()) fn (fc.getResult()); });
            };
            if (r == 1) pr.startMidiLearn (learnId);
            else if (r == 2) pr.clearMidiLearn (learnId);
            else if (r == 3) pr.cancelMidiLearn();
            else if (r >= 10 && r < 10 + numSizes) ed.setScaledHeight (sizeHeight (r - 10));
            else if (r >= 20 && r < 20 + numSizes) { WigaWindowSize::setDefaultIndex ("FM-9", r - 20); ed.setScaledHeight (sizeHeight (r - 20)); }
            else if (r >= 300 && r < 304)
            {
                const int b = r - 300;
                syxChooser (false, "Load a DX7 SysEx bank", [safe, b] (const File& f)
                {
                    if (safe != nullptr) safe->popup ("SysEx", safe->proc.importSysexFile (f, b) ? "Bank loaded" : "Not a DX7 file");
                });
            }
            else if (r >= 310 && r < 314)
            {
                const int b = r - 310;
                syxChooser (true, "Save bank as DX7 SysEx", [safe, b] (const File& f) { if (safe != nullptr) safe->proc.exportBank (f.withFileExtension ("syx"), b); });
            }
            else if (r >= 320 && r < 324) pr.resetBankToFactory (r - 320);
            else if (r == 330) ed.openSaveDrum();
            else if (r == 331)
                syxChooser (false, "Load a DX7 SysEx voice", [safe] (const File& f)
                {
                    if (safe != nullptr) safe->popup ("SysEx", safe->proc.importSysexFile (f, safe->proc.getCurrentProgram() / FM9Processor::bankSize) ? "Voice loaded" : "Not a DX7 file");
                });
            else if (r == 332)
                syxChooser (true, "Save voice as DX7 SysEx", [safe] (const File& f) { if (safe != nullptr) safe->proc.exportVoice (f.withFileExtension ("syx")); });
            else if (r == 333) ed.renameVoice();
            else if (r == 470) pr.setFxOrder ({ fm9::fxFilter, fm9::fxReverb, fm9::fxDelay, fm9::fxDrive, fm9::fxChorus, fm9::fxPhaser });
            else if (r == 471) ed.setParam ("delaySync", ed.getParam ("delaySync") > 0.5f ? 0.0f : 1.0f);
            else if (r >= 480 && r < 492) ed.setParam ("delayDiv", (float) (r - 480));
            else if (r >= 900 && r < 900 + numSkins) ed.applySkin (skins[r - 900].id, true);
            else if (r == 950) ed.openColourPanel (ed.lastMenuPos);
            else if (r == 951) ed.askSaveSkin();
            else if (r >= 1900 && r < 1990) { const auto l = userSkins(); if (r - 1900 < l.size() && ! ed.loadUserSkin (l[r - 1900])) ed.popup ("Skin", "Unreadable file"); }
            else if (r >= 2000 && r < 2090)
            {
                const auto l = userSkins();
                if (r - 2000 < l.size())
                {
                    const auto f = l[r - 2000];
                    AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Delete skin preset", "Delete \"" + f.getFileNameWithoutExtension() + "\" ?", "Delete", "Cancel", &ed,
                        ModalCallbackFunction::create ([safe, f] (int ok)
                        {
                            if (! ok || safe == nullptr) return;
                            f.moveToTrash();
                            if (safe->proc.uiState.getProperty ("skinName").toString() == f.getFileNameWithoutExtension()) safe->proc.uiState.removeProperty ("skinName", nullptr);
                            safe->popup ("Skin", "Deleted");
                        }));
                }
            }
            else if (r == 952)
            {
                ed.chooser = std::make_unique<FileChooser> ("Import a skin", File::getSpecialLocation (File::userDocumentsDirectory), "*.fm9skin");
                ed.chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe] (const FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (safe == nullptr || f == File()) return;
                    skinFolder().createDirectory();
                    const auto dst = skinFolder().getChildFile (f.getFileName());
                    if (dst != f) f.copyFileTo (dst);
                    safe->popup ("Skin", safe->loadUserSkin (dst) ? "Imported" : "Unreadable file");
                });
            }
            else if (r == 953)
            {
                const auto nm = pr.uiState.getProperty ("skinName").toString();
                ed.chooser = std::make_unique<FileChooser> ("Export the skin", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ((nm.isNotEmpty() ? nm : String ("FM-9 skin")) + ".fm9skin"), "*.fm9skin");
                ed.chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting, [safe, nm] (const FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (safe == nullptr || f == File()) return;
                    const auto out = f.withFileExtension ("fm9skin");
                    safe->popup ("Skin", out.replaceWithText (safe->currentSkinJson (nm.isNotEmpty() ? nm : out.getFileNameWithoutExtension())) ? "Exported" : "Not saved");
                });
            }
            else if (r == 954) { skinFolder().createDirectory(); skinFolder().startAsProcess(); }
            else if (r > 500 && r < 600) { pr.uiState.setProperty ("glow", r - 500, nullptr); ed.repaint(); }
            else if (r == 610 || r == 611) { pr.ctlProfile.store (r - 610); ed.popup ("Controller", r == 611 ? "KeyStep 37" : "MIDI Learn"); }
            else if (r == 620)
            {
                const int n = pr.exportNksLibrary (FM9Processor::nksUserFolder());
                ed.popup ("NKS", String (n) + " presets");
            }
            else if (r == 621)
            {
                ed.chooser = std::make_unique<FileChooser> ("Save as NKS preset", FM9Processor::nksUserFolder().getChildFile (File::createLegalFileName (pr.getVoiceName().trim()) + ".nksf"), "*.nksf");
                ed.chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting, [safe] (const FileChooser& fc)
                {
                    if (safe == nullptr || fc.getResult() == File()) return;
                    safe->popup ("NKS", safe->proc.exportNksPreset (-1, fc.getResult().withFileExtension ("nksf")) ? "Saved" : "Not saved");
                });
            }
            else if (r == 622) { FM9Processor::nksUserFolder().createDirectory(); FM9Processor::nksUserFolder().startAsProcess(); }
            else if (r == 600) for (auto* p : pr.getParameters()) if (auto* rp = dynamic_cast<RangedAudioParameter*> (p)) pr.clearMidiLearn (rp->getParameterID());
            else if (r >= 1000 && r < 1000 + pr.getNumPrograms()) pr.setCurrentProgram (r - 1000);
            else if (r == 700) ed.activateFirmware ({});
            else if (r >= 710 && r < 800) { const auto list = pr.availableFirmwares(); if (r - 710 < list.size()) ed.activateFirmware (list[r - 710]); }
            else if (r == 804) ed.openCatalog();
            else if (r == 801) ed.installFromGitHub();
            else if (r == 802) ed.installFromFile();
            else if (r == 803) { fm9::FirmwareInstaller::folder().createDirectory(); fm9::FirmwareInstaller::folder().startAsProcess(); }
        });
}

// --------------------------------------------------------------------------------------------- firmwares
void FM9Editor::activateFirmware (const String& name)
{
    String err;
    if (proc.firmware() == nullptr && name.isEmpty()) return;
    if (! proc.activateFirmware (name, err))
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Firmware", "Could not start " + name + " :\n" + err, "OK", this);
        return;
    }
    firmwareActivated (name);
}

void FM9Editor::firmwareActivated (const String& name)
{
    heldFn = heldOct = fwHeldKey = -1; fwDrawnSerial = 0; fwImage = {};
    drawnPanelSig.clear(); drawnScreenSig.clear();
    if (name.isEmpty()) popup ("FM-9", "Synthesizer");
    repaint();
}

void FM9Editor::openCatalog()
{
    catalog = std::make_unique<fm9::FirmwareCatalogView> (light, med, bold);
    const Component::SafePointer<FM9Editor> self (this);
    catalog->onClose = [self] { MessageManager::callAsync ([self] { if (self != nullptr) self->catalog.reset(); }); };
    catalog->onActivate = [self] (const String& name)
    {
        MessageManager::callAsync ([self, name] { if (self != nullptr) { self->catalog.reset(); self->activateFirmware (name); } });
    };
    // Load : le firmware est prepare en arriere-plan (progression dans le bouton), puis active ici
    FM9Processor* pr = &proc;
    catalog->loader = [pr] (const String& name, String& err, std::function<void (float)> progress) { return pr->loadFirmware (name, err, progress); };
    catalog->onLoaded = [self] (const String& name, std::shared_ptr<std::unique_ptr<fm9::FirmwareHost>> fw, const String& err)
    {
        if (self == nullptr) return;
        if (*fw == nullptr)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Firmware", "Could not start " + name + " :\n" + err, "OK", self.getComponent());
            return;
        }
        self->proc.activatePrepared (name, std::move (*fw));
        self->firmwareActivated (name);
        MessageManager::callAsync ([self] { if (self != nullptr) self->catalog.reset(); });
    };
    addAndMakeVisible (*catalog);
    catalog->setBounds (getLocalBounds());
}

void FM9Editor::installFromGitHub()
{
    auto* w = new AlertWindow ("Install a firmware from GitHub",
                               "GitHub repository of an FM-1 firmware: update package (.fwsc) or web build\n(owner/repo, github.com link or github.io page)", MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("repo", "https://deadactive.github.io/hortator/webapp/installer/");
    w->addButton ("Install", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    w->enterModalState (true, ModalCallbackFunction::create ([safe = Component::SafePointer<FM9Editor> (this), w] (int r)
    {
        if (safe == nullptr || r != 1) return;
        String owner, repo;
        if (! fm9::FirmwareInstaller::parseRepo (w->getTextEditorContents ("repo"), owner, repo))
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Firmware", "Not a GitHub repository.", "OK", safe.getComponent());
            return;
        }
        safe->popup ("Firmware", "Downloading " + repo + "...");
        Thread::launch ([safe, owner, repo]
        {
            String err;
            const auto file = fm9::FirmwareInstaller::install (owner, repo, err);
            MessageManager::callAsync ([safe, file, err, repo]
            {
                if (safe == nullptr) return;
                if (file == File()) AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Firmware", "Installation failed:\n" + err, "OK", safe.getComponent());
                else safe->activateFirmware (file.getFileNameWithoutExtension() + (file.hasFileExtension ("fwsc") ? fm9::FirmwareInstaller::hardwareSuffix : String()));
            });
        });
    }), true);
}

void FM9Editor::installFromFile()
{
    chooser = std::make_unique<FileChooser> ("FM-1 firmware (update package .fwsc or web build .wasm)", File::getSpecialLocation (File::userHomeDirectory), "*.fwsc;*.wasm");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe = Component::SafePointer<FM9Editor> (this)] (const FileChooser& fc)
    {
        if (safe == nullptr || fc.getResult() == File()) return;
        String err;
        const auto dest = fm9::FirmwareInstaller::installPackage (fc.getResult(), err);
        if (dest == File()) { AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Firmware", err, "OK", safe.getComponent()); return; }
        safe->activateFirmware (dest.getFileNameWithoutExtension() + (dest.hasFileExtension ("fwsc") ? fm9::FirmwareInstaller::hardwareSuffix : String()));
    });
}

// --------------------------------------------------------------------------------------------- captures
void FM9Editor::prepareSnapshot (float scale, int md, int pg, int pop)
{
    forcedScale = scale;
    mode = (Mode) jlimit (0, (int) mCount - 1, md);
    pageRef() = pg;
    editMode = mode == mOp || mode == mPitch || mode == mVoice;
    flashUntil = 0;
    if (pop > 0) { popKind = pop; popTitle = "Octave"; popValue = "+1"; popUntil = Time::getMillisecondCounterHiRes() + 60000.0; }
    if (auto* fw = proc.firmware()) { fw->copyScreen (fwImage); fwDrawnSerial = fw->fbSerial.load(); }
    rebuildCaches (scale);
    master->prepareForSnapshot (scale);
    for (auto& e : enc) e->prepareForSnapshot (scale);
}
