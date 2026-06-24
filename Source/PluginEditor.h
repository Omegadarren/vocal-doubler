#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Ui/PlateLookAndFeel.h"

//==============================================================================
class VocalDoublerAudioProcessorEditor : public juce::AudioProcessorEditor
                                       , private juce::Timer
{
public:
    explicit VocalDoublerAudioProcessorEditor (VocalDoublerAudioProcessor&);
    ~VocalDoublerAudioProcessorEditor() override;

    void paint     (juce::Graphics&) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void applyZoom();
    void updateKnobDimming();

    VocalDoublerAudioProcessor& processorRef;

    // Custom LookAndFeel — stored as base type, defined in .cpp
    std::unique_ptr<juce::LookAndFeel_V4> laf;

    // Voice Field Display — concrete type defined in .cpp
    std::unique_ptr<juce::Component> voiceDisplay;

    int zoomIndex = 0;
    bool centred = false;
    static constexpr float kZoomFactors[]       = { 1.0f, 1.5f, 2.0f };
    static constexpr const char* kZoomLabels[]  = { "1x", "1.5x", "2x" };
    static constexpr int kBaseW = 540;
    static constexpr int kBaseH = 425;

    juce::Rectangle<int> gainReadoutBounds;
    juce::Rectangle<int> zoomButtonBounds;
    juce::Rectangle<int> tooltipBtnBounds;

    bool tooltipsEnabled = true;

    //==========================================================================
    struct LabelledKnob
    {
        juce::Slider slider { juce::Slider::RotaryVerticalDrag,
                              juce::Slider::TextBoxBelow };
        juce::Label  label;
    };

    LabelledKnob amountKnob, pitchKnob, timingKnob;
    LabelledKnob widthKnob,  timbreKnob, rateKnob;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    Attachment amountAtt, pitchAtt, timingAtt;
    Attachment widthAtt,  timbreAtt, rateAtt;

    void setupKnob (LabelledKnob& lk, const juce::String& text);

    // Per-effect on/off toggle buttons (not for Mix)
    juce::ToggleButton pitchActiveBtn, timingActiveBtn;
    juce::ToggleButton widthActiveBtn,  timbreActiveBtn, rateActiveBtn;

    using BtnAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    BtnAttachment pitchActiveAtt, timingActiveAtt;
    BtnAttachment widthActiveAtt,  timbreActiveAtt, rateActiveAtt;

    juce::TooltipWindow tooltipWindow { this, 600 };  // 600 ms delay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalDoublerAudioProcessorEditor)
};
