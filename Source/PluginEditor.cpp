#include "PluginEditor.h"

#include <BinaryData.h>
#include <cmath>

namespace
{
    using juce::Colour;
    using juce::ColourGradient;
    using juce::Point;
    using juce::Rectangle;

    // Panel paint sampled from the EQP-1A reference photo, plus silkscreen and hardware tones.
    constexpr juce::uint32 panelTop    = 0xff336b7d;
    constexpr juce::uint32 panelBottom = 0xff275a6b;
    constexpr juce::uint32 silkColour  = 0xffe8eef0;
    constexpr juce::uint32 cream       = 0xfff0e8d2;
    constexpr juce::uint32 amber       = 0xffffad5c;

    constexpr float kPi = juce::MathConstants<float>::pi;
    constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

    constexpr int editorWidth = 1320;
    constexpr int editorHeight = 440;

    // Control centres (editor coordinates) shared by layout and the silkscreen layer.
    const Point<float> lowBoostC { 360.0f, 145.0f }, lowAttenC { 500.0f, 145.0f };
    const Point<float> highBoostC { 690.0f, 145.0f }, highAttenC { 830.0f, 145.0f };
    const Point<float> attenSelC { 990.0f, 148.0f }, lampC { 990.0f, 250.0f }, powerC { 990.0f, 342.0f };
    const Point<float> cpsC { 420.0f, 330.0f }, bandwidthC { 580.0f, 330.0f }, kcsC { 745.0f, 330.0f };
    const Point<float> inputC { 110.0f, 94.0f }, driveC { 165.0f, 94.0f }, outputC { 220.0f, 94.0f };
    const Point<float> inC { 110.0f, 332.0f }, tubeC { 165.0f, 332.0f }, xfmrC { 220.0f, 332.0f };
    const Rectangle<float> graphBezel { 1050.0f, 48.0f, 200.0f, 128.0f };

    constexpr float bigR = 40.0f, trimR = 15.0f, cpsS = 34.0f, kcsS = 40.0f, attenS = 30.0f;

    // Rotary sweeps, clockwise from 12 o'clock (JUCE convention, kept positive as Slider requires).
    constexpr float knobStart = 1.25f * kPi, knobEnd = 2.75f * kPi;
    inline float deg (float d) { return kTwoPi + d * kPi / 180.0f; }

    Point<float> polar (Point<float> c, float angle, float radius)
    {
        return c + Point<float> (std::sin (angle), -std::cos (angle)) * radius;
    }

    /** Panel lettering. The typeface ships inside the binary, so the silkscreen
        looks identical on any machine, with or without the font installed. */
    juce::Typeface::Ptr silkTypeface()
    {
        static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (BinaryData::JostRegular_ttf,
                                                                                      (size_t) BinaryData::JostRegular_ttfSize);
        return regular;
    }

    juce::Font silkFont (float size)
    {
        return juce::Font (juce::FontOptions (silkTypeface()).withHeight (size).withKerningFactor (0.06f));
    }

    void silk (juce::Graphics& g, const juce::String& text, Rectangle<float> area, float size,
               juce::Justification just = juce::Justification::centred, float alpha = 0.92f)
    {
        g.setColour (Colour (silkColour).withAlpha (alpha));
        g.setFont (silkFont (size));
        g.drawText (text, area, just, false);
    }

    void silkAt (juce::Graphics& g, const juce::String& text, Point<float> centre, float size, float alpha = 0.92f)
    {
        silk (g, text, Rectangle<float> (size * 9.0f, size + 6.0f).withCentre (centre), size, juce::Justification::centred, alpha);
    }

    void shadowFor (juce::Graphics& g, const juce::Path& p, float alpha, int radius, Point<int> offset)
    {
        juce::DropShadow (juce::Colours::black.withAlpha (alpha), radius, offset).drawForPath (g, p);
    }

    ColourGradient radial (Colour inner, Point<float> centre, Colour outer, float radius)
    {
        return ColourGradient (inner, centre, outer, centre.translated (radius, 0.0f), true);
    }

    //==========================================================================
    // Hardware drawing. Light comes from the top-left everywhere.

    void drawScrew (juce::Graphics& g, Point<float> c, float r, float slotAngle)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.x - r - 1.0f, c.y - r, (r + 1.0f) * 2.0f, (r + 1.5f) * 2.0f);

        ColourGradient head (Colour (0xfff6f8f8), c.translated (-r * 0.45f, -r * 0.55f), Colour (0xff60666a), c.translated (r * 0.8f, r * 0.9f), true);
        head.addColour (0.45, Colour (0xffbfc5c7));
        g.setGradientFill (head);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);

        const auto d = Point<float> (std::cos (slotAngle), std::sin (slotAngle)) * (r * 0.86f);
        g.setColour (Colour (0xff2b2e30));
        g.drawLine ({ c - d, c + d }, r * 0.34f);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawLine ({ c - d + Point<float> (0.0f, r * 0.2f), c + d + Point<float> (0.0f, r * 0.2f) }, 0.7f);
    }

    /** Pultec-style black bakelite knob: scalloped skirt, raised collar, concave glossy cap, pointer line. */
    void drawSkirtedKnob (juce::Graphics& g, Point<float> c, float R, float angle, int lobes)
    {
        juce::Path skirt;
        const int steps = 240;
        const float depth = lobes > 8 ? 0.085f : 0.07f;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = kTwoPi * (float) i / (float) steps;
            const float v = 0.5f * (1.0f - std::cos ((float) lobes * (t - angle)));
            const auto p = polar (c, t, R * (1.0f - depth * v * v * v));
            if (i == 0) skirt.startNewSubPath (p); else skirt.lineTo (p);
        }
        skirt.closeSubPath();

        shadowFor (g, skirt, 0.55f, juce::roundToInt (R * 0.45f), { juce::roundToInt (R * 0.10f), juce::roundToInt (R * 0.20f) });
        shadowFor (g, skirt, 0.65f, juce::roundToInt (R * 0.10f), { 0, juce::roundToInt (R * 0.05f) });

        // Skirt body with a broad sheen across the lobes.
        g.setGradientFill (radial (Colour (0xff2e2f33), c.translated (-R * 0.35f, -R * 0.45f), Colour (0xff040405), R * 1.45f));
        g.fillPath (skirt);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (skirt);
            ColourGradient sheen (juce::Colours::white.withAlpha (0.16f), c.translated (-R, -R),
                                  juce::Colours::black.withAlpha (0.30f), c.translated (R, R), false);
            sheen.addColour (0.5, juce::Colours::transparentBlack);
            g.setGradientFill (sheen);
            g.fillPath (skirt);

            // Each lobe crest catches a little light on its upper-left flank.
            for (int i = 0; i < lobes; ++i)
            {
                const float a = angle + kTwoPi * (float) i / (float) lobes;
                const float facing = 0.5f * (1.0f + std::cos (a + kPi * 0.25f)); // 1 when facing the light
                const auto p0 = polar (c, a, R * 0.80f);
                const auto p1 = polar (c, a, R * 0.99f);
                g.setColour (juce::Colours::white.withAlpha (0.05f + 0.13f * facing * facing));
                g.drawLine ({ p0, p1 }, R * 0.08f);
            }
        }
        ColourGradient rim (juce::Colours::white.withAlpha (0.40f), c.translated (-R, -R), juce::Colours::black.withAlpha (0.9f), c.translated (R * 0.7f, R * 0.7f), false);
        g.setGradientFill (rim);
        g.strokePath (skirt, juce::PathStrokeType (1.1f));

        // Raised collar.
        const float collar = R * 0.72f;
        g.setGradientFill (ColourGradient (Colour (0xff45474c), c.translated (0.0f, -collar), Colour (0xff020203), c.translated (0.0f, collar), false));
        g.fillEllipse (c.x - collar, c.y - collar, collar * 2.0f, collar * 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawEllipse (c.x - collar, c.y - collar, collar * 2.0f, collar * 2.0f, 1.0f);

        // Concave cap: dark at the top lip, lighter toward the bottom.
        const float cap = R * 0.60f;
        g.setGradientFill (ColourGradient (Colour (0xff000000), c.translated (0.0f, -cap), Colour (0xff2c2d31), c.translated (0.0f, cap), false));
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);

        const float face = R * 0.50f;
        g.setGradientFill (radial (Colour (0xff1a1b1e), c.translated (0.0f, face * 0.25f), Colour (0xff050506), face * 1.1f));
        g.fillEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);

        for (float ring : { 0.43f, 0.34f })
        {
            const float rr = R * ring;
            g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (0.20f), c.translated (0.0f, -rr), juce::Colours::white.withAlpha (0.02f), c.translated (0.0f, rr), false));
            g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.9f);
        }

        // Glossy highlights: crescent on the collar, soft spot on the cap.
        juce::Path crescent;
        crescent.addCentredArc (c.x, c.y, collar - 1.5f, collar - 1.5f, 0.0f, -kPi * 0.62f, kPi * 0.05f, true);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.strokePath (crescent, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path capClip;
            capClip.addEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);
            g.reduceClipRegion (capClip);
            const auto spot = c.translated (-R * 0.20f, -R * 0.30f);
            g.setGradientFill (radial (juce::Colours::white.withAlpha (0.30f), spot, juce::Colours::transparentWhite, R * 0.32f));
            g.fillEllipse (spot.x - R * 0.4f, spot.y - R * 0.3f, R * 0.8f, R * 0.6f);
        }

        // Pointer line engraved on the skirt.
        const auto p0 = polar (c, angle, R * 0.75f);
        const auto p1 = polar (c, angle, R * 0.97f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine ({ p0.translated (0.6f, 0.8f), p1.translated (0.6f, 0.8f) }, R * 0.065f);
        g.setColour (Colour (cream));
        g.drawLine ({ p0, p1 }, R * 0.06f);
    }

    /** Round bakelite hub shared by the chicken-head and bar knobs. */
    void drawHub (juce::Graphics& g, Point<float> c, float r)
    {
        g.setGradientFill (radial (Colour (0xff34353a), c.translated (-r * 0.35f, -r * 0.45f), Colour (0xff030304), r * 1.5f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (0.30f), c.translated (-r, -r), juce::Colours::black.withAlpha (0.8f), c.translated (r, r), false));
        g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 1.0f);
    }

    void fillBakeliteBar (juce::Graphics& g, const juce::Path& bar, const juce::AffineTransform& t, float halfWidth, float angle)
    {
        // Ridge shading across the width; the lit side flips with the pointer's orientation.
        const float lightSide = std::cos (angle + kPi * 0.25f) >= 0.0f ? -1.0f : 1.0f;
        auto left = Point<float> (-halfWidth * lightSide, 0.0f).transformedBy (t);
        auto right = Point<float> (halfWidth * lightSide, 0.0f).transformedBy (t);
        ColourGradient ridge (Colour (0xff4a4c51), left, Colour (0xff020203), right, false);
        ridge.addColour (0.42, Colour (0xff2a2b2f));
        ridge.addColour (0.52, Colour (0xff121315));
        g.setGradientFill (ridge);
        g.fillPath (bar);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.strokePath (bar, juce::PathStrokeType (1.0f));
    }

    /** Chicken-head pointer knob (CPS, ATTEN SEL). s = pointer length from the shaft. */
    void drawChickenHead (juce::Graphics& g, Point<float> c, float s, float angle)
    {
        const auto t = juce::AffineTransform::rotation (angle).translated (c);
        juce::Path head;
        head.startNewSubPath (-s * 0.07f, -s);
        head.lineTo (s * 0.07f, -s);
        head.quadraticTo (s * 0.22f, -s * 0.55f, s * 0.27f, -s * 0.05f);
        head.lineTo (s * 0.26f, s * 0.32f);
        head.quadraticTo (s * 0.22f, s * 0.58f, 0.0f, s * 0.60f);
        head.quadraticTo (-s * 0.22f, s * 0.58f, -s * 0.26f, s * 0.32f);
        head.lineTo (-s * 0.27f, -s * 0.05f);
        head.quadraticTo (-s * 0.22f, -s * 0.55f, -s * 0.07f, -s);
        head.closeSubPath();
        head.applyTransform (t);

        const float hubR = s * 0.40f;
        juce::Path hub;
        hub.addEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f);
        juce::Path silhouette (head);
        silhouette.addPath (hub);
        shadowFor (g, silhouette, 0.6f, juce::roundToInt (s * 0.35f), { juce::roundToInt (s * 0.10f), juce::roundToInt (s * 0.18f) });

        drawHub (g, c, hubR);
        fillBakeliteBar (g, head, t, s * 0.27f, angle);

        // Polished top facet and pointer inlay.
        juce::Path ridgeLine;
        ridgeLine.startNewSubPath (0.0f, -s * 0.94f);
        ridgeLine.lineTo (0.0f, s * 0.50f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.strokePath (ridgeLine, juce::PathStrokeType (s * 0.10f), t);
        g.setColour (Colour (cream));
        g.drawLine ({ Point<float> (0.0f, -s * 0.93f).transformedBy (t), Point<float> (0.0f, -s * 0.45f).transformedBy (t) }, s * 0.045f);
    }

    /** Long bar knob (KCS) with a white index on the pointing end. */
    void drawBarKnob (juce::Graphics& g, Point<float> c, float s, float angle)
    {
        const auto t = juce::AffineTransform::rotation (angle).translated (c);
        juce::Path bar;
        bar.addRoundedRectangle (-s * 0.13f, -s, s * 0.26f, s * 1.78f, s * 0.08f);
        bar.applyTransform (t);

        const float hubR = s * 0.36f;
        juce::Path hub;
        hub.addEllipse (c.x - hubR, c.y - hubR, hubR * 2.0f, hubR * 2.0f);
        juce::Path silhouette (bar);
        silhouette.addPath (hub);
        shadowFor (g, silhouette, 0.6f, juce::roundToInt (s * 0.30f), { juce::roundToInt (s * 0.09f), juce::roundToInt (s * 0.16f) });

        drawHub (g, c, hubR);
        fillBakeliteBar (g, bar, t, s * 0.13f, angle);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawLine ({ Point<float> (0.0f, -s * 0.95f).transformedBy (t), Point<float> (0.0f, s * 0.70f).transformedBy (t) }, s * 0.06f);
        g.setColour (Colour (cream));
        g.drawLine ({ Point<float> (0.0f, -s * 0.94f).transformedBy (t), Point<float> (0.0f, -s * 0.52f).transformedBy (t) }, s * 0.045f);
    }

    /** Small pointed knob used for OFF/ON. */
    void drawPointerKnob (juce::Graphics& g, Point<float> c, float r, float angle)
    {
        const float tip = r * 1.55f;
        const float phi = std::acos (r / tip);
        juce::Path knob;
        knob.startNewSubPath (0.0f, -tip);
        knob.lineTo (r * std::sin (phi), -r * std::cos (phi));
        knob.addCentredArc (0.0f, 0.0f, r, r, 0.0f, phi, kTwoPi - phi, false);
        knob.closeSubPath();
        knob.applyTransform (juce::AffineTransform::rotation (angle).translated (c));

        shadowFor (g, knob, 0.6f, juce::roundToInt (r * 0.6f), { juce::roundToInt (r * 0.15f), juce::roundToInt (r * 0.3f) });
        g.setGradientFill (radial (Colour (0xff3a3b40), c.translated (-r * 0.35f, -r * 0.45f), Colour (0xff030304), r * 1.6f));
        g.fillPath (knob);
        g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (0.35f), c.translated (-r, -r), juce::Colours::black.withAlpha (0.9f), c.translated (r, r), false));
        g.strokePath (knob, juce::PathStrokeType (1.0f));

        const auto spot = c.translated (-r * 0.3f, -r * 0.35f);
        g.setGradientFill (radial (juce::Colours::white.withAlpha (0.35f), spot, juce::Colours::transparentWhite, r * 0.5f));
        g.fillEllipse (spot.x - r * 0.5f, spot.y - r * 0.4f, r, r * 0.8f);

        g.setColour (Colour (cream));
        g.drawLine ({ polar (c, angle, r * 0.55f), polar (c, angle, r * 1.35f) }, 1.6f);
    }

    /** Chrome bat-handle toggle on a hex nut. */
    void drawBatToggle (juce::Graphics& g, Point<float> c, bool on)
    {
        auto chrome = [] (Point<float> a, Point<float> b)
        {
            ColourGradient grad (Colour (0xfff3f6f7), a, Colour (0xff595f63), b, false);
            grad.addColour (0.5, Colour (0xffaeb4b7));
            return grad;
        };

        // Washer and hex nut.
        const float washer = 14.0f;
        juce::Path washerPath;
        washerPath.addEllipse (c.x - washer, c.y - washer, washer * 2.0f, washer * 2.0f);
        shadowFor (g, washerPath, 0.5f, 5, { 1, 3 });
        g.setGradientFill (chrome (c.translated (-washer, -washer), c.translated (washer, washer)));
        g.fillPath (washerPath);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (washerPath, juce::PathStrokeType (0.8f));

        juce::Path hex;
        for (int k = 0; k < 6; ++k)
        {
            const auto p = polar (c, kPi / 6.0f + (float) k * kPi / 3.0f, 10.5f);
            if (k == 0) hex.startNewSubPath (p); else hex.lineTo (p);
        }
        hex.closeSubPath();
        g.setGradientFill (chrome (c.translated (-8.0f, -10.0f), c.translated (8.0f, 10.0f)));
        g.fillPath (hex);
        g.setColour (Colour (0xff33373a));
        g.strokePath (hex, juce::PathStrokeType (0.9f));

        g.setColour (Colour (0xff141516));
        g.fillEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f);
        g.setGradientFill (chrome (c.translated (-4.0f, -4.0f), c.translated (4.0f, 4.0f)));
        g.drawEllipse (c.x - 5.5f, c.y - 5.5f, 11.0f, 11.0f, 1.3f);

        // Lever: tapered chrome bat with a ball end.
        const float dir = on ? -1.0f : 1.0f;
        const float length = 24.0f;
        juce::Path lever;
        lever.startNewSubPath (c.x - 3.3f, c.y);
        lever.lineTo (c.x - 2.2f, c.y + dir * length);
        lever.lineTo (c.x + 2.2f, c.y + dir * length);
        lever.lineTo (c.x + 3.3f, c.y);
        lever.closeSubPath();
        const auto ballC = c.translated (0.0f, dir * length);
        juce::Path ball;
        ball.addEllipse (ballC.x - 4.4f, ballC.y - 4.4f, 8.8f, 8.8f);
        juce::Path both (lever);
        both.addPath (ball);
        shadowFor (g, both, 0.5f, 4, { 3, 4 });

        ColourGradient leverFill (Colour (0xff5d6367), Point<float> (c.x - 3.3f, c.y), Colour (0xff3e4245), Point<float> (c.x + 3.3f, c.y), false);
        leverFill.addColour (0.35, Colour (0xfff5f8f9));
        leverFill.addColour (0.65, Colour (0xff9aa0a3));
        g.setGradientFill (leverFill);
        g.fillPath (lever);
        g.setGradientFill (radial (juce::Colours::white, ballC.translated (-1.5f, -1.8f), Colour (0xff3c4043), 6.0f));
        g.fillPath (ball);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.strokePath (both, juce::PathStrokeType (0.6f));
    }

    /** Faceted ruby pilot lamp in a chrome bezel. */
    void drawJewelLamp (juce::Graphics& g, Point<float> c, float r, bool lit)
    {
        if (lit)
        {
            g.setGradientFill (radial (Colour (0xffff3a2a).withAlpha (0.38f), c, Colour (0xffff3a2a).withAlpha (0.0f), r * 3.6f));
            g.fillEllipse (c.x - r * 3.6f, c.y - r * 3.6f, r * 7.2f, r * 7.2f);
        }

        const float bezel = r * 1.5f;
        juce::Path bezelPath;
        bezelPath.addEllipse (c.x - bezel, c.y - bezel, bezel * 2.0f, bezel * 2.0f);
        shadowFor (g, bezelPath, 0.55f, 6, { 1, 3 });
        ColourGradient chrome (Colour (0xfff4f7f8), c.translated (-bezel, -bezel), Colour (0xff50565a), c.translated (bezel, bezel), false);
        chrome.addColour (0.5, Colour (0xffb5bbbe));
        g.setGradientFill (chrome);
        g.fillPath (bezelPath);
        g.setGradientFill (ColourGradient (Colour (0xff4d5256), c.translated (-r, -r), Colour (0xffe9edef), c.translated (r, r), false));
        g.fillEllipse (c.x - r * 1.14f, c.y - r * 1.14f, r * 2.28f, r * 2.28f);

        const Colour core = lit ? Colour (0xffff6a52) : Colour (0xff6a1016);
        const Colour edge = lit ? Colour (0xff8a0612) : Colour (0xff1e0204);
        g.setGradientFill (radial (core, c.translated (r * 0.1f, r * 0.15f), edge, r * 1.05f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        // Facets: alternating light and dark wedges around an octagonal table.
        for (int i = 0; i < 12; ++i)
        {
            const float a0 = kTwoPi * (float) i / 12.0f;
            const float a1 = kTwoPi * (float) (i + 1) / 12.0f;
            juce::Path facet;
            facet.startNewSubPath (polar (c, a0, r * 0.45f));
            facet.lineTo (polar (c, a0, r * 0.97f));
            facet.lineTo (polar (c, a1, r * 0.97f));
            facet.lineTo (polar (c, a1, r * 0.45f));
            facet.closeSubPath();
            g.setColour ((i % 2 == 0) ? juce::Colours::white.withAlpha (lit ? 0.12f : 0.07f)
                                      : juce::Colours::black.withAlpha (0.18f));
            g.fillPath (facet);
        }
        juce::Path table;
        for (int i = 0; i < 8; ++i)
        {
            const auto p = polar (c, kTwoPi * (float) i / 8.0f, r * 0.45f);
            if (i == 0) table.startNewSubPath (p); else table.lineTo (p);
        }
        table.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (lit ? 0.22f : 0.10f));
        g.strokePath (table, juce::PathStrokeType (0.7f));

        const auto spot = c.translated (-r * 0.38f, -r * 0.42f);
        g.setGradientFill (radial (juce::Colours::white.withAlpha (0.85f), spot, juce::Colours::transparentWhite, r * 0.38f));
        g.fillEllipse (spot.x - r * 0.4f, spot.y - r * 0.4f, r * 0.8f, r * 0.8f);
    }

    void drawKnobNumerals (juce::Graphics& g, Point<float> c, float radius, float size = 12.0f)
    {
        for (int i = 0; i <= 10; ++i)
            silkAt (g, juce::String (i), polar (c, knobStart + (knobEnd - knobStart) * (float) i / 10.0f, radius), size);
    }

    template <size_t N>
    void drawSelectorLabels (juce::Graphics& g, Point<float> c, float radius, float start, float end,
                             const std::array<float, N>& values, float divisor)
    {
        for (size_t i = 0; i < N; ++i)
        {
            const float a = start + (end - start) * (float) i / (float) (N - 1);
            silkAt (g, juce::String (values[i] / divisor, 0), polar (c, a, radius), 12.0f);
        }
    }

    juce::Image makeNoiseTile()
    {
        juce::Image img (juce::Image::ARGB, 160, 160, true);
        juce::Random rng (0x5eed);
        juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                const auto a = (juce::uint8) (rng.nextFloat() * 16.0f);
                data.setPixelColour (x, y, rng.nextBool() ? Colour::fromRGBA (255, 255, 255, a)
                                                          : Colour::fromRGBA (0, 0, 0, (juce::uint8) (a + a / 2)));
            }
        return img;
    }
}

//==============================================================================
FullteqLookAndFeel::FullteqLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, Colour (cream));
    setColour (juce::ComboBox::arrowColourId, Colour (cream));
    setColour (juce::PopupMenu::backgroundColourId, Colour (0xff111214));
    setColour (juce::PopupMenu::textColourId, Colour (cream));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colour (panelBottom));
    setColour (juce::PopupMenu::highlightedTextColourId, Colour (cream));
    setColour (juce::TextButton::textColourOffId, Colour (cream));
    setColour (juce::TextButton::textColourOnId, Colour (cream));
    setColour (juce::BubbleComponent::backgroundColourId, Colour (0xf0101113));
    setColour (juce::BubbleComponent::outlineColourId, Colour (0xff8a9294));
    setColour (juce::TooltipWindow::textColourId, Colour (cream));
}

void FullteqLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto c = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const auto style = slider.getProperties()["style"].toString();
    const float size = (float) slider.getProperties()["radius"];

    if (style == "chicken")      drawChickenHead (g, c, size, angle);
    else if (style == "bar")     drawBarKnob (g, c, size, angle);
    else if (style == "trim")    drawSkirtedKnob (g, c, size, angle, 8);
    else                         drawSkirtedKnob (g, c, size, angle, 10);
}

void FullteqLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const Colour&, bool highlighted, bool down)
{
    // Black bakelite push button.
    const auto b = button.getLocalBounds().toFloat().reduced (2.0f, 2.5f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (b.translated (0.0f, down ? 0.6f : 2.0f), 3.0f);
    const auto face = b.translated (0.0f, down ? 0.8f : 0.0f);
    g.setGradientFill (ColourGradient (down ? Colour (0xff0b0b0c) : Colour (0xff34363a).brighter (highlighted ? 0.12f : 0.0f), 0.0f, face.getY(),
                                       down ? Colour (0xff1f2023) : Colour (0xff0a0a0b), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, 3.0f);
    if (! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawLine (face.getX() + 3.0f, face.getY() + 1.0f, face.getRight() - 3.0f, face.getY() + 1.0f, 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (face, 3.0f, 1.0f);
}

void FullteqLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const auto c = button.getLocalBounds().toFloat().getCentre().translated (0.0f, down ? 0.8f : -0.5f);
    const float dir = button.getButtonText() == "<" ? -1.0f : 1.0f;
    juce::Path arrow;
    arrow.addTriangle (c.x + dir * 5.0f, c.y, c.x - dir * 4.0f, c.y - 5.0f, c.x - dir * 4.0f, c.y + 5.0f);
    g.setColour (Colour (cream).withAlpha (0.85f));
    g.fillPath (arrow);
}

void FullteqLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    const auto c = button.getLocalBounds().toFloat().getCentre();
    if (button.getProperties()["style"].toString() == "power")
    {
        // Bypass parameter: false = unit ON, pointer swings to ON.
        const bool unitOn = ! button.getToggleState();
        drawPointerKnob (g, c, 15.0f, unitOn ? kPi * 0.22f : -kPi * 0.22f);
    }
    else
    {
        drawBatToggle (g, c, button.getToggleState());
    }
}

void FullteqLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&)
{
    // Engraved black name plate.
    const auto b = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), 3.0f);
    g.setGradientFill (ColourGradient (Colour (0xff25272a), 0.0f, b.getY(), Colour (0xff060607), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (b.getX() + 3.0f, b.getY() + 1.0f, b.getRight() - 3.0f, b.getY() + 1.0f, 1.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    juce::Path arrow;
    const float ax = b.getRight() - 13.0f, ay = b.getCentreY();
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Colour (cream).withAlpha (0.7f));
    g.fillPath (arrow);
}

void FullteqLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font FullteqLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return silkFont (13.0f); }
juce::Font FullteqLookAndFeel::getPopupMenuFont()                 { return silkFont (14.0f); }
juce::Font FullteqLookAndFeel::getSliderPopupFont (juce::Slider&) { return silkFont (13.0f); }

//==============================================================================
void FullteqResponseCurve::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setGradientFill (ColourGradient (Colour (0xff10242a), 0.0f, b.getY(), Colour (0xff061013), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 2.0f);

    auto freqToX = [&] (double hz)
    {
        return b.getX() + (float) ((std::log10 (hz) - std::log10 (minHz)) / (std::log10 (maxHz) - std::log10 (minHz))) * b.getWidth();
    };
    auto dbToY = [&] (float db)
    {
        return juce::jmap (juce::jlimit (-rangeDb, rangeDb, db), -rangeDb, rangeDb, b.getBottom() - 12.0f, b.getY() + 6.0f);
    };

    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const bool major = hz == 100.0 || hz == 1000.0 || hz == 10000.0;
        g.setColour (Colour (cream).withAlpha (major ? 0.13f : 0.06f));
        g.drawVerticalLine ((int) freqToX (hz), b.getY() + 2.0f, b.getBottom() - 11.0f);
        if (major)
        {
            g.setColour (Colour (cream).withAlpha (0.45f));
            g.setFont (silkFont (9.0f));
            g.drawText (hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0),
                        Rectangle<float> (30.0f, 10.0f).withCentre ({ freqToX (hz), b.getBottom() - 6.0f }), juce::Justification::centred, false);
        }
    }
    for (float db : { -10.0f, 10.0f })
    {
        g.setColour (Colour (cream).withAlpha (0.06f));
        g.drawHorizontalLine ((int) dbToY (db), b.getX() + 2.0f, b.getRight() - 2.0f);
    }
    g.setColour (Colour (cream).withAlpha (0.22f));
    g.drawHorizontalLine ((int) dbToY (0.0f), b.getX() + 2.0f, b.getRight() - 2.0f);

    const int numPoints = juce::jmax (2, (int) b.getWidth());
    std::vector<double> frequencies ((size_t) numPoints);
    std::vector<float> magnitudes ((size_t) numPoints);
    for (int i = 0; i < numPoints; ++i)
        frequencies[(size_t) i] = minHz * std::pow (maxHz / minHz, (double) i / (double) (numPoints - 1));
    processorRef.getResponseDb (frequencies.data(), magnitudes.data(), numPoints);

    juce::Path curve;
    for (int i = 0; i < numPoints; ++i)
    {
        const float x = freqToX (frequencies[(size_t) i]);
        const float y = dbToY (magnitudes[(size_t) i]);
        if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }

    const bool bypassed = processorRef.isEffectBypassed();
    const auto lineColour = bypassed ? Colour (0xff7d8a8c) : Colour (amber);

    juce::Path fill (curve);
    fill.lineTo (freqToX (maxHz), dbToY (0.0f));
    fill.lineTo (freqToX (minHz), dbToY (0.0f));
    fill.closeSubPath();

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (b.reduced (1.0f).toNearestInt());
        g.setColour (lineColour.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (lineColour.withAlpha (bypassed ? 0.08f : 0.22f));
        g.strokePath (curve, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (lineColour);
        g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Recess: inner shadow from the bezel lip, then a glass reflection.
    g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.65f), 0.0f, b.getY(), juce::Colours::transparentBlack, 0.0f, b.getY() + 12.0f, false));
    g.fillRect (b.withHeight (12.0f));
    g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.45f), b.getX(), 0.0f, juce::Colours::transparentBlack, b.getX() + 8.0f, 0.0f, false));
    g.fillRect (b.withWidth (8.0f));

    juce::Path glass;
    glass.startNewSubPath (b.getX(), b.getY());
    glass.lineTo (b.getX() + b.getWidth() * 0.62f, b.getY());
    glass.lineTo (b.getX() + b.getWidth() * 0.38f, b.getBottom());
    glass.lineTo (b.getX(), b.getBottom());
    glass.closeSubPath();
    g.setGradientFill (ColourGradient (juce::Colours::white.withAlpha (0.07f), b.getX(), b.getY(), juce::Colours::white.withAlpha (0.0f), b.getX() + b.getWidth() * 0.5f, b.getBottom(), false));
    g.fillPath (glass);
}

//==============================================================================
FullteqAudioProcessorEditor::FullteqAudioProcessorEditor (FullteqAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), responseCurve (p)
{
    setLookAndFeel (&lookAndFeel);
    noiseTile = makeNoiseTile();

    for (auto* knob : { &lowBoost, &lowAtten, &highBoost, &highAtten, &bandwidth })
        configureKnob (*knob, "pultec", bigR, knobStart, knobEnd);
    for (auto* knob : { &inputGain, &drive, &outputGain })
        configureKnob (*knob, "trim", trimR, knobStart, knobEnd);
    configureKnob (lowFreq, "chicken", cpsS, deg (-60.0f), deg (60.0f));
    configureKnob (highFreq, "bar", kcsS, deg (-105.0f), deg (105.0f));
    configureKnob (attenSel, "chicken", attenS, deg (-40.0f), deg (40.0f));

    configureToggle (inButton, "bat");
    configureToggle (tubeButton, "bat");
    configureToggle (transformerButton, "bat");
    configureToggle (bypassButton, "power");

    inButton.setTooltip ("IN: route the signal through the equalizer");
    tubeButton.setTooltip ("Tube amplifier stage (2nd and 3rd harmonics)");
    transformerButton.setTooltip ("Transformer low-frequency core saturation");
    bypassButton.setTooltip ("OFF bypasses the whole unit");

    addAndMakeVisible (responseCurve);

    auto& state = processorRef.apvts;
    lowBoostA = std::make_unique<SliderAttachment> (state, "lowBoost", lowBoost);
    lowAttenA = std::make_unique<SliderAttachment> (state, "lowAtten", lowAtten);
    highBoostA = std::make_unique<SliderAttachment> (state, "highBoost", highBoost);
    highAttenA = std::make_unique<SliderAttachment> (state, "highAtten", highAtten);
    bandwidthA = std::make_unique<SliderAttachment> (state, "bandwidth", bandwidth);
    inputGainA = std::make_unique<SliderAttachment> (state, "inputGain", inputGain);
    driveA = std::make_unique<SliderAttachment> (state, "drive", drive);
    outputGainA = std::make_unique<SliderAttachment> (state, "outputGain", outputGain);
    // Choice parameters attach as stepped sliders (range 0..n-1, interval 1).
    lowFreqA = std::make_unique<SliderAttachment> (state, "lowFreq", lowFreq);
    highFreqA = std::make_unique<SliderAttachment> (state, "highFreq", highFreq);
    attenSelA = std::make_unique<SliderAttachment> (state, "attenSel", attenSel);
    inA = std::make_unique<ButtonAttachment> (state, "input", inButton);
    tubeA = std::make_unique<ButtonAttachment> (state, "tube", tubeButton);
    transformerA = std::make_unique<ButtonAttachment> (state, "transformer", transformerButton);
    bypassA = std::make_unique<ButtonAttachment> (state, "bypass", bypassButton);

    for (auto* name : FullteqAudioProcessor::presetNames)
        presetBox.addItem (name, presetBox.getNumItems() + 1);
    presetBox.setSelectedId (processorRef.getCurrentPresetIndex() + 1, juce::dontSendNotification);
    presetBox.setJustificationType (juce::Justification::centredLeft);
    presetBox.onChange = [this] { processorRef.loadPreset (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    previousPreset.onClick = [this]
    {
        const int numPresets = (int) FullteqAudioProcessor::presetNames.size();
        processorRef.loadPreset ((processorRef.getCurrentPresetIndex() + numPresets - 1) % numPresets);
        presetBox.setSelectedId (processorRef.getCurrentPresetIndex() + 1, juce::dontSendNotification);
    };
    nextPreset.onClick = [this]
    {
        const int numPresets = (int) FullteqAudioProcessor::presetNames.size();
        processorRef.loadPreset ((processorRef.getCurrentPresetIndex() + 1) % numPresets);
        presetBox.setSelectedId (processorRef.getCurrentPresetIndex() + 1, juce::dontSendNotification);
    };
    addAndMakeVisible (previousPreset);
    addAndMakeVisible (nextPreset);

    setSize (editorWidth, editorHeight);
    startTimerHz (20);
}

FullteqAudioProcessorEditor::~FullteqAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void FullteqAudioProcessorEditor::configureKnob (juce::Slider& slider, const char* style, float radius, float startAngle, float endAngle)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (startAngle, endAngle, true);
    slider.setPopupDisplayEnabled (true, true, this, 1500);
    slider.getProperties().set ("style", style);
    slider.getProperties().set ("radius", radius);
    addAndMakeVisible (slider);
}

void FullteqAudioProcessorEditor::configureToggle (juce::ToggleButton& button, const char* style)
{
    button.setClickingTogglesState (true);
    button.getProperties().set ("style", style);
    addAndMakeVisible (button);
}

void FullteqAudioProcessorEditor::renderStaticLayer (float scale)
{
    staticScale = scale;
    staticLayer = juce::Image (juce::Image::RGB, juce::roundToInt ((float) getWidth() * scale),
                               juce::roundToInt ((float) getHeight() * scale), true);
    juce::Graphics sg (staticLayer);
    sg.addTransform (juce::AffineTransform::scale (scale));
    paintStatic (sg);
}

void FullteqAudioProcessorEditor::paintStatic (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff1c1e20), 0.0f, 0.0f, Colour (0xff08090a), 0.0f, full.getBottom(), false));
    g.fillAll();

    // Painted steel front panel with a soft overhead light, vignette and paint grain.
    const auto panel = full.reduced (8.0f);
    juce::Path panelPath;
    panelPath.addRoundedRectangle (panel, 3.0f);
    shadowFor (g, panelPath, 0.8f, 22, { 0, 9 });

    g.setGradientFill (ColourGradient (Colour (panelTop), 0.0f, panel.getY(), Colour (panelBottom), 0.0f, panel.getBottom(), false));
    g.fillPath (panelPath);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (panelPath);
        const auto lightC = Point<float> (panel.getX() + panel.getWidth() * 0.38f, panel.getY() - 40.0f);
        g.setGradientFill (radial (juce::Colours::white.withAlpha (0.09f), lightC, juce::Colours::transparentWhite, panel.getWidth() * 0.55f));
        g.fillPath (panelPath);
        ColourGradient vignette (juce::Colours::transparentBlack, panel.getCentre(), juce::Colours::black.withAlpha (0.24f),
                                 panel.getCentre().translated (panel.getWidth() * 0.56f, 0.0f), true);
        vignette.addColour (0.55, juce::Colours::transparentBlack);
        g.setGradientFill (vignette);
        g.fillPath (panelPath);
        g.setTiledImageFill (noiseTile, 0, 0, 1.0f);
        g.fillPath (panelPath);
    }

    // Folded-edge bevel.
    g.setColour (juce::Colours::white.withAlpha (0.24f));
    g.drawLine (panel.getX() + 3.0f, panel.getY() + 0.8f, panel.getRight() - 3.0f, panel.getY() + 0.8f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (panel.getX() + 0.8f, panel.getY() + 3.0f, panel.getX() + 0.8f, panel.getBottom() - 3.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawLine (panel.getX() + 3.0f, panel.getBottom() - 0.8f, panel.getRight() - 3.0f, panel.getBottom() - 0.8f, 1.4f);
    g.drawLine (panel.getRight() - 0.8f, panel.getY() + 3.0f, panel.getRight() - 0.8f, panel.getBottom() - 3.0f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.strokePath (panelPath, juce::PathStrokeType (1.0f));

    // Rack-ear slots cut into both edges, and slotted screws.
    for (float y : { 150.0f, 290.0f })
        for (bool left : { true, false })
        {
            const Rectangle<float> slot (left ? panel.getX() - 8.0f : panel.getRight() - 34.0f, y - 9.0f, 42.0f, 18.0f);
            juce::Path slotPath;
            slotPath.addRoundedRectangle (slot, 9.0f);
            g.setColour (Colour (0xff0b0c0d));
            g.fillPath (slotPath);
            g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.7f), 0.0f, slot.getY(), juce::Colours::transparentBlack, 0.0f, slot.getY() + 7.0f, false));
            g.fillPath (slotPath);
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (panelPath);
            g.setGradientFill (ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, slot.getY(), juce::Colours::white.withAlpha (0.22f), 0.0f, slot.getBottom(), false));
            g.strokePath (slotPath, juce::PathStrokeType (1.0f));
        }

    int screwIndex = 0;
    for (auto p : { Point<float> (38.0f, 70.0f), Point<float> (38.0f, 370.0f),
                    Point<float> (1282.0f, 70.0f), Point<float> (1282.0f, 370.0f) })
        drawScrew (g, p, 7.0f, 0.35f + 0.9f * (float) screwIndex++);

    // Title block.
    silk (g, "FULLTEQ", { 70.0f, 156.0f, 190.0f, 22.0f }, 17.0f);
    silk (g, "PROGRAM EQUALIZER", { 70.0f, 180.0f, 190.0f, 20.0f }, 14.0f);
    silk (g, "FQ-1A", { 70.0f, 202.0f, 190.0f, 20.0f }, 14.0f);

    // Main knob scales and captions.
    for (auto [c, caption] : { std::pair<Point<float>, const char*> { lowBoostC, "BOOST" }, { lowAttenC, "ATTEN" },
                               { highBoostC, "BOOST" }, { highAttenC, "ATTEN" } })
    {
        silkAt (g, caption, c.translated (0.0f, -88.0f), 14.0f);
        drawKnobNumerals (g, c, bigR + 18.0f);
    }

    drawKnobNumerals (g, bandwidthC, bigR + 18.0f);
    silkAt (g, "SHARP", polar (bandwidthC, knobStart, bigR + 18.0f).translated (-16.0f, 14.0f), 8.0f);
    silkAt (g, "BROAD", polar (bandwidthC, knobEnd, bigR + 18.0f).translated (16.0f, 14.0f), 8.0f);
    silkAt (g, "BANDWIDTH", bandwidthC.translated (0.0f, 75.0f), 14.0f);

    // Stepped selectors.
    silkAt (g, "CPS", cpsC.translated (0.0f, -80.0f), 14.0f);
    drawSelectorLabels (g, cpsC, cpsS + 20.0f, deg (-60.0f), deg (60.0f), FullteqAudioProcessor::lowFrequencies, 1.0f);
    silk (g, "LOW FREQUENCY", Rectangle<float> (160.0f, 20.0f).withCentre (cpsC.translated (0.0f, 75.0f)), 14.0f);

    silkAt (g, "KCS", kcsC.translated (0.0f, -82.0f), 14.0f);
    drawSelectorLabels (g, kcsC, kcsS + 20.0f, deg (-105.0f), deg (105.0f), FullteqAudioProcessor::highFrequencies, 1000.0f);
    silk (g, "HIGH FREQUENCY", Rectangle<float> (160.0f, 20.0f).withCentre (kcsC.translated (0.0f, 75.0f)), 14.0f);

    silk (g, "ATTEN SEL", Rectangle<float> (120.0f, 20.0f).withCentre (attenSelC.translated (0.0f, -91.0f)), 14.0f);
    drawSelectorLabels (g, attenSelC, attenS + 20.0f, deg (-40.0f), deg (40.0f), FullteqAudioProcessor::attenuationFrequencies, 1000.0f);

    silkAt (g, "OFF", polar (powerC, -kPi * 0.22f, 36.0f), 13.0f);
    silkAt (g, "ON", polar (powerC, kPi * 0.22f, 36.0f), 13.0f);

    // Left column: drive trims and switches.
    for (auto [c, caption] : { std::pair<Point<float>, const char*> { inputC, "INPUT" }, { driveC, "DRIVE" }, { outputC, "OUTPUT" } })
    {
        silkAt (g, caption, c.translated (0.0f, -36.0f), 10.0f);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = knobStart + (knobEnd - knobStart) * (float) i / 10.0f;
            g.setColour (Colour (silkColour).withAlpha (0.85f));
            g.drawLine ({ polar (c, a, trimR + 4.0f), polar (c, a, trimR + (i % 5 == 0 ? 9.0f : 7.0f)) }, i % 5 == 0 ? 1.4f : 0.9f);
        }
    }
    for (auto [c, caption] : { std::pair<Point<float>, const char*> { inC, "IN" }, { tubeC, "TUBE" }, { xfmrC, "XFMR" } })
        silkAt (g, caption, c.translated (0.0f, -50.0f), 13.0f);

    // Response window bezel.
    {
        juce::Path bezel;
        bezel.addRoundedRectangle (graphBezel, 5.0f);
        shadowFor (g, bezel, 0.65f, 9, { 0, 4 });
        g.setGradientFill (ColourGradient (Colour (0xff2e3034), 0.0f, graphBezel.getY(), Colour (0xff0b0c0d), 0.0f, graphBezel.getBottom(), false));
        g.fillPath (bezel);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawLine (graphBezel.getX() + 4.0f, graphBezel.getY() + 1.0f, graphBezel.getRight() - 4.0f, graphBezel.getY() + 1.0f, 1.0f);
        g.setColour (juce::Colours::black);
        g.strokePath (bezel, juce::PathStrokeType (1.0f));
        const auto screen = graphBezel.reduced (10.0f);
        g.setGradientFill (ColourGradient (juce::Colours::black, 0.0f, screen.getY() - 2.0f, Colour (0xff3a3c40), 0.0f, screen.getBottom() + 2.0f, false));
        g.fillRoundedRectangle (screen.expanded (2.0f), 3.0f);
        for (auto p : { graphBezel.getTopLeft().translated (5.0f, 5.0f), graphBezel.getTopRight().translated (-5.0f, 5.0f),
                        graphBezel.getBottomLeft().translated (5.0f, -5.0f), graphBezel.getBottomRight().translated (-5.0f, -5.0f) })
            drawScrew (g, p, 2.6f, 0.8f);
    }
    silk (g, "RESPONSE", Rectangle<float> (graphBezel.getX(), graphBezel.getBottom() + 4.0f, graphBezel.getWidth(), 14.0f), 10.0f);

    // Brass serial plate.
    {
        const Rectangle<float> plate (1070.0f, 300.0f, 160.0f, 76.0f);
        juce::Path platePath;
        platePath.addRoundedRectangle (plate, 3.0f);
        shadowFor (g, platePath, 0.55f, 5, { 0, 2 });
        ColourGradient brass (Colour (0xffd6b977), plate.getTopLeft(), Colour (0xff7e6131), plate.getBottomRight(), false);
        brass.addColour (0.45, Colour (0xffb8975a));
        g.setGradientFill (brass);
        g.fillPath (platePath);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (platePath, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (plate.reduced (5.0f), 2.0f, 0.8f);
        for (auto p : { plate.getTopLeft().translated (9.0f, 9.0f), plate.getTopRight().translated (-9.0f, 9.0f),
                        plate.getBottomLeft().translated (9.0f, -9.0f), plate.getBottomRight().translated (-9.0f, -9.0f) })
        {
            g.setGradientFill (radial (Colour (0xfff0dca8), p.translated (-0.8f, -0.8f), Colour (0xff6b5228), 2.6f));
            g.fillEllipse (p.x - 2.2f, p.y - 2.2f, 4.4f, 4.4f);
        }
        auto engrave = [&] (const juce::String& text, float y, float size)
        {
            const auto area = Rectangle<float> (plate.getX(), y, plate.getWidth(), size + 4.0f);
            g.setFont (silkFont (size));
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (text, area.translated (0.0f, 0.8f), juce::Justification::centred, false);
            g.setColour (Colour (0xff3a2a10));
            g.drawText (text, area, juce::Justification::centred, false);
        };
        engrave ("FULLTEQ AUDIO", plate.getY() + 12.0f, 12.0f);
        engrave ("MODEL FQ-1A", plate.getY() + 31.0f, 10.0f);
        engrave ("SERIAL No. 0451", plate.getY() + 47.0f, 10.0f);
    }
}

void FullteqAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (staticLayer.isNull() || std::abs (scale - staticScale) > 0.01f)
        renderStaticLayer (scale);

    g.drawImageTransformed (staticLayer, juce::AffineTransform::scale (1.0f / staticScale));

    // Pilot lamp: lit while the equalizer is in circuit.
    drawJewelLamp (g, lampC, 12.0f, ! processorRef.isEffectBypassed());
}

void FullteqAudioProcessorEditor::resized()
{
    staticLayer = {};

    auto place = [] (juce::Component& comp, Point<float> centre, int w, int h)
    {
        comp.setBounds (Rectangle<int> (w, h).withCentre (centre.toInt()));
    };

    for (auto [knob, c] : { std::pair<juce::Slider*, Point<float>> { &lowBoost, lowBoostC }, { &lowAtten, lowAttenC },
                            { &highBoost, highBoostC }, { &highAtten, highAttenC }, { &bandwidth, bandwidthC } })
        place (*knob, c, 118, 118);
    for (auto [knob, c] : { std::pair<juce::Slider*, Point<float>> { &inputGain, inputC }, { &drive, driveC }, { &outputGain, outputC } })
        place (*knob, c, 50, 50);

    place (lowFreq, cpsC, 104, 104);
    place (highFreq, kcsC, 116, 116);
    place (attenSel, attenSelC, 92, 92);

    place (inButton, inC, 50, 84);
    place (tubeButton, tubeC, 50, 84);
    place (transformerButton, xfmrC, 50, 84);
    place (bypassButton, powerC, 64, 64);
    lampBounds = Rectangle<int> (100, 100).withCentre (lampC.toInt());

    responseCurve.setBounds (graphBezel.reduced (10.0f).toNearestInt());
    presetBox.setBounds (1050, 202, 200, 28);
    previousPreset.setBounds (1050, 238, 98, 26);
    nextPreset.setBounds (1152, 238, 98, 26);
}

void FullteqAudioProcessorEditor::timerCallback()
{
    // Keep the preset box in sync when the host changes the program.
    const int expectedId = processorRef.getCurrentPresetIndex() + 1;
    if (presetBox.getSelectedId() != expectedId)
        presetBox.setSelectedId (expectedId, juce::dontSendNotification);

    responseCurve.repaint();
    repaint (lampBounds);
}
