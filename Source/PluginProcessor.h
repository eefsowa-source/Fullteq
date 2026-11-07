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
 * Passive network (five cascaded sections, so the controls load each other):
 *   [0] low boost   : low shelf whose corner is kLowBoostCornerRatio above the
 *                     dial (measured: 300 Hz at the 60 Hz dial, 700 Hz at
 *                     100 Hz), so the lower dials spread over more of the
 *                     bottom end instead of sharpening the corner.
 *   [1] low atten   : low shelf whose corner is kLowAttenCornerRatio above the
 *                     dial (measured: 800 Hz at 60 Hz, 1500 Hz at 100 Hz). Used
 *                     together with [0] at the same dial this yields the
 *                     classic "trick": a lift below the boost corner and a dip
 *                     above it.
 *   [2] high boost  : resonant peak, bandwidth control sweeps SHARP..BROAD
 *   [3] high air    : high shelf that only opens up for the 8-16 kHz dials, so
 *                     the top stays lifted above the dial frequency instead of
 *                     dropping back to 0 dB at Nyquist
 *   [4] high atten  : high shelf cut at 5 / 10 / 20 kHz
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

    struct BandCoefficients { std::array<StageCoefficients, 5> stages {}; };

    static constexpr std::array<float, 4> lowFrequencies { 20.0f, 30.0f, 60.0f, 100.0f };
    static constexpr std::array<float, 7> highFrequencies { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f };
    static constexpr std::array<float, 3> attenuationFrequencies { 5000.0f, 10000.0f, 20000.0f };

    /** Raw value of a choice parameter is already the index; this only clamps it. */
    static int indexFromChoice (float rawValue, int numChoices);

    /** Low-band Pultec trick constants, taken from the measured turnover table
        (GroupDIY thread "Pultec EQP-1A Turnover Frequencies", compiled in
        IMPROVEMENT_NOTES_sound_quality_ref.md section 2):

          dial        20 Hz   30 Hz   60 Hz   100 Hz
          boost       100     200     300     700   (Hz, -3 dB vs maximum)
          cut         250     400     800    1500   (Hz, -3 dB vs maximum)

        The dial never moves a first-order corner: on the hardware every dial
        sounds famously low, and with a fixed capacitor each lower dial spreads
        the same corner further instead of sharpening it. So the corner is the
        dial times kLowBoostCornerRatio (mean 4.25; smartest is 4.3, 1.5x the
        dial at 100 Hz) / times kLowAttenCornerRatio (mean 9.33), and the dial
        only sets the amplitude.

        The boost keeps one corner for every dial (700 Hz, the measured turnover
        of the 100 Hz dial) instead of dial * ratio: with a single 13 dB shelf
        the amp dials would have to be about 1 dB to hit their 100-300 Hz
        turnovers, which sounds like nothing, and the hardware's signature is
        that a low boost is already wide. The cut keeps dial * 9.33 because its
        measured table is the one that separates the dials (a 100 Hz cut is a
        gentle whole-band tilt, a 20 Hz cut has its full 12 dB down low).

        The boost is anchored so the 100 Hz dial keeps the level it had before
        this model (+8.3 dB below 25 Hz at boost 8) and the lower dials scale
        up by sqrt of the dial ratio (8.0 dB at 20 Hz gives about +3.7 dB at
        20 Hz, the old trick level). The cut is anchored on its measured
        plateau (about -12 dB below 20 Hz at atten 8).
    */
    static constexpr double kLowBoostCornerRatio = 4.25;     // mean of the boost table (T100)
    static constexpr double kLowBoostCornerHz = 700.0;        // fixed corner: the boost's "whole low end"
    static constexpr double kLowAttenCornerRatio = 9.333;    // 250/400/800/1500 Hz over 20/30/60/100 Hz
    static constexpr double kLowBoostRefDial = 100.0;    // the dial whose old level is kept
    static constexpr double kLowAttenRefDial = 20.0;     // the dial whose measured plateau is kept
    static constexpr double kLowAttenMaxDb = 12.0;       // measured plateau below 20 Hz at atten 8
    static constexpr double kLowAttenDialTilt = 0.33;     // the measured cut plateau is flat, so only half-tilt

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
    /** Per-channel state of the two nonlinear stages.
        core tracks the low band that drives the core (flux), prev is the last
        shaper input so the hysteresis can see where the flux is coming from.
    */
    struct NonlinearState { double dcIn = 0.0, dcOut = 0.0, transformerLow = 0.0;
                                   double core = 0.0, prev = 0.0, magnetisation = 0.0, envelope = 0.0;
                                   double coreHighLow = 0.0; double coreHighBand = 0.0;
                                   double prevShaped = 0.0, prevSlope = 0.0, shapeRate = 60.0; int samplesSinceTurn = 0; };   // lowpasses: high-band split + its upper cap

    /** Low band level used to feed the core. */
    static constexpr double kLowBoostDriveDb = 1.6;    // core drive per dB of low boost (measured band saturates first)
    static constexpr double kCoreDriveScale = 1.2;     // how far a given boost level pushes the shaper
    static constexpr double kHysteresisShare = 0.05;  // how much of the loop reaches the output
    /** Loop width at a standstill, in shaper-slope units. */
    static constexpr double hysteresisWidth = 0.02;    // plays the role of the static coercivity
    /** High-frequency core loss path.
        A lowpass-fed core cannot make high tones thicker - a faster tone also
        arrives smaller, and the two effects cancel (measured). Real iron does
        have a separate loss at high frequency, so that is modelled as its own
        path: the band ABOVE the flux lowpass is taken out of the signal, the
        core dissipation is applied to it, and it is mixed back. Its level is
        then set by the tone itself, not by the lowpass. */
    /** Loop widening with the driving rate: the play operator is otherwise
        frequency-blind (measured gap 0.048 at every pitch from 31 Hz to 1 kHz).
        The rate is normalised by the signal, so this is pitch, not level. */
    static constexpr double kHysteresisRatePerOctave = 1.0;
    static constexpr double kHysteresisRefRate = 60.0;
    static constexpr double kHysteresisMaxScale = 4.0;

    static constexpr double kHighLossCornerHz = 250.0;   // where the extra loss starts
    static constexpr double kHighLossCapHz = 900.0;      // where it stops growing
    /** Depth of the high-band loss path (tanh slope on the extracted band). */
    static constexpr double kHighLossDepth = 6.0;
    /** How much of the shaped high band reaches the output. */
    static constexpr double kHighLossMix = 0.5;

    double coreDrive = 1.0;
    double coreBias = 0.0;

    BandCoefficients makeBand (float lowBoostDb, float lowAttenDb, float lowQ, float lowFreqChoice,
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

    std::vector<std::array<FullteqBiquad, 5>> channelFilters;
    std::vector<NonlinearState> nonlinearStates;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    double dcBlockCoeff = 0.999;
    double transformerLowCoeff = 0.02;
    /** Oversampled rate, kept for the core-loss rate weighting. */
    double oversampledRate = 48000.0 * oversamplingFactor;
    /** One-pole coefficient for the high-band split. */
    double highLossCoeff = 0.01;
    double highCapCoeff = 0.01;
    /** Smoothing for the pitch-rate estimate. */
    double shapeRateCoeff = 0.002;

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
