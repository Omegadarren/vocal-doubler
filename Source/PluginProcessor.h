#pragma once
#include <JuceHeader.h>
#include <random>
#include <array>
#include <cmath>

//==============================================================================
/**
 * Vocal Doubler — ADT (Artificial Double Tracking) VST3 plugin.
 *
 * Two independent voices are generated from the input via:
 *   • Brownian-motion delay modulation  → pitch + timing variation
 *   • All-pass filter chains            → phase coloration
 *   • High/low shelf EQ                 → timbre difference
 *
 * Parameters:
 *   amount  – wet/dry mix  (0–100 %)
 *   width   – stereo spread (0–100 %)
 *   pitch   – pitch variation magnitude (0–50 cents)
 *   timing  – base pre-delay / timing offset (10–60 ms)
 *   timbre  – EQ difference between voices (0–100 %)
 *   rate    – wander speed of pitch modulation (0.1–3.0 Hz)
 */
class VocalDoublerAudioProcessor : public juce::AudioProcessor
{
public:
    VocalDoublerAudioProcessor();
    ~VocalDoublerAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool  acceptsMidi()  const override { return false; }
    bool  producesMidi() const override { return false; }
    bool  isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    //==========================================================================
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Oscilloscope capture buffers — written on audio thread, read by the editor.
    // Per-element float writes are naturally atomic on x86; the slight data race
    // is benign for a visual display (worst case: one mildly glitched frame).
    static constexpr int kScopeSize = 512;
    float scopeIn [kScopeSize] {};
    float scopeV1 [kScopeSize] {};
    float scopeV2 [kScopeSize] {};
    std::atomic<int> scopeHead { 0 };

    // Current auto-gain trim in dB — read by the editor for display
    float getGainTrimDb() const noexcept
    {
        return 20.0f * std::log10 (juce::jmax (gainTrim, 1.0e-6f));
    }

    // Editor zoom level (0=1x, 1=1.5x, 2=2x) — persisted with plugin state
    int editorZoomIndex = 1;

private:
    //==========================================================================
    // Delay buffer  (mono, max 300 ms)
    static constexpr int kMaxDelayMs = 300;
    std::vector<float> delayBuf;
    int   delayBufSize = 0;
    int   writePos     = 0;

    //==========================================================================
    // 1st-order all-pass filter for phase coloration.
    // y[n] = c*(x[n] - y[n-1]) + x[n-1]
    struct APF
    {
        float c = 0.0f, x1 = 0.0f, y1 = 0.0f;

        void setup (float freqHz, float sr) noexcept
        {
            float t = std::tan (juce::MathConstants<float>::pi * freqHz / sr);
            c = (t - 1.0f) / (t + 1.0f);
        }

        float process (float x) noexcept
        {
            float y = c * (x - y1) + x1;
            x1 = x;  y1 = y;
            return y;
        }

        void reset() noexcept { x1 = y1 = 0.0f; }
    };

    // Four all-pass stages per voice, at different frequencies
    std::array<APF, 4> apf1, apf2;

    //==========================================================================
    // Biquad filter — transposed direct form II.
    // Implements Audio EQ Cookbook high/low shelf.
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float s1 = 0, s2 = 0;

        void makeHighShelf (float freqHz, float gainDb, float q, float sr) noexcept
        {
            float A   = std::pow (10.0f, gainDb / 40.0f);
            float w0  = juce::MathConstants<float>::twoPi * freqHz / sr;
            float sw  = std::sin (w0), cw = std::cos (w0);
            float sqA = std::sqrt (A);
            float alp = sw / (2.0f * q);
            float a0  = (A+1) - (A-1)*cw + 2*sqA*alp;
            b0 =  A * ((A+1) + (A-1)*cw + 2*sqA*alp) / a0;
            b1 = -2*A* ((A-1) + (A+1)*cw)             / a0;
            b2 =  A * ((A+1) + (A-1)*cw - 2*sqA*alp) / a0;
            a1 =  2 * ((A-1) - (A+1)*cw)              / a0;
            a2 =      ((A+1) - (A-1)*cw - 2*sqA*alp) / a0;
        }

        void makeLowShelf (float freqHz, float gainDb, float q, float sr) noexcept
        {
            float A   = std::pow (10.0f, gainDb / 40.0f);
            float w0  = juce::MathConstants<float>::twoPi * freqHz / sr;
            float sw  = std::sin (w0), cw = std::cos (w0);
            float sqA = std::sqrt (A);
            float alp = sw / (2.0f * q);
            float a0  = (A+1) + (A-1)*cw + 2*sqA*alp;
            b0 =  A * ((A+1) - (A-1)*cw + 2*sqA*alp) / a0;
            b1 = 2*A* ((A-1) - (A+1)*cw)              / a0;
            b2 =  A * ((A+1) - (A-1)*cw - 2*sqA*alp) / a0;
            a1 = -2 * ((A-1) + (A+1)*cw)              / a0;
            a2 =      ((A+1) + (A-1)*cw - 2*sqA*alp) / a0;
        }

        float process (float x) noexcept
        {
            float y = b0*x + s1;
            s1 = b1*x - a1*y + s2;
            s2 = b2*x - a2*y;
            return y;
        }

        void reset() noexcept { s1 = s2 = 0.0f; }
    };

    Biquad hiShelf1, loShelf1;   // voice 1: brighter / leaner lows
    Biquad hiShelf2, loShelf2;   // voice 2: darker  / fuller lows

    //==========================================================================
    // Brownian-motion delay-position modulator.
    //
    // The delay read-pointer is shifted by `pos` samples relative to the base
    // delay.  Because pos changes continuously (driven by vel), the effective
    // read rate differs from 1.0, creating a Doppler pitch shift.
    //
    // At steady state, vel has std-dev ≈ pitchCents/1731 samples/sample, which
    // matches the target pitch deviation via the Doppler relationship:
    //   Δcents ≈ 1731 · (dDelay/dt)
    //
    // The `rate` parameter controls the velocity correlation time
    // (1/(1–alpha) samples); higher rate → faster direction reversals.
    struct VoiceMod
    {
        float pos = 0.0f, vel = 0.0f;
        std::mt19937 rng;
        std::uniform_real_distribution<float> dist { -1.0f, 1.0f };

        void init (uint32_t seed) noexcept
        {
            rng.seed (seed);
            pos = vel = 0.0f;
        }

        // Returns delay-position offset in samples.
        // pitchCents : desired pitch-variation magnitude
        // rate       : wander speed in Hz (0.1–3.0)
        // maxSamples : hard boundary (= base delay × 0.75)
        float tick (float pitchCents, float rate, float maxSamples) noexcept
        {
            float alpha = 1.0f - juce::jmax (rate, 0.05f) * 1.0e-4f;
            float sigma = (pitchCents / 1731.0f) * std::sqrt (2.0f * (1.0f - alpha));

            vel = alpha * vel + sigma * dist (rng);
            pos += vel;

            if (pos >  maxSamples) { pos =  maxSamples; vel = -std::abs (vel) * 0.7f; }
            if (pos < -maxSamples) { pos = -maxSamples; vel =  std::abs (vel) * 0.7f; }

            return pos;
        }
    };

    VoiceMod mod1, mod2;

    //==========================================================================
    // Auto gain compensation.
    // Two slow RMS envelope followers (one on input, one on output) drive a
    // smoothed gain trim so bypass/engage causes no audible level jump.
    // Time constants are long enough (~200 ms) to avoid any pumping.
    float inputRmsEnv  = 0.0f;
    float outputRmsEnv = 0.0f;
    float gainTrim     = 1.0f;
    float rmsCoef      = 0.0f;   // per-sample decay for RMS envelopes
    float trimCoef     = 0.0f;   // per-sample decay for gain-trim smoothing

    //==========================================================================
    double sampleRate = 44100.0;

    // Linear-interpolated read from the circular delay buffer.
    // delaySamples must be > 0 and < delayBufSize.
    float readInterp (float delaySamples) const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout buildLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalDoublerAudioProcessor)
};
