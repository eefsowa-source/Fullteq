#include "PluginEditor.h"

#include <cmath>

namespace
{
    constexpr juce::uint32 bluePanel = 0xff19536a;
    constexpr juce::uint32 blueEdge  = 0xff0b2f40;
    constexpr juce::uint32 blueDeep  = 0xff0d3447;
    constexpr juce::uint32 cream     = 0xfff0e8d2;
    constexpr juce::uint32 creamDim  = 0xffb9c4c4;
    constexpr juce::uint32 black     = 0xff101214;
    constexpr juce::uint32 chrome    = 0xffb4b8b8;
    constexpr juce::uint32 orange    = 0xffe45b37;
    constexpr juce::uint32 alarm     = 0xffc23a2b;

    constexpr float kPi = juce::MathConstants<float>::pi;

    juce::Font makeFont (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, float size = 11.0f, bool bold = true)
    {
        g.setColour (juce::Colour (cream));
        g.setFont (makeFont (size, bold));
        g.drawText (text, area, juce::Justification::centred, false);
    }

    void drawScrew (juce::Graphics& g, juce::Point<float> centre)
    {
        g.setColour (juce::Colour (0x66000000));
        g.fillEllipse (centre.x - 7.0f, centre.y - 6.0f, 14.0f, 14.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e8e8), centre.x - 4.0f, centre.y - 5.0f,
                                                 juce::Colour (0xff80868a), centre.x + 5.0f, centre.y + 6.0f, true));
        g.fillEllipse (centre.x - 6.0f, centre.y - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colour (0xff565b5e));
        g.drawLine (centre.x - 4.0f, centre.y + 1.0f, centre.x + 4.0f, centre.y - 1.0f, 1.6f);
    }
}

FullteqLookAndFeel::FullteqLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, juce::Colour (cream));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (black));
    setColour (juce::ComboBox::textColourId, juce::Colour (cream));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (chrome));
    setColour (juce::ComboBox::arrowColourId, juce::Colour (cream));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (black));
    setColour (juce::PopupMenu::textColourId, juce::Colour (cream));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (bluePanel));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (cream));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff123c4d));
    setColour (juce::TextButton::textColourOffId, juce::Colour (cream));
    setColour (juce::TextButton::textColourOnId, juce::Colour (cream));
}

void FullteqLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float startAngle, float endAngle, juce::Slider&)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto centre = area.getCentre();
    const float outer = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const bool large = outer > 50.0f;

    // JUCE measures rotary angles clockwise from 12 o'clock; trigonometry wants them from 3 o'clock.
    auto pointAt = [&] (float angle, float radius)
    {
        return centre + juce::Point<float> (std::cos (angle - kPi * 0.5f), std::sin (angle - kPi * 0.5f)) * radius;
    };

    const float bodyR = outer * (large ? 0.56f : 0.62f);
    const float ringR = outer * (large ? 0.66f : 0.78f);
    const float tickIn = outer * (large ? 0.72f : 0.84f);
    const float tickOut = outer * (large ? 0.80f : 0.96f);
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Scale: 11 ticks, numerals 0..10 on the large knobs like the printed panel.
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
        const auto p1 = pointAt (a, tickIn);
        const auto p2 = pointAt (a, tickOut);
        g.setColour (juce::Colour (cream).withAlpha (0.85f));
        g.drawLine (p1.x, p1.y, p2.x, p2.y, (i % 5 == 0) ? 1.8f : 1.0f);

        if (large)
        {
            g.setFont (makeFont (11.0f));
            g.drawText (juce::String (i), juce::Rectangle<int> (20, 14).withCentre (pointAt (a, outer * 0.90f).toInt()),
                        juce::Justification::centred, false);
        }
    }

    g.setColour (juce::Colour (0x66000000));
    g.fillEllipse (centre.x - ringR + 2.0f, centre.y - ringR + 3.0f, ringR * 2.0f, ringR * 2.0f);

    // Polished ring, fluted skirt, black bakelite body.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9ecec), centre.x - ringR, centre.y - ringR,
                                             juce::Colour (0xff6c7274), centre.x + ringR, centre.y + ringR, true));
    g.fillEllipse (centre.x - ringR, centre.y - ringR, ringR * 2.0f, ringR * 2.0f);

    g.setColour (juce::Colour (0xff0c0d0e));
    g.fillEllipse (centre.x - ringR + 3.0f, centre.y - ringR + 3.0f, (ringR - 3.0f) * 2.0f, (ringR - 3.0f) * 2.0f);

    const int flutes = large ? 18 : 12;
    for (int i = 0; i < flutes; ++i)
    {
        const float a = 2.0f * kPi * (float) i / (float) flutes;
        const auto p1 = pointAt (a, bodyR + 1.0f);
        const auto p2 = pointAt (a, ringR - 4.0f);
        g.setColour (juce::Colour (0xff2b2e30));
        g.drawLine (p1.x, p1.y, p2.x, p2.y, large ? 2.6f : 2.0f);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e52), centre.x - bodyR * 0.4f, centre.y - bodyR * 0.5f,
                                             juce::Colour (0xff08090a), centre.x + bodyR * 0.6f, centre.y + bodyR * 0.7f, true));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    g.setColour (juce::Colour (0xff1c1e20));
    g.drawEllipse (centre.x - bodyR * 0.62f, centre.y - bodyR * 0.62f, bodyR * 1.24f, bodyR * 1.24f, 1.0f);

    // Indicator line, running from the rim of the body toward the skirt like the original pointer knobs.
    const auto tip = pointAt (angle, ringR - 2.0f);
    const auto base = pointAt (angle, bodyR * 0.55f);
    g.setColour (juce::Colour (cream));
    g.drawLine (base.x, base.y, tip.x, tip.y, large ? 2.6f : 2.0f);
}

void FullteqLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool)
{
    const auto b = button.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff123c4d).brighter (highlighted ? 0.12f : 0.0f));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (chrome));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
}

void FullteqLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto b = button.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = button.getToggleState();
    const auto litColour = button.findColour (juce::TextButton::buttonOnColourId);

    g.setColour (juce::Colour (0x55000000));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), 4.0f);
    g.setColour ((on ? litColour : juce::Colour (0xff123c4d)).brighter (highlighted ? 0.1f : 0.0f));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (chrome));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);

    g.setColour (juce::Colour (cream).withAlpha (on ? 1.0f : 0.8f));
    g.setFont (makeFont (11.0f, true));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void FullteqResponseCurve::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setColour (juce::Colour (blueDeep));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colour (0xff06202c));
    g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

    auto freqToX = [&] (double hz)
    {
        return b.getX() + (float) ((std::log10 (hz) - std::log10 (minHz)) / (std::log10 (maxHz) - std::log10 (minHz))) * b.getWidth();
    };
    auto dbToY = [&] (float db)
    {
        return juce::jmap (juce::jlimit (-rangeDb, rangeDb, db), -rangeDb, rangeDb, b.getBottom() - 3.0f, b.getY() + 3.0f);
    };

    g.setFont (makeFont (9.0f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = freqToX (hz);
        g.setColour (juce::Colour (cream).withAlpha (0.10f));
        g.drawVerticalLine ((int) x, b.getY() + 2.0f, b.getBottom() - 2.0f);
        g.setColour (juce::Colour (cream).withAlpha (0.45f));
        const juce::String label = hz >= 1000.0 ? juce::String (hz / 1000.0, 0) + "k" : juce::String (hz, 0);
        g.drawText (label, juce::Rectangle<int> (30, 10).withCentre ({ (int) x + 1, (int) b.getBottom() - 7 }), juce::Justification::centred, false);
    }

    for (float db : { -10.0f, 10.0f })
    {
        g.setColour (juce::Colour (cream).withAlpha (0.10f));
        g.drawHorizontalLine ((int) dbToY (db), b.getX() + 2.0f, b.getRight() - 2.0f);
    }
    g.setColour (juce::Colour (cream).withAlpha (0.35f));
    g.drawHorizontalLine ((int) dbToY (0.0f), b.getX() + 2.0f, b.getRight() - 2.0f);

    const int numPoints = juce::jmax (2, (int) b.getWidth() / 2);
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
    const auto lineColour = bypassed ? juce::Colour (0xff7d8a8c) : juce::Colour (orange);

    juce::Path fill (curve);
    fill.lineTo (freqToX (maxHz), dbToY (0.0f));
    fill.lineTo (freqToX (minHz), dbToY (0.0f));
    fill.closeSubPath();

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (b.reduced (1.0f).toNearestInt());
    g.setColour (lineColour.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (lineColour);
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
}

FullteqAudioProcessorEditor::FullteqAudioProcessorEditor (FullteqAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), responseCurve (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (1000, 470);

    for (auto* knob : { &lowBoost, &lowAtten, &highBoost, &highAtten, &bandwidth })
        configureKnob (*knob, 80);
    for (auto* knob : { &inputGain, &drive, &outputGain })
        configureKnob (*knob, 66);
    configureSelector (lowFreq, "lowFreq");
    configureSelector (highFreq, "highFreq");
    configureSelector (attenSel, "attenSel");
    for (auto* toggle : { &inButton, &tubeButton, &transformerButton, &bypassButton })
        configureToggle (*toggle);

    for (auto* toggle : { &inButton, &tubeButton, &transformerButton })
        toggle->setColour (juce::TextButton::buttonOnColourId, juce::Colour (orange));
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (alarm));

    inButton.setTooltip ("IN: route the signal through the equalizer");
    tubeButton.setTooltip ("Tube amplifier stage (2nd and 3rd harmonics)");
    transformerButton.setTooltip ("Transformer low-frequency core saturation");
    bypassButton.setTooltip ("Bypass the whole unit");

    addAndMakeVisible (responseCurve);

    lowBoostA = std::make_unique<SliderAttachment> (processorRef.apvts, "lowBoost", lowBoost);
    lowAttenA = std::make_unique<SliderAttachment> (processorRef.apvts, "lowAtten", lowAtten);
    highBoostA = std::make_unique<SliderAttachment> (processorRef.apvts, "highBoost", highBoost);
    highAttenA = std::make_unique<SliderAttachment> (processorRef.apvts, "highAtten", highAtten);
    bandwidthA = std::make_unique<SliderAttachment> (processorRef.apvts, "bandwidth", bandwidth);
    inputGainA = std::make_unique<SliderAttachment> (processorRef.apvts, "inputGain", inputGain);
    driveA = std::make_unique<SliderAttachment> (processorRef.apvts, "drive", drive);
    outputGainA = std::make_unique<SliderAttachment> (processorRef.apvts, "outputGain", outputGain);
    lowFreqA = std::make_unique<ComboBoxAttachment> (processorRef.apvts, "lowFreq", lowFreq);
    highFreqA = std::make_unique<ComboBoxAttachment> (processorRef.apvts, "highFreq", highFreq);
    attenSelA = std::make_unique<ComboBoxAttachment> (processorRef.apvts, "attenSel", attenSel);
    inA = std::make_unique<ButtonAttachment> (processorRef.apvts, "input", inButton);
    tubeA = std::make_unique<ButtonAttachment> (processorRef.apvts, "tube", tubeButton);
    transformerA = std::make_unique<ButtonAttachment> (processorRef.apvts, "transformer", transformerButton);
    bypassA = std::make_unique<ButtonAttachment> (processorRef.apvts, "bypass", bypassButton);

    for (auto* name : FullteqAudioProcessor::presetNames)
        presetBox.addItem (name, presetBox.getNumItems() + 1);
    presetBox.setSelectedId (processorRef.getCurrentPresetIndex() + 1, juce::dontSendNotification);
    presetBox.setJustificationType (juce::Justification::centred);
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

    startTimerHz (20);
}

FullteqAudioProcessorEditor::~FullteqAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void FullteqAudioProcessorEditor::configureKnob (juce::Slider& slider, int textBoxWidth)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, 16);
    addAndMakeVisible (slider);
}

void FullteqAudioProcessorEditor::configureSelector (juce::ComboBox& box, const char* parameterId)
{
    // ComboBoxAttachment does not create items: fill them from the choice parameter first.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter (parameterId)))
        box.addItemList (choice->choices, 1);

    box.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (box);
}

void FullteqAudioProcessorEditor::configureToggle (juce::ToggleButton& button)
{
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);
}

void FullteqAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff071b25));

    const auto panel = getLocalBounds().toFloat().reduced (12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (bluePanel).brighter (0.06f), panel.getX(), panel.getY(),
                                             juce::Colour (bluePanel).darker (0.12f), panel.getX(), panel.getBottom(), false));
    g.fillRoundedRectangle (panel, 4.0f);
    g.setColour (juce::Colour (blueEdge));
    g.drawRoundedRectangle (panel, 4.0f, 2.0f);

    // Rack ears: mounting screws and slots.
    for (auto p : { juce::Point<float> (34.0f, 34.0f), juce::Point<float> (966.0f, 34.0f),
                    juce::Point<float> (34.0f, 436.0f), juce::Point<float> (966.0f, 436.0f) })
        drawScrew (g, p);

    // Title block.
    g.setColour (juce::Colour (cream));
    g.setFont (makeFont (26.0f, true));
    g.drawText ("FULLTEQ", 56, 22, 170, 32, juce::Justification::left, false);
    g.setFont (makeFont (11.0f));
    g.setColour (juce::Colour (creamDim));
    g.drawText ("PROGRAM EQUALIZER", 57, 52, 170, 16, juce::Justification::left, false);

    // Top row captions.
    const int captionY = 84;
    for (auto* knob : { &lowBoost, &highBoost })
        drawCaption (g, "BOOST", { knob->getX(), captionY, knob->getWidth(), 16 });
    for (auto* knob : { &lowAtten, &highAtten })
        drawCaption (g, "ATTEN", { knob->getX(), captionY, knob->getWidth(), 16 });

    drawCaption (g, "ATTEN SEL", { attenSel.getX(), attenSel.getY() - 20, attenSel.getWidth(), 16 });

    // Selector captions: CPS / KCS above, LOW FREQUENCY / HIGH FREQUENCY below, BANDWIDTH under the knob.
    drawCaption (g, "CPS", { lowFreq.getX(), lowFreq.getY() - 20, lowFreq.getWidth(), 16 });
    drawCaption (g, "LOW FREQUENCY", { lowFreq.getX() - 20, lowFreq.getBottom() + 6, lowFreq.getWidth() + 40, 16 });
    drawCaption (g, "KCS", { highFreq.getX(), highFreq.getY() - 20, highFreq.getWidth(), 16 });
    drawCaption (g, "HIGH FREQUENCY", { highFreq.getX() - 20, highFreq.getBottom() + 6, highFreq.getWidth() + 40, 16 });
    drawCaption (g, "BANDWIDTH", { bandwidth.getX(), bandwidth.getBottom() + 2, bandwidth.getWidth(), 16 });

    g.setFont (makeFont (8.0f));
    g.setColour (juce::Colour (creamDim));
    g.drawText ("SHARP", bandwidth.getX() + 6, bandwidth.getBottom() - 42, 40, 10, juce::Justification::left, false);
    g.drawText ("BROAD", bandwidth.getRight() - 46, bandwidth.getBottom() - 42, 40, 10, juce::Justification::right, false);

    // Trim section.
    drawCaption (g, "INPUT", { inputGain.getX(), inputGain.getY() - 16, inputGain.getWidth(), 14 }, 10.0f);
    drawCaption (g, "DRIVE", { drive.getX(), drive.getY() - 16, drive.getWidth(), 14 }, 10.0f);
    drawCaption (g, "OUTPUT", { outputGain.getX(), outputGain.getY() - 16, outputGain.getWidth(), 14 }, 10.0f);

    // Pilot lamp: lit while the equalizer is in circuit.
    const bool lit = ! processorRef.isEffectBypassed();
    const auto lamp = lampBounds.toFloat();
    g.setColour (juce::Colour (0xff9aa0a2));
    g.fillEllipse (lamp);
    const auto jewel = lamp.reduced (4.0f);
    if (lit)
    {
        g.setColour (juce::Colour (orange).withAlpha (0.28f));
        g.fillEllipse (lamp.expanded (7.0f));
    }
    g.setGradientFill (juce::ColourGradient (lit ? juce::Colour (0xffff9a7a) : juce::Colour (0xff5a1a14), jewel.getX() + 3.0f, jewel.getY() + 3.0f,
                                             lit ? juce::Colour (0xffb42a14) : juce::Colour (0xff2a0a08), jewel.getRight(), jewel.getBottom(), true));
    g.fillEllipse (jewel);
}

void FullteqAudioProcessorEditor::resized()
{
    responseCurve.setBounds (226, 22, 574, 56);

    presetBox.setBounds (812, 24, 134, 24);
    previousPreset.setBounds (812, 54, 64, 22);
    nextPreset.setBounds (882, 54, 64, 22);

    // Top row: BOOST / ATTEN (low), BOOST / ATTEN (high), ATTEN SEL.
    const int knobY = 102, knobW = 134, knobH = 158;
    lowBoost.setBounds (226, knobY, knobW, knobH);
    lowAtten.setBounds (366, knobY, knobW, knobH);
    highBoost.setBounds (530, knobY, knobW, knobH);
    highAtten.setBounds (670, knobY, knobW, knobH);

    attenSel.setBounds (836, 196, 120, 30);
    lampBounds = { 883, 258, 26, 26 };
    bypassButton.setBounds (836, 340, 120, 30);

    // Bottom row: CPS selector, bandwidth, KCS selector.
    lowFreq.setBounds (232, 332, 122, 30);
    bandwidth.setBounds (396, 282, 150, 152);
    highFreq.setBounds (588, 332, 122, 30);

    // Left column: switches and trims.
    inButton.setBounds (56, 232, 52, 28);
    tubeButton.setBounds (114, 232, 52, 28);
    transformerButton.setBounds (172, 232, 52, 28);

    inputGain.setBounds (46, 316, 56, 92);
    drive.setBounds (104, 316, 56, 92);
    outputGain.setBounds (162, 316, 56, 92);
}

void FullteqAudioProcessorEditor::timerCallback()
{
    // Keep the preset box in sync when the host changes the program.
    const int expectedId = processorRef.getCurrentPresetIndex() + 1;
    if (presetBox.getSelectedId() != expectedId)
        presetBox.setSelectedId (expectedId, juce::dontSendNotification);

    responseCurve.repaint();
    repaint (lampBounds.expanded (10));
}
