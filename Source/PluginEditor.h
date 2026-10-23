#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class FullteqLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    FullteqLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

/** Log-frequency magnitude plot of the passive EQ network (analytic, no audio-thread access). */
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
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void configureKnob (juce::Slider&, int textBoxWidth);
    void configureSelector (juce::ComboBox&, const char* parameterId);
    void configureToggle (juce::ToggleButton&);
    void timerCallback() override;

    FullteqAudioProcessor& processorRef;
    FullteqLookAndFeel lookAndFeel;
    FullteqResponseCurve responseCurve;

    juce::Slider lowBoost, lowAtten, highBoost, highAtten, bandwidth, inputGain, drive, outputGain;
    juce::ComboBox lowFreq, highFreq, attenSel, presetBox;
    juce::ToggleButton inButton { "IN" }, tubeButton { "TUBE" }, transformerButton { "XFMR" }, bypassButton { "BYPASS" };
    juce::TextButton previousPreset { "<" }, nextPreset { ">" };
    juce::Rectangle<int> lampBounds;

    std::unique_ptr<SliderAttachment> lowBoostA, lowAttenA, highBoostA, highAttenA, bandwidthA, inputGainA, driveA, outputGainA;
    std::unique_ptr<ComboBoxAttachment> lowFreqA, highFreqA, attenSelA;
    std::unique_ptr<ButtonAttachment> inA, tubeA, transformerA, bypassA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullteqAudioProcessorEditor)
};
