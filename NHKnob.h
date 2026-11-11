// NINE HANDS | DSP - potards animes par calques (skill potards-nine-hands-dsp)
// nh::Knob : potard lie a un parametre (course -135 / +135 deg).
// nh::Encoder : encodeur sans fin (FM-1 : SELECT, PRESETS, ALGORITHM, KNOB1-4) qui envoie des crans.
// Marqueur, chapeau, couronne et ombre tournent ensemble ; flou de mouvement selon la vitesse ;
// reflets fixes ; ombre a 45 deg vers le bas a droite (lumiere en haut a gauche) ; anti-gel v2.
#pragma once
#include <JuceHeader.h>

namespace nh
{
class RotaryLayers : public juce::Component, protected juce::Timer
{
public:
    // base : centre fixe sous les calques qui tournent ; indicator : repere qui tourne, pose apres le reflet fixe
    struct Layers { juce::Image shadow, base, crown, cap, wear, marker, highlight, indicator; };
    struct Style
    {
        float startAngle = juce::degreesToRadians (-135.0f);
        float endAngle   = juce::degreesToRadians ( 135.0f);
        juce::Point<float> shadowOffset { 0.063f, 0.063f };   // lumiere en haut a gauche a 45 deg : ombre a 45 deg vers le bas a droite
        float blurMax    = juce::degreesToRadians (40.0f);
        float blurStep   = juce::degreesToRadians (3.0f);
        float blurDecay  = 0.6f;
        float dragPixels = 250.0f;
    };

    RotaryLayers (Layers l, Style s) : layers (std::move (l)), style (s) { startTimerHz (30); }

    std::function<void (const juce::MouseEvent&)> onRightClick;
    void resized() override { resizeSettle = frame.isValid() ? 3 : 1; }
    void paint (juce::Graphics& g) override { if (frame.isValid()) g.drawImage (frame, getLocalBounds().toFloat()); }
    bool hitTest (int x, int y) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        return c.getDistanceFrom ({ (float) x, (float) y }) < (float) juce::jmin (getWidth(), getHeight()) * 0.40f;
    }

protected:
    virtual float currentAngle() const = 0;

    void timerCallback() override
    {
        if (++scaleTick >= 8)
        {
            scaleTick = 0;
            const auto s = juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this));
            if (std::abs (s - scale) > 0.01f && resizeSettle == 0) resizeSettle = 1;
        }
        if (resizeSettle > 0 && --resizeSettle == 0) { rebuildCache(); dirty = true; }

        const auto a = currentAngle();
        const auto v = a - lastAngle;
        lastAngle = a;
        if (std::abs (v) > 1.0e-5f) blurDir = v > 0.0f ? 1.0f : -1.0f;
        blurSpan = juce::jmax (juce::jmin (std::abs (v), style.blurMax), blurSpan * style.blurDecay);
        if (blurSpan < 0.002f) blurSpan = 0.0f;

        const bool changed = dirty || std::abs (a - drawnAngle) > 1.0e-4f || blurSpan != drawnSpan;
        if (changed && stack.isValid())
        {
            renderFrame (a);
            drawnAngle = a; drawnSpan = blurSpan; dirty = false;
            repaint();
        }
    }

    Style style;

private:
    juce::AffineTransform place (const juce::Image& img, juce::Point<float> c, float a) const
    {
        return juce::AffineTransform::translation (-img.getWidth() * 0.5f, -img.getHeight() * 0.5f)
                   .scaled (1.0f / scale).rotated (a).translated (c.x, c.y);
    }

    void renderFrame (float a)
    {
        const int w = juce::roundToInt (getWidth() * scale), h = juce::roundToInt (getHeight() * scale);
        if (w <= 0 || h <= 0) return;
        if (frame.getWidth() != w || frame.getHeight() != h)
        {
            frame   = juce::Image (juce::Image::ARGB, w, h, true);
            scratch = juce::Image (juce::Image::ARGB, w, h, true);
        }
        frame.clear (frame.getBounds());
        juce::Graphics g (frame);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto b = getLocalBounds().toFloat();
        const auto d = juce::jmin (b.getWidth(), b.getHeight());
        const auto c = b.getCentre();
        if (shadowCache.isValid()) drawBlurred (g, shadowCache, c + style.shadowOffset * d, a);
        if (baseCache.isValid()) g.drawImage (baseCache, b.withSizeKeepingCentre (d, d));
        drawBlurred (g, stack, c, a);
        if (highlightCache.isValid()) g.drawImage (highlightCache, b.withSizeKeepingCentre (d, d));
        if (indicatorCache.isValid()) drawBlurred (g, indicatorCache, c, a);
    }

    void drawBlurred (juce::Graphics& g, const juce::Image& img, juce::Point<float> c, float a)
    {
        if (blurSpan <= 0.0f) { g.drawImageTransformed (img, place (img, c, a), false); return; }
        const int n = juce::jlimit (4, 16, (int) std::ceil (blurSpan / style.blurStep) + 1);
        scratch.clear (scratch.getBounds());
        {
            juce::Graphics gs (scratch);
            gs.addTransform (juce::AffineTransform::scale (scale));
            gs.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            for (int k = 0; k < n; ++k)
            {
                const auto t = (float) k / (float) (n - 1);
                gs.setOpacity (1.0f / (float) (k + 1)); // moyenne progressive exacte
                gs.drawImageTransformed (img, place (img, c, a - blurDir * blurSpan * t), false);
            }
        }
        g.drawImage (scratch, getLocalBounds().toFloat());
    }

    void rebuildCache()
    {
        scale = juce::jmax (1.0f, juce::Component::getApproximateScaleFactorForComponent (this));
        if (forcedScale > 0.0f) scale = forcedScale;
        const int px = juce::roundToInt (juce::jmin (getWidth(), getHeight()) * scale);
        if (px <= 0) return;
        auto fit = [px] (const juce::Image& src)
        {
            if (src.isNull()) return juce::Image();
            juce::Image out (juce::Image::ARGB, px, px, true);
            juce::Graphics g (out);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (src, juce::Rectangle<float> (0.0f, 0.0f, (float) px, (float) px), juce::RectanglePlacement::centred);
            return out;
        };
        shadowCache = fit (layers.shadow);
        highlightCache = fit (layers.highlight);
        baseCache = fit (layers.base);
        indicatorCache = fit (layers.indicator);
        stack = juce::Image (juce::Image::ARGB, px, px, true);
        juce::Graphics g (stack);
        for (auto* l : { &layers.crown, &layers.cap, &layers.wear, &layers.marker })
            if (l->isValid()) g.drawImageAt (fit (*l), 0, 0);
    }

public:
    // captures hors DAW : caches a l'echelle voulue, image prete tout de suite
    void prepareForSnapshot (float sc) { forcedScale = sc; rebuildCache(); renderFrame (currentAngle()); drawnAngle = currentAngle(); }

private:
    Layers layers;
    juce::Image stack, shadowCache, highlightCache, baseCache, indicatorCache, frame, scratch;
    float scale = 1.0f, forcedScale = 0.0f, lastAngle = 0.0f;
    float blurSpan = 0.0f, blurDir = 1.0f;
    float drawnAngle = 1.0e9f, drawnSpan = -1.0f;
    int resizeSettle = 0, scaleTick = 0;
    bool dirty = true;
};

class Knob : public RotaryLayers
{
public:
    Knob (juce::RangedAudioParameter& p, Layers l, Style s = {})
        : RotaryLayers (std::move (l), s), param (p),
          attachment (p, [this] (float v) { target = param.convertTo0to1 (v); if (onValueChange) onValueChange(); }, nullptr)
    {
        attachment.sendInitialUpdate();
    }
    std::function<void()> onValueChange;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (e); return; }
        dragging = true; lastY = e.position.y; attachment.beginGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const auto range = style.dragPixels * (e.mods.isShiftDown() ? 5.0f : 1.0f);
        const auto dy = e.position.y - lastY; lastY = e.position.y;
        setNormalised (target - dy / range, true);
    }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) attachment.endGesture(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) setNormalised (param.getDefaultValue(), false); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { setNormalised (target + w.deltaY * 0.05f, false); }

protected:
    float currentAngle() const override { return style.startAngle + target * (style.endAngle - style.startAngle); }

private:
    void setNormalised (float v, bool inGesture)
    {
        target = juce::jlimit (0.0f, 1.0f, v);
        const auto plain = param.convertFrom0to1 (target);
        if (inGesture) attachment.setValueAsPartOfGesture (plain);
        else           attachment.setValueAsCompleteGesture (plain);
    }

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    float target = 0.0f, lastY = 0.0f;
    bool dragging = false;
};

// encodeur sans fin : 24 crans par tour comme les encodeurs de la FM-1
class Encoder : public RotaryLayers
{
public:
    explicit Encoder (Layers l, Style s = {}) : RotaryLayers (std::move (l), s) {}
    std::function<void (int steps, bool fine)> onStep;
    std::function<void()> onPress;          // clic simple sans rotation

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (e); return; }
        dragging = true; acc = 0; moved = false; lastY = e.position.y;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float pxPerStep = e.mods.isShiftDown() ? 24.0f : 9.0f;
        acc += (lastY - e.position.y) / pxPerStep; lastY = e.position.y;
        while (acc >= 1.0f) { step (1, e.mods.isShiftDown()); acc -= 1.0f; }
        while (acc <= -1.0f) { step (-1, e.mods.isShiftDown()); acc += 1.0f; }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging && ! moved && ! e.mods.isPopupMenu() && onPress) onPress();
        dragging = false;
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        wheelAcc += w.deltaY * 8.0f;
        while (wheelAcc >= 1.0f) { step (1, e.mods.isShiftDown()); wheelAcc -= 1.0f; }
        while (wheelAcc <= -1.0f) { step (-1, e.mods.isShiftDown()); wheelAcc += 1.0f; }
    }
    void step (int s, bool fine)
    {
        moved = true;
        angle += s * juce::MathConstants<float>::twoPi / 24.0f;
        if (onStep) onStep (s, fine);
    }

protected:
    float currentAngle() const override { return angle; }

private:
    float angle = 0.0f, acc = 0.0f, wheelAcc = 0.0f, lastY = 0.0f;
    bool dragging = false, moved = false;
};
} // namespace nh
