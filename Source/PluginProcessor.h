#pragma once

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

/**
 * Single biquad section, transposed direct form II, double precision state.
 * Coefficient layout is normalised so that a0 == 1.
 */
struct FullteqBiquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double s1 = 0.0, s2 = 0.0;

    void reset() noexcept { s1 = 0.0; s2 = 0.0; }

    void setCoefficients (double nb0, double nb1, double nb2, double na1, double na2) noexcept
    {
        b0 = nb0;
        b1 = nb1;
        b2 = nb2;
        a1 = na1;
        a2 = na2;
    }

    /** @see https://shepazu.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html */
    float process (float x) noexcept
    {
        const double in = static_cast<double> (x);
        const double y = b0 * in + s1;
        s1 = b1 * in - a1 * y + s2;
        s2 = b2 * in - a2 * y;
        return static_cast<float> (y);
    }
};

/**
 * Program equaliser modelled as the physical signal chain of a passive
 * tube program EQ:
 *
 *   input gain -> passive EQ network -> tube stage -> transformer -> output
 *
 * Passive network (four cascaded sections, so the controls load each other):
 *   [0] low boost   : low shelf
 *   [1] low atten   : low shelf with a higher corner and gentler slope. Used
 *                     together with [0] at the same dial setting this yields
 *                     the classic "trick": a lift below the corner and a dip
 *                     above it.
 *   [2] high boost  : resonant peak, bandwidth control sweeps SHARP..BROAD
 *   [3] high atten  : high shelf cut at 5 / 10 / 20 kHz
 *
 * Tube and transformer are non-linear, so they run 4x oversampled. The
 * oversampler latency is reported to the host and applied to the dry path so
 * the bypass cross-fade stays phase aligned.
 */
class FullteqAudioProcessor final : public juce::AudioProcessor
{
public:
    FullteqAudioProcessor();
    ~FullteqAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    // -----------------------------------------------------------------
    // Frequency response (passive network only).
    //
    // Computed from the atomic parameter values, so the editor can call it
    // from the message thread without touching audio state.
    // -----------------------------------------------------------------
    struct StageCoefficients
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    struct BandCoefficients { std::array<StageCoefficients, 4> stages {}; };

    static constexpr std::array<float, 4> lowFrequencies { 20.0f, 30.0f, 60.0f, 100.0f };
    static constexpr std::array<float, 7> highFrequencies { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    static constexpr std::array<float, 3> attenuationFrequencies { 5000.0f, 10000.0f, 20000.0f };

    /** Raw value of a choice parameter is already the index; this only clamps it. */
    static int indexFromChoice (float rawValue, int numChoices);

    static BandCoefficients computeBandCoefficients (double sampleRate,
                                                    float lowFrequencyHz, float lowBoostDb, float lowAttenDb, float lowQ,
                                                    float highFrequencyHz, float attenuationFrequencyHz,
                                                    float highBoostDb, float highAttenDb, float bandwidth);
    static double stageMagnitudeLinear (const StageCoefficients& stage, double hz, double sampleRate);

    /** Magnitude of the passive network in dB at each requested frequency. */
    void getResponseDb (const double* frequenciesHz, float* outDb, int numPoints);

    /** True when the plug-in is bypassed (BYPASS on or IN off). */
    bool isEffectBypassed() const;

    // -----------------------------------------------------------------
    // Presets (exposed to the host through the program interface).
    // -----------------------------------------------------------------
    static const std::array<const char*, 8> presetNames;
    void loadPreset (int index);
    int getCurrentPresetIndex() const noexcept { return currentPreset.load (std::memory_order_relaxed); }

    // Metering (linear peak, decaying).
    float getInputLevel() const noexcept { return inputLevel.load (std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return outputLevel.load (std::memory_order_relaxed); }

private:
    struct NonlinearState { double dcIn = 0.0, dcOut = 0.0, transformerLow = 0.0; };

    BandCoefficients makeBand (float lowBoostDb, float lowAttenDb, float lowQ,
                               float highBoostDb, float highAttenDb, float bandwidth) const;
    void applyDryDelay (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples);
    void resetDspState();
    void resizeScratch (int numSamples);

    static constexpr int oversamplingLog2 = 2; // 4x: keeps heavy-drive harmonics from folding back into the audio band
    static constexpr int oversamplingFactor = 1 << oversamplingLog2;

    double currentSampleRate = 44100.0;
    int maxBlockSize = 512;
    int latencySamples = 0;
    bool wasBypassed = false;

    std::vector<std::array<FullteqBiquad, 4>> channelFilters;
    std::vector<NonlinearState> nonlinearStates;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double dcBlockCoeff = 0.999;
    double transformerLowCoeff = 0.02;

    juce::AudioBuffer<float> dryBuffer;
    std::vector<std::vector<float>> dryDelay;
    std::vector<int> dryDelayPos;
    std::vector<float> inGainRamp, outGainRamp, bypassRamp, driveRamp;

    juce::SmoothedValue<float> lowBoostSmoothed, lowAttenSmoothed, lowQSmoothed;
    juce::SmoothedValue<float> highBoostSmoothed, highAttenSmoothed, bandwidthSmoothed;
    juce::SmoothedValue<float> inputGainSmoothed, outputGainSmoothed, driveSmoothed, bypassSmoothed;

    std::atomic<int> currentPreset { 0 };
    std::atomic<float> inputLevel { 0.0f }, outputLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullteqAudioProcessor)
};
