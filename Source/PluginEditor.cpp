#include "PluginEditor.h"
#include "Ui/PlateLookAndFeel.h"

//==============================================================================
// Colour palette
using T = PlateUi::Theme;
static const juce::Colour kBg       = T::background();
static const juce::Colour kPanel    = T::surface();
static const juce::Colour kHeader   = T::surfaceRaised();
static const juce::Colour kAccent   = T::accent();
static const juce::Colour kTextMain = T::text();
static const juce::Colour kTextDim  = T::textDim();
static const juce::Colour kDivider  = T::border();

// Use the shared warm-dark PlateLookAndFeel
using VocalDoublerLookAndFeel = PlateUi::PlateLookAndFeel;

//==============================================================================
// Voice Field Display — real-time oscilloscope.
// Captures the actual audio from the processor's scope ring buffer and draws
// three overlapping waveforms: dry (white), Voice 1 (blue), Voice 2 (amber).
// Pitch knob adds vertical separation; Timbre controls glow width;
// Amount fades voice traces in/out; Timing creates natural phase drift.
class VoiceFieldDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit VoiceFieldDisplay (VocalDoublerAudioProcessor& p) : proc (p)
    {
        startTimerHz (30);
    }

    void timerCallback() override
    {
        // Snapshot the circular scope buffers into linear order (oldest → newest)
        const int head = proc.scopeHead.load (std::memory_order_relaxed);
        for (int i = 0; i < kSnap; ++i)
        {
            int idx   = (head + i) % kSnap;
            snapIn[i] = proc.scopeIn[idx];
            snapV1[i] = proc.scopeV1[idx];
            snapV2[i] = proc.scopeV2[idx];
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto  b  = getLocalBounds().toFloat();
        const float bw = b.getWidth(), bh = b.getHeight();
        const float cy = bh * 0.5f;

        // ── Background ───────────────────────────────────────────────────────
        g.setColour (juce::Colour (5, 7, 13));
        g.fillRoundedRectangle (b, 5.0f);

        // ── Grid ─────────────────────────────────────────────────────────────
        g.setColour (juce::Colour (28, 48, 75).withAlpha (0.55f));
        g.fillRect (8.0f, cy - 0.5f, bw - 16.0f, 1.0f);          // centre line
        g.setColour (juce::Colour (20, 36, 58).withAlpha (0.28f));
        g.fillRect (8.0f, bh * 0.25f, bw - 16.0f, 1.0f);
        g.fillRect (8.0f, bh * 0.75f, bw - 16.0f, 1.0f);
        g.fillRect (bw * 0.25f, 8.0f, 1.0f, bh - 16.0f);
        g.fillRect (bw * 0.50f, 8.0f, 1.0f, bh - 16.0f);
        g.fillRect (bw * 0.75f, 8.0f, 1.0f, bh - 16.0f);

        // ── Parameters ───────────────────────────────────────────────────────
        const float amount = proc.apvts.getRawParameterValue ("amount")->load() / 100.0f;
        const float pitch  = proc.apvts.getRawParameterValue ("pitch")->load()  / 50.0f;
        const float timbre = proc.apvts.getRawParameterValue ("timbre")->load() / 100.0f;

        // Pitch drives vertical separation of the two voice traces
        const float pitchOff = pitch * bh * 0.16f;

        // Waveform drawing helper
        auto drawWave = [&] (float* data, float yCentre,
                              juce::Colour col, float alpha, float strokeW)
        {
            const float scaleY = bh * 0.30f;
            const float innerW = bw - 16.0f;

            juce::Path path;
            for (int i = 0; i < kSnap; ++i)
            {
                float x = 8.0f + (float)i / float(kSnap - 1) * innerW;
                float y = juce::jlimit (4.0f, bh - 4.0f,
                                        yCentre - data[i] * scaleY);
                if (i == 0) path.startNewSubPath (x, y);
                else        path.lineTo (x, y);
            }

            // Soft glow pass
            g.setColour (col.withAlpha (alpha * 0.22f));
            g.strokePath (path, juce::PathStrokeType (strokeW + 2.5f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // Main line
            g.setColour (col.withAlpha (alpha));
            g.strokePath (path, juce::PathStrokeType (strokeW,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        // Dry signal — always visible, subtle white/grey
        drawWave (snapIn, cy,
                  juce::Colour (200, 215, 255), 0.28f, 1.0f);

        // Voice 1 — blue, shifted up by pitch amount; glow scales with timbre
        if (amount > 0.01f)
            drawWave (snapV1, cy - pitchOff,
                      juce::Colour (65, 145, 210),
                      0.45f + amount * 0.45f,
                      1.4f + timbre * 0.8f);

        // Voice 2 — amber, shifted down by pitch amount
        if (amount > 0.01f)
            drawWave (snapV2, cy + pitchOff,
                      juce::Colour (210, 163, 65),
                      0.45f + amount * 0.45f,
                      1.4f + timbre * 0.8f);

        // ── Border ───────────────────────────────────────────────────────────
        g.setColour (juce::Colour (48, 82, 124).withAlpha (0.40f));
        g.drawRoundedRectangle (b.reduced (0.5f), 4.5f, 1.0f);

        // ── Label + legend ────────────────────────────────────────────────────
        g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.setColour (juce::Colour (115, 152, 195).withAlpha (0.40f));
        g.drawText ("SCOPE", 8, 3, 40, 9, juce::Justification::centredLeft, false);

        {
            int lx = (int)bw - 76;
            g.setFont (juce::Font (7.5f, juce::Font::bold));
            auto drawLegend = [&] (juce::Colour col, const char* lbl)
            {
                g.setColour (col.withAlpha (0.85f));
                g.fillEllipse ((float)lx, 4.0f, 5.0f, 5.0f);
                g.setColour (juce::Colour (145, 168, 210).withAlpha (0.65f));
                g.drawText (lbl, lx + 7, 1, 22, 10, juce::Justification::centredLeft, false);
                lx += 26;
            };
            drawLegend (juce::Colour (200, 215, 255), "DRY");
            drawLegend (juce::Colour ( 65, 145, 210), "V1");
            drawLegend (juce::Colour (210, 163,  65), "V2");
        }
    }

private:
    VocalDoublerAudioProcessor& proc;

    static constexpr int kSnap = VocalDoublerAudioProcessor::kScopeSize;
    float snapIn [kSnap] {};
    float snapV1 [kSnap] {};
    float snapV2 [kSnap] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceFieldDisplay)
};

//==============================================================================
VocalDoublerAudioProcessorEditor::VocalDoublerAudioProcessorEditor (VocalDoublerAudioProcessor& p)
    : AudioProcessorEditor (&p)
    , processorRef (p)
    , amountAtt (p.apvts, "amount", amountKnob.slider)
    , pitchAtt  (p.apvts, "pitch",  pitchKnob.slider)
    , timingAtt (p.apvts, "timing", timingKnob.slider)
    , widthAtt  (p.apvts, "width",  widthKnob.slider)
    , timbreAtt (p.apvts, "timbre", timbreKnob.slider)
    , rateAtt          (p.apvts, "rate",         rateKnob.slider)
    , pitchActiveAtt   (p.apvts, "activePitch",  pitchActiveBtn)
    , timingActiveAtt  (p.apvts, "activeTiming", timingActiveBtn)
    , widthActiveAtt   (p.apvts, "activeWidth",  widthActiveBtn)
    , timbreActiveAtt  (p.apvts, "activeTimbre", timbreActiveBtn)
    , rateActiveAtt    (p.apvts, "activeRate",   rateActiveBtn)
{
    laf = std::make_unique<VocalDoublerLookAndFeel>();
    setLookAndFeel (laf.get());

    setupKnob (amountKnob, "Mix");
    setupKnob (pitchKnob,  "Pitch Var");
    setupKnob (timingKnob, "Timing");
    setupKnob (widthKnob,  "Width");
    setupKnob (timbreKnob, "Timbre Var");
    setupKnob (rateKnob,   "Rate");

    amountKnob.slider.setTooltip ("Mix (0-100%) - Blend of the doubled voices into the output. "
                                   "At 0% only the dry signal passes through; at 100% you hear the full doubling effect.");
    pitchKnob.slider .setTooltip ("Pitch Variation (0-50 cents) - Maximum random pitch wander applied independently to each voice. "
                                   "Low values add subtle humanisation; high values give a wide, detuned chorus character.");
    timingKnob.slider.setTooltip ("Timing Offset (10-60 ms) - Delay between the two phantom voices and the dry signal. "
                                   "Short values (10-20 ms) feel tight and natural; longer values push toward slapback echo.");
    widthKnob.slider .setTooltip ("Stereo Width (0-100%) - How far apart the two voices are panned in the stereo field. "
                                   "At 0% both voices are centred (mono-safe); at 100% they sit hard left and right.");
    timbreKnob.slider.setTooltip ("Timbre Variation (0-100%) - Random high/low shelf EQ variation applied to each voice. "
                                   "Mimics the tonal differences between two separate takes of the same performance.");
    rateKnob.slider  .setTooltip ("Modulation Rate (0.1-3.0 Hz) - Speed of the Brownian-motion pitch and timing wander. "
                                   "Slow rates (< 0.5 Hz) feel organic; faster rates add more animated movement.");

    pitchActiveBtn .setTooltip ("Toggle Pitch Variation on/off");
    timingActiveBtn.setTooltip ("Toggle Timing Offset on/off - when off, voices play in sync with dry");
    widthActiveBtn .setTooltip ("Toggle Stereo Width on/off - when off, voices collapse to centre");
    timbreActiveBtn.setTooltip ("Toggle Timbre Variation on/off - when off, both voices have identical EQ");
    rateActiveBtn  .setTooltip ("Toggle Rate modulation on/off - when off, pitch wander is frozen");

    addAndMakeVisible (pitchActiveBtn);
    addAndMakeVisible (timingActiveBtn);
    addAndMakeVisible (widthActiveBtn);
    addAndMakeVisible (timbreActiveBtn);
    addAndMakeVisible (rateActiveBtn);

    voiceDisplay = std::make_unique<VoiceFieldDisplay> (processorRef);
    addAndMakeVisible (*voiceDisplay);

    zoomIndex = processorRef.editorZoomIndex;
    applyZoom();
    startTimerHz (30);
}

//==============================================================================
VocalDoublerAudioProcessorEditor::~VocalDoublerAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    stopTimer();
}

//==============================================================================
void VocalDoublerAudioProcessorEditor::timerCallback()
{
    repaint (gainReadoutBounds);
    updateKnobDimming();
}

void VocalDoublerAudioProcessorEditor::updateKnobDimming()
{
    auto dim = [](LabelledKnob& lk, bool active)
    {
        float a = active ? 1.0f : 0.38f;
        lk.slider.setAlpha (a);
        lk.label .setAlpha (a);
    };
    auto& avts = processorRef.apvts;
    dim (pitchKnob,  *avts.getRawParameterValue ("activePitch")  > 0.5f);
    dim (timingKnob, *avts.getRawParameterValue ("activeTiming") > 0.5f);
    dim (widthKnob,  *avts.getRawParameterValue ("activeWidth")  > 0.5f);
    dim (timbreKnob, *avts.getRawParameterValue ("activeTimbre") > 0.5f);
    dim (rateKnob,   *avts.getRawParameterValue ("activeRate")   > 0.5f);
}

//==============================================================================
constexpr float VocalDoublerAudioProcessorEditor::kZoomFactors[];
constexpr const char* VocalDoublerAudioProcessorEditor::kZoomLabels[];

void VocalDoublerAudioProcessorEditor::applyZoom()
{
    setSize (kBaseW, kBaseH);
    if (isShowing())
        setScaleFactor (kZoomFactors[zoomIndex]);
}

void VocalDoublerAudioProcessorEditor::visibilityChanged()
{
    if (isVisible())
    {
        setScaleFactor (kZoomFactors[zoomIndex]);
        if (! centred)
        {
            centred = true;
            juce::Component::SafePointer<juce::Component> safeThis (this);
            juce::MessageManager::callAsync ([safeThis]()
            {
                if (safeThis == nullptr) return;
                if (auto* peer = safeThis->getPeer())
                    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                        peer->setBounds (peer->getBounds().withCentre (d->userArea.getCentre()), false);
            });
        }
    }
}

//==============================================================================
void VocalDoublerAudioProcessorEditor::setupKnob (LabelledKnob& lk,
                                                   const juce::String& text)
{
    addAndMakeVisible (lk.slider);
    lk.label.setText (text, juce::dontSendNotification);
    lk.label.setFont (juce::Font (11.0f, juce::Font::bold));
    lk.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (lk.label);
}

//==============================================================================
void VocalDoublerAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();

    // ── Background ───────────────────────────────────────────────────────────
    PlateUi::drawBackground (g, getLocalBounds(), true);

    // ── Header bar ───────────────────────────────────────────────────────────
    PlateUi::drawHeaderBar (g, getLocalBounds(), 50, true);

    // ── OMEGADARREN brand ────────────────────────────────────────────────────
    PlateUi::drawBrandMark (g, { 84, 9, 130, 17 }, true);

    // Plugin title
    {
        juce::Font titleFont (20.0f, juce::Font::bold);
        g.setFont (titleFont);
        const juce::String wordA = "VOCAL";
        const juce::String wordB = " DOUBLER";
        float wA = titleFont.getStringWidthFloat (wordA);
        float wB = titleFont.getStringWidthFloat (wordB);
        float sx = (w - wA - wB) * 0.5f;
        g.setColour (PlateUi::Theme::text().withAlpha (0.88f));
        g.drawText (wordA, (int)sx, 15, (int)wA + 4, 20, juce::Justification::centredLeft, false);
        juce::ColourGradient tGrad (PlateUi::Theme::accentBright(), sx + wA, 15.f,
                                    PlateUi::Theme::accentDeep(),   sx + wA + wB, 35.f, false);
        g.setGradientFill (tGrad);
        g.drawText (wordB, (int)(sx + wA), 15, (int)wB + 4, 20, juce::Justification::centredLeft, false);
    }

    // Version
    g.setFont (juce::Font (8.5f));
    g.setColour (PlateUi::Theme::textDim().withAlpha (0.50f));
    g.drawText ("v1.7", w - 52, 38, 40, 10, juce::Justification::centredRight, false);

    // ── Zoom button (top-left of header) ─────────────────────────────────────
    {
        PlateUi::drawFloatingControl (g, zoomButtonBounds, kZoomLabels[zoomIndex], false);
    }

    // ── Tooltip toggle (pill + "Tool Tips" label to its right) ─────────────
    {
        const auto& tb = tooltipBtnBounds;
        const bool hovered = tb.contains (getMouseXYRelative().toInt());

        // Pill background
        juce::Colour pillBg = tooltipsEnabled
            ? kAccent.withAlpha (hovered ? 0.75f : 0.55f)
            : juce::Colour (20, 22, 36).withAlpha (hovered ? 0.80f : 0.60f);
        g.setColour (pillBg);
        g.fillRoundedRectangle (tb.toFloat(), (float)tb.getHeight() * 0.5f);

        // Pill border
        g.setColour (tooltipsEnabled ? kAccent.withAlpha (0.80f)
                                     : kDivider.withAlpha (0.50f));
        g.drawRoundedRectangle (tb.toFloat().reduced (0.5f),
                                (float)tb.getHeight() * 0.5f - 0.5f, 1.0f);

        // Thumb dot
        const float pad = 2.0f;
        const float tD  = (float)tb.getHeight() - pad * 2.0f;
        const float tX  = tooltipsEnabled
            ? (float)tb.getRight()  - pad - tD
            : (float)tb.getX()      + pad;
        g.setColour (tooltipsEnabled ? juce::Colours::white.withAlpha (0.95f)
                                     : juce::Colour (55, 70, 95));
        g.fillEllipse (tX, (float)tb.getY() + pad, tD, tD);
        if (tooltipsEnabled)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillEllipse (tX + tD * 0.20f, (float)tb.getY() + pad + tD * 0.12f,
                           tD * 0.40f, tD * 0.40f);
        }

        // "Tool Tips" label to the right of the pill
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.setColour (tooltipsEnabled ? kTextDim : kTextDim.withAlpha (0.40f));
        g.drawText ("Tool Tips", tb.getRight() + 4, tb.getY() - 1, 56, tb.getHeight() + 2,
                    juce::Justification::centredLeft, false);
    }

    // ── Section panels (recessed, with drop shadows + title bars) ───────────
    auto drawPanel = [&] (juce::Rectangle<int> b, const juce::String& title)
    {
        float bx = (float)b.getX(),  by = (float)b.getY();
        float bw = (float)b.getWidth(), bh = (float)b.getHeight();

        // Drop shadow beneath panel
        g.setColour (juce::Colour (0, 0, 0).withAlpha (0.55f));
        g.fillRoundedRectangle (bx + 2.0f, by + 3.0f, bw, bh, 8.0f);

        // Panel body gradient
        juce::ColourGradient panelBg (
            juce::Colour (24, 25, 38), bx, by,
            juce::Colour (14, 15, 24), bx, by + bh, false);
        g.setGradientFill (panelBg);
        g.fillRoundedRectangle (bx, by, bw, bh, 8.0f);

        // Top inner highlight (raised-edge illusion)
        g.setColour (juce::Colour (255, 255, 255).withAlpha (0.055f));
        g.fillRoundedRectangle (bx + 1.0f, by + 1.0f, bw - 2.0f, 1.5f, 0.75f);

        // Outer border
        g.setColour (kDivider.withAlpha (0.42f));
        g.drawRoundedRectangle (bx + 0.5f, by + 0.5f, bw - 1.0f, bh - 1.0f, 7.5f, 1.0f);

        // Title bar tint
        {
            juce::Path tp;
            tp.addRoundedRectangle (bx, by, bw, 15.0f, 8.0f, 8.0f, true, true, false, false);
            g.setColour (kAccent.withAlpha (0.10f));
            g.fillPath (tp);

            g.setColour (kDivider.withAlpha (0.55f));
            g.fillRect (bx + 1.0f, by + 15.0f, bw - 2.0f, 1.0f);

            g.setFont (juce::Font (8.5f, juce::Font::bold));
            g.setColour (kAccent.withAlpha (0.9f));
            g.drawText (title, b.getX() + 10, b.getY() + 1,
                        b.getWidth() - 20, 13, juce::Justification::centredLeft, false);
        }
    };

    drawPanel ({ 6,  49, 470, 137 }, "MIX & SPREAD");
    drawPanel ({ 6, 288, 470, 137 }, "VARIATION");

    // ── Auto Gain vertical meter ──────────────────────────────────────────────
    {
        const float trimDb   = processorRef.getGainTrimDb();
        const float kRangeDb = 6.0f;
        const bool  active   = std::abs (trimDb) > 0.05f;

        const int px = gainReadoutBounds.getX();
        const int pw = gainReadoutBounds.getWidth();
        const int ph = gainReadoutBounds.getHeight();
        const int py = gainReadoutBounds.getY();

        // Panel bg
        juce::ColourGradient mBg (
            juce::Colour (12, 13, 22), (float)px, (float)py,
            juce::Colour (17, 18, 29), (float)px, (float)(py + ph), false);
        g.setGradientFill (mBg);
        g.fillRect (gainReadoutBounds);

        // Left separator (double-line depth)
        g.setColour (juce::Colour (0, 0, 0).withAlpha (0.85f));
        g.fillRect (px, py, 1, ph);
        g.setColour (kDivider.withAlpha (0.35f));
        g.fillRect (px + 1, py, 1, ph);

        // Labels
        g.setFont (juce::Font (7.5f, juce::Font::bold));
        g.setColour (active ? kAccent : kTextDim.withAlpha (0.45f));
        g.drawText ("AUTO", px, py + 4,  pw, 10, juce::Justification::centred, false);
        g.drawText ("GAIN", px, py + 13, pw, 10, juce::Justification::centred, false);

        juce::String dbStr = (trimDb >= 0.0f ? "+" : "")
                             + juce::String (trimDb, 1) + "dB";
        g.setFont (juce::Font (8.5f, juce::Font::bold));
        g.setColour (active ? kTextMain : kTextDim.withAlpha (0.45f));
        g.drawText (dbStr, px, py + ph - 16, pw, 13, juce::Justification::centred, false);

        // Track
        const int trackX  = px + pw / 2 - 5;
        const int trackW  = 10;
        const int trackTop   = py + 26;
        const int trackBot   = py + ph - 22;
        const int trackH     = trackBot - trackTop;
        const int centerY    = trackTop + trackH / 2;

        // Inset shadow + track body
        g.setColour (juce::Colour (0, 0, 0).withAlpha (0.7f));
        g.fillRoundedRectangle ((float)trackX - 1, (float)trackTop - 1,
                                (float)trackW + 2, (float)trackH + 2, 3.0f);
        g.setColour (juce::Colour (10, 10, 18));
        g.fillRoundedRectangle ((float)trackX, (float)trackTop,
                                (float)trackW, (float)trackH, 2.0f);

        // Filled bar
        float norm  = juce::jlimit (-1.0f, 1.0f, trimDb / kRangeDb);
        int   barPx = static_cast<int> (std::abs (norm) * (float)(trackH / 2));

        if (barPx > 0)
        {
            bool isBoost = (norm > 0.0f);
            int  barY    = isBoost ? centerY - barPx : centerY;
            juce::Colour barBase = isBoost ? juce::Colour (65, 145, 210)
                                           : juce::Colour (60, 195, 100);
            g.setColour (barBase.withAlpha (0.22f));
            g.fillRoundedRectangle ((float)trackX - 2, (float)barY - 1,
                                    (float)trackW + 4, (float)barPx + 2, 2.0f);
            juce::ColourGradient barGrad (
                barBase.brighter (0.5f), (float)trackX, (float)barY,
                barBase,                 (float)trackX, (float)(barY + barPx), false);
            g.setGradientFill (barGrad);
            g.fillRoundedRectangle ((float)trackX, (float)barY,
                                    (float)trackW, (float)barPx, 2.0f);
        }

        // Zero line
        g.setColour (kDivider.withAlpha (0.8f));
        g.fillRect (trackX - 3, centerY - 1, trackW + 6, 2);

        // Ticks
        g.setFont (juce::Font (7.0f));
        const int kTicks[] = { 6, 3, 0, -3, -6 };
        for (int db : kTicks)
        {
            float frac  = (kRangeDb - (float)db) / (2.0f * kRangeDb);
            int   tickY = trackTop + static_cast<int> (frac * (float)trackH);
            bool  isZ   = (db == 0);
            g.setColour (isZ ? kDivider : kDivider.withAlpha (0.4f));
            g.fillRect (trackX - 3, tickY, trackW + 6, isZ ? 2 : 1);
            juce::String lbl = (db > 0 ? "+" : "") + juce::String (db);
            g.setColour (kTextDim.withAlpha (isZ ? 0.85f : 0.45f));
            g.drawText (lbl, px + 1, tickY - 5, pw / 2, 10,
                        juce::Justification::centredRight, false);
        }
    }
}

//==============================================================================
void VocalDoublerAudioProcessorEditor::resized()
{
    constexpr int kKnobW  = 80;
    constexpr int kKnobH  = 84;
    constexpr int kLabelH = 18;
    // Panel 1: y=49, title bar 15px -> knobs at y=65 (1px padding)
    // Panel 2: y=175, title bar 15px -> knobs at y=193 (3px padding)
    constexpr int kRow1Y  = 66;
    constexpr int kRow2Y  = 305;

    // Knob columns pinned to left 480px: centres at 80, 240, 400
    const std::array<int, 3> cx = { 80, 240, 400 };

    auto place = [&] (LabelledKnob& lk, int colIdx, int rowY)
    {
        int x = cx[static_cast<size_t> (colIdx)] - kKnobW / 2;
        lk.slider.setBounds (x, rowY,          kKnobW, kKnobH);
        lk.label .setBounds (x, rowY + kKnobH, kKnobW, kLabelH);
    };

    place (amountKnob, 0, kRow1Y);
    place (pitchKnob,  1, kRow1Y);
    place (timingKnob, 2, kRow1Y);

    place (widthKnob,  0, kRow2Y);
    place (timbreKnob, 1, kRow2Y);
    place (rateKnob,   2, kRow2Y);

    // Toggle buttons (34x14) centred under each knob label
    constexpr int kTogW = 34, kTogH = 14;
    const int togRow1 = kRow1Y + kKnobH + kLabelH + 2;
    const int togRow2 = kRow2Y + kKnobH + kLabelH + 2;

    // Row 1: no toggle for Mix (col 0); pitch (col 1), timing (col 2)
    pitchActiveBtn .setBounds (cx[1] - kTogW / 2, togRow1, kTogW, kTogH);
    timingActiveBtn.setBounds (cx[2] - kTogW / 2, togRow1, kTogW, kTogH);

    // Row 2: all three
    widthActiveBtn .setBounds (cx[0] - kTogW / 2, togRow2, kTogW, kTogH);
    timbreActiveBtn.setBounds (cx[1] - kTogW / 2, togRow2, kTogW, kTogH);
    rateActiveBtn  .setBounds (cx[2] - kTogW / 2, togRow2, kTogW, kTogH);

    voiceDisplay->setBounds (6, 187, 470, 100);
    gainReadoutBounds = { 482, 49, 56, getHeight() - 49 };
    zoomButtonBounds  = {   8, 10, 38, 26 };
    tooltipBtnBounds  = {  52, 17, 26, 14 };  // pill sits right of zoom button
}

//==============================================================================
void VocalDoublerAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (zoomButtonBounds.contains (e.getPosition()))
    {
        zoomIndex = (zoomIndex + 1) % 3;
        processorRef.editorZoomIndex = zoomIndex;
        applyZoom();
        repaint();
    }
    else if (tooltipBtnBounds.contains (e.getPosition()))
    {
        tooltipsEnabled = !tooltipsEnabled;
        tooltipWindow.setMillisecondsBeforeTipAppears (tooltipsEnabled ? 600 : 1000000);
        repaint();
    }
}

//==============================================================================
// Required for the editor to be created from the processor
juce::AudioProcessorEditor*
VocalDoublerAudioProcessor::createEditor()
{
    return new VocalDoublerAudioProcessorEditor (*this);
}
