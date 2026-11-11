#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr int kWidth = GluePanel::kBaseWidth, kHeight = GluePanel::kBaseHeight;
    constexpr int kTopBarH = 64;
    constexpr float kBracketTop = 639.0f, kBracketBottom = 702.0f;
}

//==============================================================================
GluePanel::GluePanel (GlueAudioProcessor& p)
    : processor (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    createBackground();

    auto& apvts = processor.apvts;
    using namespace glue::ParamIDs;

    // Knobs ----------------------------------------------------------------------
    thresholdKnob.setReversed (true);   // 0 dB on the left, -40 dB on the right
    rangeKnob.setReversed (true);       // 0 dB on the left, Full on the right

    thresholdKnob.setScale ({ { 0.0, "0" }, { -20.0, "-20" }, { -40.0, "-40" } }, 9);
    rangeKnob.setScale     ({ { 0.0, "0" }, { -40.0, "-40" }, { (double) glue::kRangeMin, "FULL" } }, 9);
    makeupKnob.setScale    ({ { 0.0, "0" }, { 10.0, "10" }, { 20.0, "20" } }, 9);
    attackKnob.setScale    ({ { 0, ".01" }, { 1, ".1" }, { 2, ".3" }, { 3, "1" }, { 4, "3" }, { 5, "10" }, { 6, "30" } }, 0);
    ratioKnob.setScale     ({ { 0, "2" }, { 1, "4" }, { 2, "10" } }, 0);
    releaseKnob.setScale   ({ { 0, ".1" }, { 1, ".2" }, { 2, ".4" }, { 3, ".6" }, { 4, ".8" }, { 5, "1.2" }, { 6, "A" } }, 0);
    mixKnob.setScale       ({ { 0.0, "DRY" }, { 1.0, "WET" } }, 9);
    hpKnob.setScale        ({ { (double) glue::kScHpfOff - 1.0, "OFF" }, { 90.0, "90" }, { 500.0, "500" } }, 9);

    const std::pair<GlueKnob*, const char*> knobs[] =
    {
        { &thresholdKnob, threshold }, { &rangeKnob, range }, { &makeupKnob, makeup },
        { &attackKnob, attack }, { &ratioKnob, ratio }, { &releaseKnob, release },
        { &mixKnob, mix }, { &hpKnob, scHpf },
    };
    for (auto& [knob, id] : knobs)
    {
        knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (id), *knob));
        addAndMakeVisible (*knob);
    }

    // Buttons ----------------------------------------------------------------------
    const std::tuple<TextButton*, const char*, const char*> buttons[] =
    {
        { &bypassButton,   bypass,     "Bypass the whole plugin (latency is kept)" },
        { &compInButton,   compIn,     "Comp In: switches the gain change and the makeup gain on/off" },
        { &peakClipButton, peakClip,   "Peak Clip: soft clipper on the output, ceiling -0.5 dBFS" },
        { &extScButton,    extSc,      "Ext In: the detector listens to the sidechain input instead of the main input" },
        { &noiseButton,    noise,      "Analog: very low-level circuit noise in the detector (as the circuit model)" },
        { &lowLatButton,   lowLatency, "Low Latency: minimum-phase IIR oversampling filters instead of linear-phase FIR" },
    };
    for (auto& [button, id, tip] : buttons)
    {
        button->setClickingTogglesState (true);
        button->setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, *button));
        addAndMakeVisible (*button);
    }

    // Dropdowns ------------------------------------------------------------------
    for (int i = 0; i < glue::oversamplingNames().size(); ++i)
        oversamplingBox.addItem ("OS " + glue::oversamplingNames()[i], i + 1);
    oversamplingBox.setJustificationType (Justification::centredLeft);
    oversamplingAttachment = std::make_unique<ComboAttachment> (apvts, oversampling, oversamplingBox);
    addAndMakeVisible (oversamplingBox);

    for (int i = 0; i < glue::numFactoryPresets; ++i)
        presetBox.addItem (String (i + 1).paddedLeft ('0', 2) + "  " + glue::factoryPresets[i].name, i + 1);
    presetBox.setTooltip ("Presets");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
        lastShownProgram = index;
    };
    addAndMakeVisible (presetBox);

    // Meter / help --------------------------------------------------------------------
    meter.onClick = [this] { processor.gui.resetPeak.store (true); };
    lcdPeak.onClick = meter.onClick;
    lcdPeak.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (lcdGr);
    addAndMakeVisible (lcdPeak);
    meter.setTooltip ("Gain reduction. Click to reset the peak marker");
    meter.setMouseCursor (MouseCursor::PointingHandCursor);
    addAndMakeVisible (meter);

    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    setSize (kWidth, kHeight);   // last: resized() lays out every control created above
    timerCallback();
    startTimerHz (30);
}

GluePanel::~GluePanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

float GluePanel::rawParam (const char* id) const
{
    return processor.apvts.getRawParameterValue (id)->load();
}

//==============================================================================
void GluePanel::resized()
{
    // top bar
    helpButton.setBounds (262, 7, 20, 20);
    bypassButton.setBounds (288, 7, 64, 20);
    presetBox.setBounds (8, 35, 250, 22);
    oversamplingBox.setBounds (264, 35, 88, 22);

    // meter panel
    meterPanel = { 8.0f, 70.0f, 344.0f, 174.0f };
    meter.setBounds (18, 96, 324, 112);
    lcdGr.setBounds (18, 214, 160, 22);
    lcdPeak.setBounds (182, 214, 160, 22);

    // threshold / range / makeup
    thresholdKnob.setBounds (6, 248, 120, 128);
    rangeKnob.setBounds (120, 268, 120, 116);
    makeupKnob.setBounds (234, 248, 120, 128);

    // attack / ratio / release
    attackKnob.setBounds (6, 380, 120, 128);
    ratioKnob.setBounds (120, 402, 120, 102);
    releaseKnob.setBounds (234, 380, 120, 128);

    // comp in / mix / peak clip
    compInButton.setBounds (22, 534, 88, 26);
    mixKnob.setBounds (120, 500, 120, 110);
    peakClipButton.setBounds (250, 534, 88, 26);

    // sidechain
    hpKnob.setBounds (10, 594, 116, 100);
    extScButton.setBounds (148, 626, 64, 26);
    scStatusArea = { 130.0f, 658.0f, 100.0f, 12.0f };
    noiseButton.setBounds (252, 610, 92, 24);
    lowLatButton.setBounds (252, 640, 92, 24);

    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void GluePanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff34373b), 0.0f, 0.0f, Colour (0xff1c1e21), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x61ed);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.03f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 2200; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 15 + rng.nextInt (110);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.012f + rng.nextFloat() * 0.028f));
        g.fillRect (x, y, len, 1);
    }

    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.4f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void GluePanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Stylised lowercase wordmark: chrome "glue", blue "bus comp", and a swoosh.
    const float baseline = area.getBottom() - 4.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (24.0f, Font::bold | Font::italic)), "glue", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (16.0f, Font::plain)), "bus comp", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (GlueColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.0f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.0f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (GlueColours::blue.withAlpha (0.0f), sx, sy, GlueColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.4f, PathStrokeType::curved, PathStrokeType::rounded));
}

void GluePanel::drawCaption (Graphics& g, const String& text, Rectangle<float> area, float size)
{
    g.setFont (CustomLookAndFeel::uiFont (size, true));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawText (text, area.translated (0.0f, 1.0f), Justification::centred);
    g.setColour (Colour (0xfff1f3f5));
    g.drawText (text, area, Justification::centred);
}

void GluePanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top bar ----------------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopBarH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (GlueColours::bevelDark);
    g.drawHorizontalLine (kTopBarH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopBarH, 0.0f, (float) kWidth);

    drawWordmark (g, { 12.0f, 2.0f, 200.0f, 28.0f });

    // --- meter panel -----------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, meterPanel, "Gain Reduction");

    if (scStatus.isNotEmpty())
    {
        g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
        g.setColour (GlueColours::red);
        g.drawText (scStatus, scStatusArea, Justification::centred);
    }

    // --- sidechain bracket -------------------------------------------------------------
    const float cx = kWidth * 0.5f, left = 12.0f, right = (float) kWidth - 12.0f;
    Path bracket;
    bracket.startNewSubPath (left + 14.0f, kBracketTop);
    bracket.lineTo (left, kBracketTop);
    bracket.lineTo (left, kBracketBottom);
    bracket.lineTo (cx - 52.0f, kBracketBottom);
    bracket.startNewSubPath (cx + 52.0f, kBracketBottom);
    bracket.lineTo (right, kBracketBottom);
    bracket.lineTo (right, kBracketTop);
    bracket.lineTo (right - 6.0f, kBracketTop);
    bracket.startNewSubPath (128.0f, kBracketTop);   // stubs between the controls
    bracket.lineTo (146.0f, kBracketTop);
    bracket.startNewSubPath (214.0f, kBracketTop);
    bracket.lineTo (248.0f, kBracketTop);
    const PathStrokeType stroke (2.0f, PathStrokeType::mitered, PathStrokeType::square);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.strokePath (bracket, stroke, AffineTransform::translation (0.0f, 1.0f));
    g.setColour (GlueColours::headerBottom);
    g.strokePath (bracket, stroke);
    drawCaption (g, "SIDECHAIN", { cx - 52.0f, kBracketBottom - 9.0f, 104.0f, 18.0f }, 13.5f);
}

//==============================================================================
void GluePanel::timerCallback()
{
    using namespace glue::ParamIDs;
    auto& gui = processor.gui;

    const bool compOn = rawParam (compIn) > 0.5f && rawParam (bypass) < 0.5f;
    meter.setValues (gui.grNeedleDb.load(), gui.grPeakDb.load());
    meter.setAlpha (compOn ? 1.0f : 0.6f);

    lcdGr.setContent ("GR", String (gui.grNeedleDb.load(), 1) + " dB");
    lcdPeak.setContent ("PEAK HOLD", String (gui.grPeakDb.load(), 1) + " dB");
    lcdGr.setDimmed (! compOn);
    lcdPeak.setDimmed (! compOn);

    const String status = rawParam (extSc) > 0.5f && ! gui.auxConnected.load() ? "NO SC INPUT" : "";
    if (status != scStatus)
    {
        scStatus = status;
        repaint (scStatusArea.toNearestInt().expanded (2));
    }

    const int latency = gui.latency.load();
    if (latency != lastLatency)
    {
        lastLatency = latency;
        oversamplingBox.setTooltip ("Oversampling factor (more = cleaner, more CPU). Latency: "
                                    + (latency == 0 ? String ("none") : String (latency) + " samples"));
    }

    const int program = processor.getCurrentProgram();
    if (program != lastShownProgram)
    {
        lastShownProgram = program;
        presetBox.setSelectedId (program + 1, dontSendNotification);
    }
}

//==============================================================================
// Resizable editor: the panel is laid out at its base size and scaled as a whole.
//==============================================================================
GlueAudioProcessorEditor::GlueAudioProcessorEditor (GlueAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    // Read the remembered size first: setting up the resize limits below briefly
    // resizes the window, and that must not overwrite the saved size.
    const int savedWidth = processor.gui.editorWidth.load();

    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kWidth * 7 / 10, kHeight * 7 / 10, kWidth * 5 / 2, kHeight * 5 / 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (kWidth * 7 / 10, kWidth * 5 / 2, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void GlueAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.gui.editorWidth.store (getWidth());
}
