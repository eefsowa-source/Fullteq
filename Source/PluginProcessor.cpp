#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr double kPi = 3.14159265358979323846;

    using Stage = FullteqAudioProcessor::StageCoefficients;

    double clampFrequency (double hz, double sampleRate) { return juce::jlimit (10.0, sampleRate * 0.45, hz); }

    /** Robert Bristow-Johnson peaking EQ, normalised to a0 == 1. */
    void makePeaking (Stage& stage, double sampleRate, double frequencyHz, double q, double dbGain)
    {
        const double a = std::pow (10.0, dbGain * 0.025);
        const double w0 = 2.0 * kPi * clampFrequency (frequencyHz, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);

        const double a0 = 1.0 + alpha / a;
        stage.b0 = (1.0 + alpha * a) / a0;
        stage.b1 = (-2.0 * cosw0) / a0;
        stage.b2 = (1.0 - alpha * a) / a0;
        stage.a1 = (-2.0 * cosw0) / a0;
        stage.a2 = (1.0 - alpha / a) / a0;
    }

    /** RBJ low shelf (Q form), normalised to a0 == 1. */
    void makeLowShelf (Stage& stage, double sampleRate, double frequencyHz, double q, double dbGain)
    {
        const double a = std::pow (10.0, dbGain / 40.0);
        const double w0 = 2.0 * kPi * clampFrequency (frequencyHz, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double twoSqrtAAlpha = 2.0 * std::sqrt (a) * (std::sin (w0) / (2.0 * q));

        const double a0 = (a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha;
        stage.b0 = a * ((a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha) / a0;
        stage.b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosw0) / a0;
        stage.b2 = a * ((a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0;
        stage.a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosw0) / a0;
        stage.a2 = ((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0;
    }

    /** RBJ high shelf (Q form), normalised to a0 == 1. */
    void makeHighShelf (Stage& stage, double sampleRate, double frequencyHz, double q, double dbGain)
    {
        const double a = std::pow (10.0, dbGain / 40.0);
        const double w0 = 2.0 * kPi * clampFrequency (frequencyHz, sampleRate) / sampleRate;
        const double cosw0 = std::cos (w0);
        const double twoSqrtAAlpha = 2.0 * std::sqrt (a) * (std::sin (w0) / (2.0 * q));

        const double a0 = (a + 1.0) - (a - 1.0) * cosw0 + twoSqrtAAlpha;
        stage.b0 = a * ((a + 1.0) + (a - 1.0) * cosw0 + twoSqrtAAlpha) / a0;
        stage.b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosw0) / a0;
        stage.b2 = a * ((a + 1.0) + (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0;
        stage.a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosw0) / a0;
        stage.a2 = ((a + 1.0) - (a - 1.0) * cosw0 - twoSqrtAAlpha) / a0;
    }

    // Avoid printing "-0.00" for values that round to zero.
    juce::String formatDb (float value) { return juce::String (std::abs (value) < 0.005f ? 0.0f : value, 2) + " dB"; }
    juce::String dbToText (float value, int) { return formatDb (value); }
    float dbFromText (const juce::String& text) { return text.upToLastOccurrenceOf (" ", false, false).getFloatValue(); }

    juce::String percentToText (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + " %"; }
    float percentFromText (const juce::String& text) { return text.upToLastOccurrenceOf (" ", false, false).getFloatValue() / 100.0f; }

    juce::String ratioToText (float value, int) { return juce::String (value, 2); }
    float ratioFromText (const juce::String& text) { return text.getFloatValue(); }

    std::unique_ptr<juce::RangedAudioParameter> dbParam (const juce::String& id, const juce::String& name, float maximum, float defaultValue = 0.0f)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name,
                                                           juce::NormalisableRange<float> (0.0f, maximum, 0.01f),
                                                           defaultValue, juce::String(),
                                                           juce::AudioProcessorParameter::genericParameter,
                                                           dbToText, dbFromText);
    }

    using PresetEntry = std::pair<juce::String, float>;

    /** Choice values are indices: lowFreq 0..3 = 20/30/60/100 Hz, highFreq 0..6 = 3/4/5/8/10/12/16 kHz,
        attenSel 0..2 = 5/10/20 kHz. Bandwidth is 0 (sharp) .. 10 (broad). */
    std::vector<PresetEntry> makePresetEntries (float lowFreq, float lowBoost, float lowAtten,
                                                float highFreq, float highBoost, float highAtten,
                                                float attenSel, float bandwidth, float drive, float outputGain)
    {
        return { { "lowFreq", lowFreq }, { "lowBoost", lowBoost }, { "lowAtten", lowAtten }, { "lowQ", 1.0f },
                 { "highFreq", highFreq }, { "highBoost", highBoost }, { "highAtten", highAtten },
                 { "attenSel", attenSel }, { "bandwidth", bandwidth },
                 { "inputGain", 0.0f }, { "outputGain", outputGain }, { "drive", drive },
                 { "tube", 1.0f }, { "transformer", 1.0f }, { "input", 1.0f }, { "bypass", 0.0f } };
    }

    std::vector<PresetEntry> makePreset (int index)
    {
        switch (index)
        {
            case 0: return makePresetEntries (2, 0.0f, 0.0f, 4, 0.0f, 0.0f, 1, 5.0f, 0.15f, 0.0f);   // Init
            case 1: return makePresetEntries (2, 8.0f, 8.0f, 4, 0.0f, 0.0f, 1, 5.0f, 0.15f, 0.0f);   // Pultec Low Trick
            case 2: return makePresetEntries (1, 7.0f, 2.0f, 4, 0.0f, 0.0f, 1, 5.0f, 0.20f, 0.0f);   // Deep Sub Lift
            case 3: return makePresetEntries (3, 2.0f, 0.0f, 2, 5.0f, 0.0f, 1, 3.5f, 0.15f, -0.5f);  // Vocal Presence
            case 4: return makePresetEntries (2, 2.5f, 1.5f, 5, 3.0f, 1.0f, 2, 8.0f, 0.25f, -0.5f);  // Master Glue
            case 5: return makePresetEntries (3, 1.0f, 0.0f, 6, 8.0f, 0.0f, 2, 7.0f, 0.15f, -1.0f);  // Bright Air
            case 6: return makePresetEntries (2, 1.0f, 0.0f, 3, 0.0f, 4.0f, 0, 5.0f, 0.15f, 0.0f);   // Tame Harsh
            default: return makePresetEntries (2, 3.0f, 1.0f, 4, 2.0f, 0.0f, 1, 6.0f, 0.55f, -1.0f); // Tube Warmth
        }
    }
}

const std::array<const char*, 8> FullteqAudioProcessor::presetNames
{
    "Init", "Pultec Low Trick", "Deep Sub Lift", "Vocal Presence",
    "Master Glue", "Bright Air", "Tame Harsh", "Tube Warmth"
};

FullteqAudioProcessor::FullteqAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout FullteqAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray lowChoices;
    for (auto f : lowFrequencies) lowChoices.add (juce::String (f, 0) + " Hz");
    layout.add (std::make_unique<juce::AudioParameterChoice> ("lowFreq", "Low Frequency", lowChoices, 2));

    juce::StringArray highChoices;
    for (auto f : highFrequencies) highChoices.add (juce::String (f / 1000.0f, 0) + " kHz");
    layout.add (std::make_unique<juce::AudioParameterChoice> ("highFreq", "High Frequency", highChoices, 4));

    juce::StringArray attenuationChoices { "5 kHz", "10 kHz", "20 kHz" };
    layout.add (std::make_unique<juce::AudioParameterChoice> ("attenSel", "Atten Select", attenuationChoices, 1));

    layout.add (dbParam ("lowBoost", "Low Boost", 20.0f));
    layout.add (dbParam ("lowAtten", "Low Atten", 20.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("lowQ", "Low Shelf Q", juce::NormalisableRange<float> (0.4f, 2.0f, 0.01f), 1.0f,
                                                            juce::String(), juce::AudioProcessorParameter::genericParameter,
                                                            ratioToText, ratioFromText));

    layout.add (dbParam ("highBoost", "High Boost", 20.0f));
    layout.add (dbParam ("highAtten", "High Atten", 20.0f));
    // 0 = SHARP, 10 = BROAD, like the printed scale on the hardware.
    layout.add (std::make_unique<juce::AudioParameterFloat> ("bandwidth", "Bandwidth", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f,
                                                            juce::String(), juce::AudioProcessorParameter::genericParameter,
                                                            ratioToText, ratioFromText));

    auto gainParam = [] (const juce::String& id, const juce::String& name)
    {
        return std::make_unique<juce::AudioParameterFloat> (id, name, juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                                           juce::String(), juce::AudioProcessorParameter::genericParameter,
                                                           [] (float v, int) { return formatDb (v); },
                                                           [] (const juce::String& t) { return t.upToLastOccurrenceOf (" ", false, false).getFloatValue(); });
    };
    layout.add (gainParam ("inputGain", "Input"));
    layout.add (gainParam ("outputGain", "Output"));

    layout.add (std::make_unique<juce::AudioParameterFloat> ("drive", "Drive", juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.15f,
                                                            juce::String(), juce::AudioProcessorParameter::genericParameter,
                                                            percentToText, percentFromText));

    // Bool parameters are stored as stepped 0/1 floats so state save/restore
    // (and pluginval's state-restoration test) round-trips the exact value.
    layout.add (std::make_unique<juce::AudioParameterFloat> ("tube", "Tube Stage",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("transformer", "Transformer",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("input", "In",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> ("bypass", "Bypass",
                juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), 0.0f));

    return layout;
}

int FullteqAudioProcessor::indexFromChoice (float rawValue, int numChoices)
{
    return juce::jlimit (0, numChoices - 1, juce::roundToInt (rawValue));
}

FullteqAudioProcessor::BandCoefficients FullteqAudioProcessor::computeBandCoefficients (
    double sampleRate, float lowFrequencyHz, float lowBoostDb, float lowAttenDb, float lowQ,
    float highFrequencyHz, float attenuationFrequencyHz, float highBoostDb, float highAttenDb, float bandwidth)
{
    BandCoefficients band;

    // Low band. Boost and attenuation are separate shelves that load each
    // other; the attenuation shelf sits much higher, so with both engaged the
    // boost survives below its corner while a dip opens up above it - the
    // "trick".
    //
    // Low shelves follow the measured turnover table (see the constants in the
    // header). The boost corner is a fixed 700 Hz: every dial swings the same
    // amplitude below it, which is why a 60 Hz boost already reaches down to
    // 15 Hz (the "whole low end", not a 60 Hz bell). Measured turnover ratios
    // per dial (100/200/300/700 Hz against 20/30/60/100 Hz) average 4.25 and
    // differ by only +-15 %, so one corner is enough and keeps the levels that
    // the tests and presets already expect. The cut corner is the dial times
    // kLowAttenCornerRatio (250/400/800/1500 Hz against 20/30/60/100 Hz), which
    // reproduces the measured table exactly. The dial sets the amplitude: the
    // boost reproduces the old level at 100 Hz (+8.3 dB below 25 Hz at boost 8),
    // the cut is anchored on its measured plateau.
    const double q = static_cast<double> (lowQ);
    const double dialHz = juce::jmax (10.0, static_cast<double> (lowFrequencyHz));
    const double boostGain = static_cast<double> (lowBoostDb) * std::sqrt (dialHz / kLowBoostRefDial);
    const double cutGain = -kLowAttenMaxDb * static_cast<double> (lowAttenDb) / 8.0
                         * std::pow (static_cast<double> (kLowAttenRefDial) / dialHz, kLowAttenDialTilt);

    // The boost corner is fixed at 700 Hz and the boost is anchored on the
    // 100 Hz dial (+8.3 dB below 25 Hz at boost 8, the level this plug-in
    // already produced); the lower dials scale up by sqrt so 8 dB at 20 Hz
    // gives about +3.7 dB below 100 Hz. The cut corner is dial * 9.33
    // (measured 250/400/800/1500 Hz) and its plateau is anchored at -12 dB.
    makeLowShelf (band.stages[0], sampleRate, kLowBoostCornerHz, 0.9 * q, boostGain);
    makeLowShelf (band.stages[1], sampleRate, dialHz * kLowAttenCornerRatio, 0.4 * q, cutGain);

    // High band. Bandwidth 0 (SHARP) .. 10 (BROAD) sweeps the resonant boost
    // from Q 2.5 down to Q 0.4 on a log scale. The attenuation is a fixed
    // high shelf at the ATTEN SEL frequency.
    //
    // Above the dial frequency the LC tank keeps the lift going instead of
    // dropping back to 0 dB at Nyquist, so the air settings add a shelf on top
    // of the peak. It fades in from 6 kHz so the 3-5 kHz dials stay bells. A
    // shelf gives half its gain at its corner, so the peak is trimmed by half
    // the shelf gain to keep the dial value as the gain at the dial frequency.
    const double broadness = juce::jlimit (0.0, 1.0, static_cast<double> (bandwidth) / 10.0);
    const double boostQ = 2.5 * std::pow (0.16, broadness);
    const double airDb = highBoostDb * 0.6 * juce::jlimit (0.0, 1.0, (highFrequencyHz - 6000.0) / 10000.0);
    makePeaking (band.stages[2], sampleRate, highFrequencyHz, boostQ, highBoostDb - 0.5 * airDb);
    makeHighShelf (band.stages[3], sampleRate, highFrequencyHz, 0.7, airDb);
    makeHighShelf (band.stages[4], sampleRate, attenuationFrequencyHz, 0.6, -static_cast<double> (highAttenDb));

    return band;
}

FullteqAudioProcessor::BandCoefficients FullteqAudioProcessor::makeBand (
    float lowBoostDb, float lowAttenDb, float lowQ, float lowFreqChoice, float highBoostDb, float highAttenDb, float bandwidth) const
{
    const int lowIndex = indexFromChoice (lowFreqChoice, (int) lowFrequencies.size());
    const int highIndex = indexFromChoice (*apvts.getRawParameterValue ("highFreq"), (int) highFrequencies.size());
    const int attenIndex = indexFromChoice (*apvts.getRawParameterValue ("attenSel"), (int) attenuationFrequencies.size());

    return computeBandCoefficients (currentSampleRate,
                                    lowFrequencies[(size_t) lowIndex], lowBoostDb, lowAttenDb, lowQ,
                                    highFrequencies[(size_t) highIndex], attenuationFrequencies[(size_t) attenIndex],
                                    highBoostDb, highAttenDb, bandwidth);
}

double FullteqAudioProcessor::stageMagnitudeLinear (const StageCoefficients& stage, double hz, double sampleRate)
{
    const double w = 2.0 * kPi * hz / sampleRate;
    const double cosw = std::cos (w);
    const double sinw = std::sin (w);
    const double cos2w = std::cos (2.0 * w);
    const double sin2w = std::sin (2.0 * w);

    const double nr = stage.b0 + stage.b1 * cosw + stage.b2 * cos2w;
    const double ni = -(stage.b1 * sinw + stage.b2 * sin2w);
    const double dr = 1.0 + stage.a1 * cosw + stage.a2 * cos2w;
    const double di = -(stage.a1 * sinw + stage.a2 * sin2w);

    const double numerator = nr * nr + ni * ni;
    const double denominator = dr * dr + di * di;
    return std::sqrt (numerator / denominator);
}

void FullteqAudioProcessor::getResponseDb (const double* frequenciesHz, float* outDb, int numPoints)
{
    const BandCoefficients band = makeBand (*apvts.getRawParameterValue ("lowBoost"), *apvts.getRawParameterValue ("lowAtten"),
                                            *apvts.getRawParameterValue ("lowQ"),
                                            *apvts.getRawParameterValue ("lowFreq"),
                                            *apvts.getRawParameterValue ("highBoost"), *apvts.getRawParameterValue ("highAtten"),
                                            *apvts.getRawParameterValue ("bandwidth"));

    for (int i = 0; i < numPoints; ++i)
    {
        const double hz = clampFrequency (frequenciesHz[i], currentSampleRate);
        double magnitude = 1.0;
        for (const auto& stage : band.stages)
            magnitude *= stageMagnitudeLinear (stage, hz, currentSampleRate);


        outDb[i] = juce::Decibels::gainToDecibels (static_cast<float> (magnitude));
    }
}

bool FullteqAudioProcessor::isEffectBypassed() const
{
    return *apvts.getRawParameterValue ("bypass") >= 0.5f || *apvts.getRawParameterValue ("input") < 0.5f;
}

void FullteqAudioProcessor::loadPreset (int index)
{
    const auto clamped = juce::jlimit (0, (int) presetNames.size() - 1, index);

    for (const auto& [id, value] : makePreset (clamped))
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));

    currentPreset.store (clamped, std::memory_order_relaxed);
}

void FullteqAudioProcessor::resizeScratch (int numSamples)
{
    inGainRamp.resize ((size_t) numSamples);
    outGainRamp.resize ((size_t) numSamples);
    bypassRamp.resize ((size_t) numSamples);
    driveRamp.resize ((size_t) numSamples);
}

void FullteqAudioProcessor::resetDspState()
{
    for (auto& stages : channelFilters)
        for (auto& stage : stages)
            stage.reset();

    for (auto& state : nonlinearStates)
        state = NonlinearState();

    if (oversampler != nullptr)
        oversampler->reset();
}

void FullteqAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);

    const int numChannels = juce::jmax (1, getTotalNumInputChannels());
    channelFilters.assign ((size_t) numChannels, {});
    nonlinearStates.assign ((size_t) numChannels, {});

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, (size_t) oversamplingLog2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlockSize);
    oversampler->reset();

    latencySamples = juce::roundToInt (oversampler->getLatencyInSamples());
    setLatencySamples (latencySamples);

    const double osRate = sampleRate * oversamplingFactor;
    dcBlockCoeff = 1.0 - 2.0 * kPi * 5.0 / osRate;
    transformerLowCoeff = 1.0 - std::exp (-2.0 * kPi * 160.0 / osRate);
    oversampledRate = osRate;
    highLossCoeff = 1.0 - std::exp (-2.0 * kPi * kHighLossCornerHz / osRate);
    highCapCoeff = 1.0 - std::exp (-2.0 * kPi * kHighLossCapHz / osRate);

    dryBuffer.setSize (numChannels, maxBlockSize, false, false, true);
    dryDelay.assign ((size_t) numChannels, std::vector<float> ((size_t) juce::jmax (0, latencySamples), 0.0f));
    dryDelayPos.assign ((size_t) numChannels, 0);
    resizeScratch (maxBlockSize);

    constexpr double rampSeconds = 0.03;
    lowBoostSmoothed.reset (sampleRate, rampSeconds);
    lowAttenSmoothed.reset (sampleRate, rampSeconds);
    lowQSmoothed.reset (sampleRate, rampSeconds);
    highBoostSmoothed.reset (sampleRate, rampSeconds);
    highAttenSmoothed.reset (sampleRate, rampSeconds);
    bandwidthSmoothed.reset (sampleRate, rampSeconds);
    inputGainSmoothed.reset (sampleRate, rampSeconds);
    outputGainSmoothed.reset (sampleRate, rampSeconds);
    driveSmoothed.reset (sampleRate, rampSeconds);
    bypassSmoothed.reset (sampleRate, 0.02);

    // Start from the current parameter values instead of ramping up from zero.
    lowBoostSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("lowBoost"));
    lowAttenSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("lowAtten"));
    lowQSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("lowQ"));
    highBoostSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("highBoost"));
    highAttenSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("highAtten"));
    bandwidthSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("bandwidth"));
    inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("inputGain")));
    outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("outputGain")));
    driveSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("drive"));
    bypassSmoothed.setCurrentAndTargetValue (isEffectBypassed() ? 1.0f : 0.0f);

    wasBypassed = false;
}

void FullteqAudioProcessor::releaseResources()
{
    resetDspState();
    dryBuffer.setSize (0, 0, false, false, false);
}

bool FullteqAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && output == layouts.getMainInputChannelSet();
}

void FullteqAudioProcessor::applyDryDelay (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples)
{
    if (latencySamples <= 0)
        return;

    for (int ch = 0; ch < numChannels && ch < (int) dryDelay.size(); ++ch)
    {
        auto& line = dryDelay[(size_t) ch];
        int pos = dryDelayPos[(size_t) ch];
        float* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float in = data[i];
            data[i] = line[(size_t) pos];
            line[(size_t) pos] = in;
            if (++pos >= latencySamples)
                pos = 0;
        }

        dryDelayPos[(size_t) ch] = pos;
    }
}

void FullteqAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int ch = totalNumInputChannels; ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    // A host passes on whatever an upstream plug-in left behind, and one non-finite
    // sample latches in the nonlinear state for good (pluginval fuzz does this too).
    for (int ch = 0; ch < juce::jmin (totalNumInputChannels, buffer.getNumChannels()); ++ch)
        for (float* d = buffer.getWritePointer (ch), *last = d + numSamples; d != last; ++d)
            if (! std::isfinite (*d))
                *d = 0.0f;

    const int numCh = juce::jmin (totalNumInputChannels, buffer.getNumChannels(), (int) channelFilters.size());
    if (numSamples <= 0 || numCh <= 0 || oversampler == nullptr)
        return;

    if (numSamples > (int) inGainRamp.size())
        resizeScratch (numSamples);

    const float decay = std::pow (0.001f, (float) numSamples / (float) currentSampleRate);
    const float peakIn = buffer.getMagnitude (0, numSamples);

    const bool bypassTarget = isEffectBypassed();
    bypassSmoothed.setTargetValue (bypassTarget ? 1.0f : 0.0f);

    if (bypassTarget && ! bypassSmoothed.isSmoothing())
    {
        // Fully bypassed: skip the DSP, but keep the same latency as the wet
        // path so the host's delay compensation stays correct.
        lowBoostSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("lowBoost"));
        lowAttenSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("lowAtten"));
        lowQSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("lowQ"));
        highBoostSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("highBoost"));
        highAttenSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("highAtten"));
        bandwidthSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("bandwidth"));
        inputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("inputGain")));
        outputGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("outputGain")));
        driveSmoothed.setCurrentAndTargetValue (*apvts.getRawParameterValue ("drive"));

        if (! wasBypassed)
        {
            resetDspState();
            wasBypassed = true;
        }

        applyDryDelay (buffer, numCh, numSamples);

        const float peak = buffer.getMagnitude (0, numSamples);
        inputLevel.store (std::max (peakIn, inputLevel.load (std::memory_order_relaxed) * decay), std::memory_order_relaxed);
        outputLevel.store (std::max (peak, outputLevel.load (std::memory_order_relaxed) * decay), std::memory_order_relaxed);
        return;
    }

    wasBypassed = false;

    lowBoostSmoothed.setTargetValue (*apvts.getRawParameterValue ("lowBoost"));
    lowAttenSmoothed.setTargetValue (*apvts.getRawParameterValue ("lowAtten"));
    lowQSmoothed.setTargetValue (*apvts.getRawParameterValue ("lowQ"));
    highBoostSmoothed.setTargetValue (*apvts.getRawParameterValue ("highBoost"));
    highAttenSmoothed.setTargetValue (*apvts.getRawParameterValue ("highAtten"));
    bandwidthSmoothed.setTargetValue (*apvts.getRawParameterValue ("bandwidth"));
    inputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("inputGain")));
    outputGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain ((float) *apvts.getRawParameterValue ("outputGain")));
    driveSmoothed.setTargetValue (*apvts.getRawParameterValue ("drive"));

    const bool tubeOn = *apvts.getRawParameterValue ("tube") >= 0.5f;
    const bool transformerOn = *apvts.getRawParameterValue ("transformer") >= 0.5f;

    // Core drive follows the low band, the way the real unit's output
    // transformer is pushed by a big low-boost lift. Only the boost and the
    // drive knob raise it: attenuation adds no flux.
    const double driveKnob = (double) *apvts.getRawParameterValue ("drive");
    const double liftDb = kLowBoostDriveDb * std::max (0.0, (double) lowBoostSmoothed.getTargetValue()) / 8.0;
    // Anchor: at the default drive (0.15) and no low boost, coreDrive is 1.0, so
    // the core behaves exactly as it did before the band link existed (the 50 Hz
    // lift test measures that state). Boost and the drive knob raise it from there.
    coreDrive = std::pow (10.0, (liftDb + 6.0 * (driveKnob - 0.15)) / 20.0);
    coreBias = 0.12 + 0.18 * (double) *apvts.getRawParameterValue ("drive");

    // Filter coefficients are updated once per block from the smoothed value
    // at the end of the block; gains, drive and the bypass mix ramp per sample.
    const float lowBoostDb = lowBoostSmoothed.skip (numSamples);
    const float lowAttenDb = lowAttenSmoothed.skip (numSamples);
    const float lowQ = lowQSmoothed.skip (numSamples);
    const float highBoostDb = highBoostSmoothed.skip (numSamples);
    const float highAttenDb = highAttenSmoothed.skip (numSamples);
    const float bandwidth = bandwidthSmoothed.skip (numSamples);

    for (int i = 0; i < numSamples; ++i)
    {
        inGainRamp[(size_t) i] = inputGainSmoothed.getNextValue();
        outGainRamp[(size_t) i] = outputGainSmoothed.getNextValue();
        driveRamp[(size_t) i] = driveSmoothed.getNextValue();
        bypassRamp[(size_t) i] = bypassSmoothed.getNextValue();
    }

    // Dry path: untouched input, delayed to line up with the oversampled wet path.
    dryBuffer.makeCopyOf (buffer, true);
    applyDryDelay (dryBuffer, numCh, numSamples);

    BandCoefficients band = makeBand (lowBoostDb, lowAttenDb, lowQ, *apvts.getRawParameterValue ("lowFreq"),
                                      highBoostDb, highAttenDb, bandwidth);

    // 1) Passive EQ network.
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& stages = channelFilters[(size_t) ch];
        for (size_t i = 0; i < stages.size(); ++i)
        {
            const auto& s = band.stages[i];
            stages[i].setCoefficients (s.b0, s.b1, s.b2, s.a1, s.a2);
        }

        float* data = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
        {
            float y = data[i] * inGainRamp[(size_t) i];
            for (auto& stage : stages)
                y = stage.process (y);
            data[i] = y;
        }
    }

    // 2) Tube stage and 3) transformer, oversampled.
    juce::dsp::AudioBlock<float> fullBlock (buffer);
    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int length = juce::jmin (maxBlockSize, numSamples - start);
        auto sub = fullBlock.getSubBlock ((size_t) start, (size_t) length);
        auto up = oversampler->processSamplesUp (sub);
        const int upLength = (int) up.getNumSamples();

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& state = nonlinearStates[(size_t) ch];
            float* data = up.getChannelPointer ((size_t) ch);

            for (int j = 0; j < upLength; ++j)
            {
                const double drive = driveRamp[(size_t) (start + (j >> oversamplingLog2))];
                double x = static_cast<double> (data[j]);

                if (tubeOn)
                {
                    // Triode-like shaper: tanh for odd harmonics plus a small
                    // bias for 2nd harmonic, normalised to unity small-signal
                    // gain, then a DC blocker removes the bias offset.
                    const double k = 0.6 + 4.4 * drive;
                    const double bias = 0.10 + 0.10 * drive;
                    const double tb = std::tanh (bias);
                    const double y = (std::tanh (k * x + bias) - tb) / (k * (1.0 - tb * tb));

                    const double hp = y - state.dcIn + dcBlockCoeff * state.dcOut;
                    state.dcIn = y;
                    state.dcOut = hp;
                    x = hp;
                }

                if (transformerOn)
                {
                    // Core model. The low band (where the flux is) drives it, so
                    // a low-boost lift pushes the iron harder - the band that
                    // carries the energy saturates first, as on the hardware.
                    const double m = (1.2 + 1.8 * drive) * coreDrive;
                    state.transformerLow += transformerLowCoeff * (x - state.transformerLow);
                    const double flux = state.transformerLow;

                    // Asymmetric shaper: tanh for odd harmonics, bias for the
                    // even ones, normalised to unity small-signal gain.
                    const double tb = std::tanh (coreBias);
                    const double shaped = (std::tanh (m * flux + coreBias) - tb) / (m * (1.0 - tb * tb));

                    // Hysteresis (Preisach-style play operator): the core output
                    // only follows the flux once it has moved far enough past
                    // where it last turned around, so it lags on the way back and
                    // remembers the previous swing.
                    // Loop width: a fraction of the shaper slope, the physical
                    // scale of where the core turns around. Scaling it with the
                    // signal level instead swallows quiet material, so it stays
                    // tied to the drive (which is what sets the core's working
                    // point) and to the shaper slope.
                    // Core loss at high frequency.
                    //
                    // Measuring loss from the core's own flux does not work: the
                    // flux path is a lowpass, so a faster tone arrives smaller by
                    // about as much as it changes faster, and |dPhi/dt| barely
                    // moves with pitch (measured: 61 -> 249 for 40 -> 400 Hz,
                    // while the saturation point stayed flat). The extra loss is
                    // therefore modelled on its own path: the band above the
                    // flux corner is lifted out of the signal, the core's
                    // dissipation is applied to it, and it is mixed back. Its
                    // level comes from the tone itself, so higher tones lose more.
                    state.coreHighLow += highLossCoeff * (x - state.coreHighLow);
                    const double aboveLowPass = x - state.coreHighLow;
                    // Cap the band: real iron loss flattens out rather than
                    // growing without limit, and the transformer must not take
                    // level from the top end (contract in tests 7 and 12).
                    state.coreHighBand += highCapCoeff * (aboveLowPass - state.coreHighBand);
                    const double highBand = aboveLowPass - state.coreHighBand;

                    // Iron dissipates the top octaves harder than the body of the
                    // signal. The high band is shaped on its own and added back,
                    // so a 400 Hz tone carries real loss while a 40 Hz one barely
                    // touches this path. Frequency weighting comes from the
                    // split itself, not from measuring flux rate.
                    // Only loss: tanh compresses peaks, so dividing by the slope
                    // (hb) keeps unity small-signal gain and the difference is
                    // never positive - the path can only take energy away.
                    const double hb = kHighLossDepth;
                    const double lostBand = std::min (0.0, std::tanh (hb * highBand) / hb - highBand);

                    // Hysteresis (Preisach-style play operator): the core only
                    // follows the flux once it has moved past where it last
                    // turned around, so it lags on the way back and remembers
                    // the previous swing. The width is tied to the shaper slope
                    // (the core's working point), not to the signal level, which
                    // would swallow quiet material.
                    // Loop width grows with how fast the core is being driven.
                    // A static play operator gives every pitch an identical loop
                    // (measured: gap 0.048 at 31 Hz through 1 kHz), so the iron
                    // responded to frequency not at all. Here the width scales
                    // with the shaper's slope normalised by the pitch, so faster
                    // flux means a wider loop and a longer memory.
                    // Pitch, not level: count how long a half-swing takes by
                    // timing the sign changes of the core's slope. That is the
                    // frequency the iron actually sees, independent of amplitude.
                    const double dShaped = shaped - state.prevShaped;
                    if (dShaped > 0.0 && state.prevSlope <= 0.0)
                    {
                        if (state.samplesSinceTurn > 0)
                        {
                            const double halfPeriod = state.samplesSinceTurn / oversampledRate;
                            if (halfPeriod > 1.0e-5 && halfPeriod < 5.0e-2)
                                state.shapeRate += 0.05 * (0.5 / halfPeriod - state.shapeRate);
                        }
                        state.samplesSinceTurn = 0;
                    }
                    ++state.samplesSinceTurn;
                    state.prevSlope = dShaped;
                    const double widthScale = std::clamp (1.0 + kHysteresisRatePerOctave * std::log2 (std::max (1.0, state.shapeRate / kHysteresisRefRate)),
                                                          0.25, kHysteresisMaxScale);
                    const double width = hysteresisWidth * m * widthScale;
                    if (shaped > state.magnetisation + width)
                        state.magnetisation = shaped - width;
                    else if (shaped < state.magnetisation - width)
                        state.magnetisation = shaped + width;

                    state.prev = flux;
                    state.prevShaped = shaped;

                    // Hysteresis enters as the memory term only: the gap between
                    // where the core is and where the flux says it should be.
                    // The direct signal keeps its full level, so the band's gain
                    // is untouched and only the history is new.
                    x = (x - flux) + shaped + kHysteresisShare * (state.magnetisation - shaped) + 0.08 * flux
                        + kHighLossMix * lostBand;
                }

                data[j] = static_cast<float> (x);
            }
        }

        oversampler->processSamplesDown (sub);
    }

    // 4) Output gain and bypass cross-fade against the aligned dry signal.
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        const float* dry = dryBuffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float mix = bypassRamp[(size_t) i];
            out[i] = out[i] * outGainRamp[(size_t) i] * (1.0f - mix) + dry[i] * mix;
        }
    }

    const float peakOut = buffer.getMagnitude (0, numSamples);
    inputLevel.store (std::max (peakIn, inputLevel.load (std::memory_order_relaxed) * decay), std::memory_order_relaxed);
    outputLevel.store (std::max (peakOut, outputLevel.load (std::memory_order_relaxed) * decay), std::memory_order_relaxed);
}

int FullteqAudioProcessor::getNumPrograms() { return (int) presetNames.size(); }
int FullteqAudioProcessor::getCurrentProgram() { return getCurrentPresetIndex(); }
void FullteqAudioProcessor::setCurrentProgram (int index) { loadPreset (index); }
const juce::String FullteqAudioProcessor::getProgramName (int index)
{
    const auto clamped = juce::jlimit (0, (int) presetNames.size() - 1, index);
    return presetNames[(size_t) clamped];
}
void FullteqAudioProcessor::changeProgramName (int, const juce::String&) {}

void FullteqAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FullteqAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* FullteqAudioProcessor::createEditor() { return new FullteqAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FullteqAudioProcessor(); }
