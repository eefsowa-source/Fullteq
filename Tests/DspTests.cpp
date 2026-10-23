#include <JuceHeader.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    int failures = 0;

    void check (bool condition, const char* what, double value = 0.0)
    {
        std::printf ("  [%s] %s (%.4f)\n", condition ? "PASS" : "FAIL", what, value);
        if (! condition) ++failures;
    }

    void setParam (FullteqAudioProcessor& p, const char* id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    void flat (FullteqAudioProcessor& p, bool tube, bool transformer)
    {
        for (auto id : { "lowBoost", "lowAtten", "highBoost", "highAtten", "inputGain", "outputGain", "drive" })
            setParam (p, id, 0.0f);
        setParam (p, "lowFreq", 2.0f);
        setParam (p, "highFreq", 4.0f);
        setParam (p, "attenSel", 1.0f);
        setParam (p, "bandwidth", 5.0f);
        setParam (p, "tube", tube ? 1.0f : 0.0f);
        setParam (p, "transformer", transformer ? 1.0f : 0.0f);
        setParam (p, "input", 1.0f);
        setParam (p, "bypass", 0.0f);
    }

    std::vector<float> run (FullteqAudioProcessor& p, const std::vector<float>& in, int block = blockSize)
    {
        std::vector<float> out (in.size());
        juce::MidiBuffer midi;
        for (size_t start = 0; start < in.size(); start += (size_t) block)
        {
            const int n = (int) std::min ((size_t) block, in.size() - start);
            juce::AudioBuffer<float> buffer (2, n);
            for (int i = 0; i < n; ++i)
            {
                buffer.setSample (0, i, in[start + (size_t) i]);
                buffer.setSample (1, i, in[start + (size_t) i]);
            }
            p.processBlock (buffer, midi);
            for (int i = 0; i < n; ++i)
                out[start + (size_t) i] = buffer.getSample (0, i);
        }
        return out;
    }

    std::vector<float> sine (double hz, double amp, int samples)
    {
        std::vector<float> v ((size_t) samples);
        for (int i = 0; i < samples; ++i)
            v[(size_t) i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sampleRate));
        return v;
    }

    /** Amplitude of the component at hz over the last half second (integer cycles for even hz). */
    double amplitudeAt (const std::vector<float>& v, double hz)
    {
        const int n = 24000;
        const size_t start = v.size() - (size_t) n;
        double c = 0.0, s = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / sampleRate;
            const double x = (double) v[start + (size_t) i];
            c += x * std::cos (ph);
            s += x * std::sin (ph);
        }
        return 2.0 * std::sqrt (c * c + s * s) / n;
    }

    double mean (const std::vector<float>& v)
    {
        const int n = 24000;
        double m = 0.0;
        for (size_t i = v.size() - (size_t) n; i < v.size(); ++i) m += (double) v[i];
        return m / n;
    }

    double measuredDb (FullteqAudioProcessor& p, double hz, double amp = 0.01)
    {
        const auto in = sine (hz, amp, 72000);
        const auto out = run (p, in);
        return 20.0 * std::log10 (amplitudeAt (out, hz) / amplitudeAt (in, hz));
    }

    double analyticDb (FullteqAudioProcessor& p, double hz)
    {
        float db = 0.0f;
        p.getResponseDb (&hz, &db, 1);
        return (double) db;
    }

    void prepare (FullteqAudioProcessor& p, int maxBlock = blockSize)
    {
        p.setPlayConfigDetails (2, 2, sampleRate, maxBlock);
        p.prepareToPlay (sampleRate, maxBlock);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        // Render the editor offscreen to a PNG: --snapshot out.png [preset]
        FullteqAudioProcessor p;
        prepare (p);
        p.loadPreset (argc > 3 ? juce::String (argv[3]).getIntValue() : 1);
        FullteqAudioProcessorEditor editor (p);
        const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
        const juce::File file { juce::String (argv[2]) };
        file.deleteFile();
        juce::FileOutputStream stream { file };
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::printf ("wrote %s (%dx%d)\n", argv[2], image.getWidth(), image.getHeight());
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "--diag")
    {
        // Worst alias product (any 1 kHz multiple except the fundamental) for a loud 13 kHz tone.
        for (auto [amp, driveValue] : { std::pair { 0.5, 0.5f }, std::pair { 0.5, 1.0f }, std::pair { 0.8, 1.0f } })
        {
            FullteqAudioProcessor p;
            prepare (p);
            flat (p, true, false);
            setParam (p, "drive", driveValue);
            const auto out = run (p, sine (13000.0, amp, 72000));
            const double fund = amplitudeAt (out, 13000.0);
            double worst = -200.0;
            int worstK = 0;
            for (int k = 1; k <= 23; ++k)
            {
                if (k == 13) continue;
                const double rel = 20.0 * std::log10 (std::max (1e-9, amplitudeAt (out, k * 1000.0)) / fund);
                if (rel > worst) { worst = rel; worstK = k; }
            }
            std::printf ("amp=%.1f drive=%.1f  worst alias %.1f dB rel. fundamental @%d kHz\n", amp, driveValue, worst, worstK);
        }
        return 0;
    }

    std::printf ("1. choice index mapping\n");
    check (FullteqAudioProcessor::indexFromChoice (0.0f, 4) == 0, "index 0 -> 0");
    check (FullteqAudioProcessor::indexFromChoice (2.0f, 4) == 2, "index 2 -> 2 (old code returned 3)");
    check (FullteqAudioProcessor::indexFromChoice (6.0f, 7) == 6, "index 6 of 7 -> 6");

    std::printf ("2. flat settings, tube and transformer off: unity gain\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, false, false);
        for (double hz : { 100.0, 1000.0, 10000.0 })
        {
            const double db = measuredDb (p, hz);
            check (std::abs (db) < 0.1, "unity", db);
        }
    }

    std::printf ("3. low band Pultec trick (60 Hz, boost 8 / atten 8)\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, false, false);
        setParam (p, "lowBoost", 8.0f);
        setParam (p, "lowAtten", 8.0f);
        check (analyticDb (p, 25.0) >= 2.5, "lift below corner @25 Hz", analyticDb (p, 25.0));
        check (analyticDb (p, 110.0) <= -1.5, "dip above corner @110 Hz", analyticDb (p, 110.0));
        check (std::abs (analyticDb (p, 1000.0)) < 0.3, "flat @1 kHz", analyticDb (p, 1000.0));
        for (double hz : { 26.0, 110.0 })
        {
            const double a = analyticDb (p, hz), m = measuredDb (p, hz);
            check (std::abs (a - m) < 0.25, "measured matches analytic", m - a);
        }
    }

    std::printf ("4. measured vs analytic with every band engaged\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, false, false);
        setParam (p, "lowBoost", 6.0f);
        setParam (p, "lowAtten", 3.0f);
        setParam (p, "highBoost", 10.0f);
        setParam (p, "highAtten", 4.0f);
        setParam (p, "bandwidth", 3.0f);
        for (double hz : { 50.0, 200.0, 1000.0, 5000.0, 10000.0, 16000.0 })
        {
            const double a = analyticDb (p, hz), m = measuredDb (p, hz);
            check (std::abs (a - m) < 0.25, "response", m - a);
        }
    }

    std::printf ("5. bandwidth is SHARP at 0 and BROAD at 10\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, false, false);
        setParam (p, "highBoost", 10.0f);
        setParam (p, "bandwidth", 0.0f);
        const double sharpSkirt = analyticDb (p, 5000.0);
        setParam (p, "bandwidth", 10.0f);
        const double broadSkirt = analyticDb (p, 5000.0);
        check (broadSkirt > sharpSkirt + 1.0, "broad setting has wider skirt @5 kHz", broadSkirt - sharpSkirt);
    }

    std::printf ("6. tube stage: harmonics, unity small-signal gain, no DC, no aliasing\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, true, false);
        setParam (p, "drive", 0.5f);
        const double small = measuredDb (p, 1000.0, 0.01);
        check (std::abs (small) < 0.3, "small-signal gain ~ unity", small);

        const auto in = sine (1000.0, 0.8, 72000);
        const auto out = run (p, in);
        const double fund = amplitudeAt (out, 1000.0);
        const double h2 = 20.0 * std::log10 (amplitudeAt (out, 2000.0) / fund);
        const double h3 = 20.0 * std::log10 (amplitudeAt (out, 3000.0) / fund);
        check (h2 > -60.0 && h2 < -10.0, "2nd harmonic present", h2);
        check (h3 > -60.0 && h3 < -5.0, "3rd harmonic present", h3);
        check (std::abs (mean (out)) < 0.002, "no DC offset", mean (out));

        // Loud 13 kHz tone at full drive: every alias product lands on a 1 kHz multiple.
        // Measured: 2x oversampling gives about -36 dB here, 4x gives about -68 dB.
        setParam (p, "drive", 1.0f);
        const auto hf = run (p, sine (13000.0, 0.5, 72000));
        const double hfFund = amplitudeAt (hf, 13000.0);
        double worstAlias = -200.0;
        for (int k = 1; k <= 23; ++k)
            if (k != 13)
                worstAlias = std::max (worstAlias, 20.0 * std::log10 (std::max (1e-9, amplitudeAt (hf, k * 1000.0)) / hfFund));
        check (worstAlias < -60.0, "worst alias product below -60 dB (13 kHz, 0.5 amp, full drive)", worstAlias);
    }

    std::printf ("7. transformer only compresses lows\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, false, true);
        setParam (p, "drive", 0.5f);
        const double lowLoud = 20.0 * std::log10 (amplitudeAt (run (p, sine (50.0, 0.9, 72000)), 50.0) / 0.9);
        const double highLoud = 20.0 * std::log10 (amplitudeAt (run (p, sine (4000.0, 0.9, 72000)), 4000.0) / 0.9);
        check (lowLoud < -0.3, "50 Hz compressed at high level", lowLoud);
        check (std::abs (highLoud) < 0.3, "4 kHz untouched", highLoud);
    }

    std::printf ("8. bypass is a pure delayed copy and keeps latency aligned\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        flat (p, true, true);
        setParam (p, "lowBoost", 8.0f);
        setParam (p, "bypass", 1.0f);
        std::vector<float> noise (48000);
        juce::Random rng (7);
        for (auto& s : noise) s = rng.nextFloat() * 0.5f - 0.25f;
        const auto out = run (p, noise);
        const int latency = p.getLatencySamples();
        double err = 0.0;
        for (size_t i = 24000; i < noise.size(); ++i)
            err = std::max (err, (double) std::abs (out[i] - noise[i - (size_t) latency]));
        check (err < 1e-6, "bypass == input delayed by latency", err);
        std::printf ("  reported latency: %d samples\n", latency);

        flat (p, false, false);
        const auto wet = run (p, noise);
        int bestLag = 0;
        double best = -1.0e30;
        for (int lag = 0; lag < 32; ++lag)
        {
            double c = 0.0;
            for (size_t i = 24000; i < noise.size(); ++i)
                c += (double) wet[i] * (double) noise[i - (size_t) lag];
            if (c > best) { best = c; bestLag = lag; }
        }
        check (std::abs (bestLag - latency) <= 1, "wet path lag matches reported latency", bestLag);
    }

    std::printf ("9. robustness\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        for (auto id : { "lowBoost", "lowAtten", "highBoost", "highAtten" }) setParam (p, id, 20.0f);
        setParam (p, "drive", 1.0f);
        setParam (p, "inputGain", 12.0f);
        setParam (p, "outputGain", 12.0f);
        setParam (p, "bandwidth", 0.0f);
        setParam (p, "tube", 1.0f);
        setParam (p, "transformer", 1.0f);
        std::vector<float> noise (96000);
        juce::Random rng (3);
        for (auto& s : noise) s = rng.nextFloat() * 2.0f - 1.0f;
        const auto out = run (p, noise);
        bool finite = true;
        double peak = 0.0;
        for (float s : out) { finite = finite && std::isfinite (s); peak = std::max (peak, (double) std::abs (s)); }
        check (finite, "all outputs finite at extreme settings");
        std::printf ("  peak at extreme settings: %.2f\n", peak);

        const auto big = run (p, sine (440.0, 0.5, 48000), 1024); // host block larger than prepared
        bool bigFinite = true;
        for (float s : big) bigFinite = bigFinite && std::isfinite (s);
        check (bigFinite, "host block (1024) larger than prepared block (256)");
    }

    std::printf ("10. presets\n");
    {
        FullteqAudioProcessor p;
        prepare (p);
        p.loadPreset (1);
        check (std::abs (*p.apvts.getRawParameterValue ("lowBoost") - 8.0f) < 1e-3f, "preset 1 sets low boost 8");
        p.loadPreset (2);
        check (FullteqAudioProcessor::indexFromChoice (*p.apvts.getRawParameterValue ("lowFreq"), 4) == 1, "preset 2 selects 30 Hz");
    }

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}