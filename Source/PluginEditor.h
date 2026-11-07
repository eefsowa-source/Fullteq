#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

/** Draws the panel hardware: bakelite skirted knobs, chicken-head selectors, bat toggles, push buttons.
    Each slider/button carries a "style" property (and sliders a "radius") that selects the drawing. */
class FullteqLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FullteqLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getSliderPopupFont (juce::Slider&) override;
};

/** Log-frequency magnitude plot of the passive EQ network (analytic, no audio-thread access),
    drawn as a recessed instrument window behind glass. */
class FullteqResponseCurve final : public juce::Component
{
public:
    explicit FullteqResponseCurve (FullteqAudioProcessor& p) : processorRef (p) {}
    void paint (juce::Graphics&) override;

private:
    static constexpr double minHz = 20.0;
    static constexpr double maxHz = 20000.0;
    static constexpr float rangeDb = 15.0f;

    FullteqAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullteqResponseCurve)
};

class FullteqAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FullteqAudioProcessorEditor (FullteqAudioProcessor&);
    ~FullteqAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void configureKnob (juce::Slider&, const char* style, float radius, float startAngle, float endAngle);
    void configureToggle (juce::ToggleButton&, const char* style);
    void renderStaticLayer (float scale);
    void paintStatic (juce::Graphics&);
    void timerCallback() override;

    FullteqAudioProcessor& processorRef;
    FullteqLookAndFeel lookAndFeel;
    FullteqResponseCurve responseCurve;

    // Rotary controls: main knobs, trims, and the stepped selectors (chicken-head / bar knobs).
    juce::Slider lowBoost, lowAtten, highBoost, highAtten, bandwidth, inputGain, drive, outputGain;
    juce::Slider lowFreq, highFreq, attenSel;
    juce::ComboBox presetBox;
    juce::ToggleButton inButton { "IN" }, tubeButton { "TUBE" }, transformerButton { "XFMR" }, bypassButton { "POWER" };
    juce::TextButton previousPreset { "<" }, nextPreset { ">" };
    juce::Rectangle<int> lampBounds;

    juce::Image staticLayer, noiseTile;
    float staticScale = 0.0f;

    std::unique_ptr<SliderAttachment> lowBoostA, lowAttenA, highBoostA, highAttenA, bandwidthA, inputGainA, driveA, outputGainA;
    std::unique_ptr<SliderAttachment> lowFreqA, highFreqA, attenSelA;
    std::unique_ptr<ButtonAttachment> inA, tubeA, transformerA, bypassA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullteqAudioProcessorEditor)
};
