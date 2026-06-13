#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocalDoublerAudioProcessor::buildLayout()
{
    using Apf = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Apf> (
        "amount", "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<Apf> (
        "width", "Width",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<Apf> (
        "pitch", "Pitch Var",
        juce::NormalisableRange<float> (0.0f, 50.0f, 0.1f), 12.0f,
        juce::AudioParameterFloatAttributes().withLabel ("cents")));

    layout.add (std::make_unique<Apf> (
        "timing", "Timing",
        juce::NormalisableRange<float> (10.0f, 60.0f, 0.1f), 20.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    layout.add (std::make_unique<Apf> (
        "timbre", "Timbre Var",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    // Skewed range so the lower end (subtle, realistic) has more resolution
    layout.add (std::make_unique<Apf> (
        "rate", "Rate",
        juce::NormalisableRange<float> (0.1f, 3.0f, 0.01f, 0.5f), 0.3f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    return layout;
}

//==============================================================================
VocalDoublerAudioProcessor::VocalDoublerAudioProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "Parameters", buildLayout())
{
    mod1.init (0x1A2B3C4D);
    mod2.init (0xDEADBEEF);
}

//==============================================================================
bool VocalDoublerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output must be stereo (the two voices go L and R)
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Accept mono or stereo input; we sum to mono internally
    auto ins = layouts.getMainInputChannelSet();
    return (ins == juce::AudioChannelSet::mono() ||
            ins == juce::AudioChannelSet::stereo());
}

//==============================================================================
void VocalDoublerAudioProcessor::prepareToPlay (double newSampleRate, int /*samplesPerBlock*/)
{
    sampleRate = newSampleRate;

    // Allocate delay buffer (max 300 ms, +2 for interpolation safety)
    delayBufSize = static_cast<int> (kMaxDelayMs / 1000.0 * sampleRate) + 2;
    delayBuf.assign (static_cast<size_t> (delayBufSize), 0.0f);
    writePos = 0;

    // ── All-pass filter setup ────────────────────────────────────────────────
    // Voice 1 and 2 use different frequencies to create distinct phase tinting.
    // These frequencies are chosen to spread phase rotation across the audible band.
    static constexpr float kApfFreqs1[] = { 400.0f, 1100.0f, 2800.0f, 6500.0f };
    static constexpr float kApfFreqs2[] = { 550.0f, 1400.0f, 3600.0f, 8000.0f };

    for (int i = 0; i < 4; ++i)
    {
        apf1[i].setup (kApfFreqs1[i], static_cast<float> (sampleRate));
        apf2[i].setup (kApfFreqs2[i], static_cast<float> (sampleRate));
        apf1[i].reset();
        apf2[i].reset();
    }

    // ── Shelf filter initial setup ───────────────────────────────────────────
    // Will be updated each block from the timbre parameter; reset state here.
    auto sr = static_cast<float> (sampleRate);
    hiShelf1.makeHighShelf (3500.0f,  1.5f, 0.707f, sr);
    hiShelf2.makeHighShelf (3500.0f, -1.5f, 0.707f, sr);
    loShelf1.makeLowShelf  ( 300.0f, -1.0f, 0.707f, sr);
    loShelf2.makeLowShelf  ( 300.0f,  1.0f, 0.707f, sr);
    hiShelf1.reset();  hiShelf2.reset();
    loShelf1.reset();  loShelf2.reset();

    // ── Reset voice modulators ───────────────────────────────────────────────
    mod1.init (0x1A2B3C4D);
    mod2.init (0xDEADBEEF);

    // ── Auto gain compensation ───────────────────────────────────────────────
    // 200 ms time constant for RMS tracking — slow enough to never pump.
    // 50 ms for the trim smoothing — fast enough to settle quickly on load.
    rmsCoef  = std::exp (-1.0f / (0.200f * static_cast<float> (sampleRate)));
    trimCoef = std::exp (-1.0f / (0.050f * static_cast<float> (sampleRate)));
    inputRmsEnv = outputRmsEnv = 0.0f;
    gainTrim = 1.0f;
}

//==============================================================================
void VocalDoublerAudioProcessor::releaseResources() {}

//==============================================================================
// Linear-interpolated circular-buffer read.
// delaySamples is the number of samples behind writePos to read from.
float VocalDoublerAudioProcessor::readInterp (float delaySamples) const noexcept
{
    float rp = static_cast<float> (writePos) - delaySamples;

    // Wrap into [0, delayBufSize)
    if (rp < 0.0f)
        rp += static_cast<float> (delayBufSize);
    if (rp >= static_cast<float> (delayBufSize))
        rp -= static_cast<float> (delayBufSize);

    auto  i0   = static_cast<int> (rp);
    int   i1   = (i0 + 1) % delayBufSize;
    float frac = rp - static_cast<float> (i0);

    return delayBuf[static_cast<size_t> (i0)] * (1.0f - frac)
         + delayBuf[static_cast<size_t> (i1)] *        frac;
}

//==============================================================================
void VocalDoublerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Read parameters ──────────────────────────────────────────────────────
    const float amount = *apvts.getRawParameterValue ("amount") / 100.0f;
    const float width  = *apvts.getRawParameterValue ("width")  / 100.0f;
    const float pitch  = *apvts.getRawParameterValue ("pitch");
    const float timing = *apvts.getRawParameterValue ("timing");
    const float timbre = *apvts.getRawParameterValue ("timbre") / 100.0f;
    const float rate   = *apvts.getRawParameterValue ("rate");

    const float delaySamples = timing * static_cast<float> (sampleRate) / 1000.0f;

    // Modulation depth bound: never allow reading closer than 1 ms to the write head
    const float maxMod = juce::jmin (delaySamples * 0.75f,
                                     static_cast<float> (sampleRate) * 0.05f);

    // ── Update timbre shelf filters for this block ───────────────────────────
    // Voice 1: slightly brighter highs, leaner lows  (like closer mic placement)
    // Voice 2: slightly darker highs, fuller lows    (like farther mic placement)
    auto sr = static_cast<float> (sampleRate);
    const float hiGain1 =  timbre * 3.0f;    // up to +3 dB
    const float hiGain2 = -timbre * 3.0f;    // up to -3 dB
    const float loGain1 = -timbre * 2.0f;
    const float loGain2 =  timbre * 2.0f;
    hiShelf1.makeHighShelf (3500.0f, hiGain1, 0.707f, sr);
    hiShelf2.makeHighShelf (3500.0f, hiGain2, 0.707f, sr);
    loShelf1.makeLowShelf  ( 300.0f, loGain1, 0.707f, sr);
    loShelf2.makeLowShelf  ( 300.0f, loGain2, 0.707f, sr);

    const float dryGain = 1.0f - amount;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // ── Sum input to mono ────────────────────────────────────────────────
        float inputSample = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            inputSample += buffer.getReadPointer (ch)[i];
        if (numChannels > 1)
            inputSample *= 0.5f;

        // Write to delay buffer
        delayBuf[static_cast<size_t> (writePos)] = inputSample;

        // ── Advance Brownian-motion modulators ───────────────────────────────
        const float m1 = mod1.tick (pitch, rate, maxMod);
        const float m2 = mod2.tick (pitch, rate, maxMod);

        // Convert modulation to effective delay (must stay positive)
        const float d1 = juce::jmax (delaySamples - m1, 1.0f);
        const float d2 = juce::jmax (delaySamples - m2, 1.0f);

        // ── Read two voices from different delay positions ───────────────────
        float v1 = readInterp (d1);
        float v2 = readInterp (d2);

        // ── All-pass filter chains (phase coloration) ────────────────────────
        for (auto& ap : apf1) v1 = ap.process (v1);
        for (auto& ap : apf2) v2 = ap.process (v2);

        // ── Shelf filters (timbre coloration) ────────────────────────────────
        v1 = hiShelf1.process (loShelf1.process (v1));
        v2 = hiShelf2.process (loShelf2.process (v2));

        // ── Oscilloscope capture ──────────────────────────────────────────────
        {
            int sh = scopeHead.load (std::memory_order_relaxed);
            scopeIn[sh] = inputSample;
            scopeV1[sh] = v1;
            scopeV2[sh] = v2;
            scopeHead.store ((sh + 1) % kScopeSize, std::memory_order_relaxed);
        }

        // ── Stereo width blend ───────────────────────────────────────────────
        // width=0 → both outputs carry (v1+v2)/2 (mono double)
        // width=1 → L carries v1 only, R carries v2 only (full stereo spread)
        const float mono = (v1 + v2) * 0.5f;
        const float wetL = mono + (v1 - mono) * width;
        const float wetR = mono + (v2 - mono) * width;

        float outSampleL = dryGain * inputSample + amount * wetL;
        float outSampleR = (outR != nullptr) ? dryGain * inputSample + amount * wetR
                                             : outSampleL;

        // ── Auto gain compensation ───────────────────────────────────────────
        // Track squared amplitudes (proportional to power) with slow envelopes.
        inputRmsEnv  = rmsCoef * inputRmsEnv  + (1.0f - rmsCoef) * (inputSample * inputSample);
        float outMono = (outSampleL + outSampleR) * 0.5f;
        outputRmsEnv = rmsCoef * outputRmsEnv + (1.0f - rmsCoef) * (outMono * outMono);

        // Derive target trim: sqrt(inPower / outPower) = RMS ratio.
        // Only adjust when wet signal is present and output is above noise floor.
        float targetTrim = 1.0f;
        if (amount > 0.01f && outputRmsEnv > 1.0e-8f)
        {
            targetTrim = std::sqrt (inputRmsEnv / outputRmsEnv);
            targetTrim = juce::jlimit (0.5f, 1.5f, targetTrim);  // clamp +/-6 dB
        }
        // Smooth the trim to prevent any step changes
        gainTrim = trimCoef * gainTrim + (1.0f - trimCoef) * targetTrim;

        outL[i] = outSampleL * gainTrim;
        if (outR != nullptr)
            outR[i] = outSampleR * gainTrim;

        writePos = (writePos + 1) % delayBufSize;
    }
}

//==============================================================================
void VocalDoublerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void VocalDoublerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// JUCE plugin factory
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalDoublerAudioProcessor();
}
