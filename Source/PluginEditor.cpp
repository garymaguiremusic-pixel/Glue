#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr int kWidth = GluePanel::kBaseWidth, kHeight = GluePanel::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 368;
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
    const std::pair<GlueKnob*, const char*> knobs[] =
    {
        { &thresholdKnob, threshold }, { &makeupKnob, makeup }, { &attackKnob, attack }, { &releaseKnob, release },
        { &rangeKnob, range }, { &mixKnob, mix }, { &scHpfKnob, scHpf },
    };
    for (auto& [knob, id] : knobs)
    {
        knobAttachments.push_back (std::make_unique<KnobAttachment> (*apvts.getParameter (id), *knob));
        addAndMakeVisible (*knob);
    }

    ratioButtons = std::make_unique<ChoiceButtons> (*apvts.getParameter (ratio), glue::ratioNames(),
                                                    "Compression ratio");
    addAndMakeVisible (*ratioButtons);

    // Buttons ----------------------------------------------------------------------
    const std::tuple<TextButton*, const char*, const char*> buttons[] =
    {
        { &bypassButton,     bypass,     "Bypass the whole plugin (latency is kept)" },
        { &compInButton,     compIn,     "Comp In: switches the gain change and the makeup gain on/off" },
        { &peakClipButton,   peakClip,   "Peak Clip: soft clipper on the output, ceiling -0.5 dBFS" },
        { &extScButton,      extSc,      "Ext SC: the detector listens to the sidechain input instead of the main input" },
        { &noiseButton,      noise,      "Analog: very low-level circuit noise in the detector (as the circuit model)" },
        { &lowLatencyButton, lowLatency, "Low Latency: minimum-phase IIR oversampling filters instead of linear-phase FIR" },
    };
    for (auto& [button, id, tip] : buttons)
    {
        button->setClickingTogglesState (true);
        button->setTooltip (tip);
        buttonAttachments.push_back (std::make_unique<ButtonAttachment> (apvts, id, *button));
        addAndMakeVisible (*button);
    }

    // Dropdowns ------------------------------------------------------------------
    oversamplingBox.addItemList (glue::oversamplingNames(), 1);
    oversamplingBox.setTooltip ("Oversampling factor for the compressor (more = cleaner, more CPU)");
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

    // Displays ------------------------------------------------------------------------
    for (auto* c : { &lcdThreshold, &lcdMakeup, &lcdGr, &lcdPeak })
        addAndMakeVisible (*c);
    lcdPeak.onClick = [this] { processor.gui.resetPeak.store (true); };
    lcdPeak.setMouseCursor (MouseCursor::PointingHandCursor);
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
    // Row 1: Threshold / Makeup | Attack / Release / Ratio | Gain Reduction
    compPanel = { 8.0f,   40.0f, 232.0f, 180.0f };
    dynPanel  = { 246.0f, 40.0f, 150.0f, 180.0f };
    grPanel   = { 402.0f, 40.0f, 190.0f, 180.0f };

    // Row 2: Range / Mix | Sidechain | Output / Quality
    rangePanel = { 8.0f,   226.0f, 150.0f, 136.0f };
    scPanel    = { 164.0f, 226.0f, 150.0f, 136.0f };
    outPanel   = { 320.0f, 226.0f, 272.0f, 136.0f };

    // top strip
    bypassButton.setBounds (330, 8, 64, 19);
    presetBox.setBounds (450, 7, 142, 21);

    // threshold / makeup
    thresholdKnob.setBounds (15, 62, 106, 106);
    makeupKnob.setBounds (127, 62, 106, 106);
    lcdThreshold.setBounds (16, 182, 104, 22);
    lcdMakeup.setBounds (128, 182, 104, 22);

    // dynamics
    attackKnob.setBounds (252, 62, 68, 96);
    releaseKnob.setBounds (322, 62, 68, 96);
    ratioButtons->setBounds (256, 184, 130, 22);

    // gain reduction
    meter.setBounds (410, 64, 174, 102);
    lcdGr.setBounds (410, 170, 174, 21);
    lcdPeak.setBounds (410, 194, 174, 21);

    // range / mix
    rangeKnob.setBounds (14, 252, 68, 100);
    mixKnob.setBounds (84, 252, 68, 100);

    // sidechain
    scHpfKnob.setBounds (168, 252, 72, 100);
    extScButton.setBounds (246, 270, 60, 22);
    scStatusArea = { 242.0f, 298.0f, 68.0f, 12.0f };

    // output / quality
    compInButton.setBounds (330, 254, 84, 22);
    peakClipButton.setBounds (330, 282, 84, 22);
    noiseButton.setBounds (330, 310, 84, 22);
    oversamplingBox.setBounds (426, 268, 156, 22);
    lowLatencyButton.setBounds (426, 296, 156, 22);
    latencyArea = { 426.0f, 324.0f, 156.0f, 12.0f };

    helpButton.setBounds (kWidth - 26, kBottomBarY + 6, 20, 20);
    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void GluePanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x61ed);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 1700; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    // soft vignette
    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void GluePanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Stylised lowercase wordmark: chrome "glue", blue "bus comp", and a swoosh.
    const float baseline = area.getCentreY() + 9.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "glue", area.getX(), baseline);
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
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "bus comp", mainBounds.getRight() + 7.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (GlueColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (GlueColours::blue.withAlpha (0.0f), sx, sy, GlueColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void GluePanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (GlueColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });
    CustomLookAndFeel::drawSmallLabel (g, "Preset", { 400.0f, 8.0f, 44.0f, 19.0f }, Justification::centredRight);

    // --- panels ---------------------------------------------------------------------
    CustomLookAndFeel::drawPanel (g, compPanel,  "Threshold / Makeup");
    CustomLookAndFeel::drawPanel (g, dynPanel,   "Attack / Release");
    CustomLookAndFeel::drawPanel (g, grPanel,    "Gain Reduction");
    CustomLookAndFeel::drawPanel (g, rangePanel, "Range / Mix");
    CustomLookAndFeel::drawPanel (g, scPanel,    "Sidechain");
    CustomLookAndFeel::drawPanel (g, outPanel,   "Output / Quality");

    // divider + ratio label in the dynamics panel
    g.setColour (GlueColours::bevelDark);
    g.drawHorizontalLine (164, 256.0f, 386.0f);
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine (165, 256.0f, 386.0f);
    CustomLookAndFeel::drawSmallLabel (g, "Ratio", { 256.0f, 169.0f, 130.0f, 12.0f }, Justification::centredLeft);

    // sidechain status
    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    g.setColour (scStatusWarn ? GlueColours::red : GlueColours::labelText.withAlpha (0.8f));
    g.drawText (scStatus, scStatusArea, Justification::centred);

    // output panel: divider and labels
    g.setColour (GlueColours::bevelDark);
    g.drawVerticalLine (420, 248.0f, 352.0f);
    g.setColour (Colours::white.withAlpha (0.07f));
    g.drawVerticalLine (421, 248.0f, 352.0f);
    CustomLookAndFeel::drawSmallLabel (g, "Oversampling", { 426.0f, 254.0f, 156.0f, 12.0f }, Justification::centredLeft);
    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    g.setColour (GlueColours::labelText.withAlpha (0.8f));
    g.drawText (latencyText, latencyArea, Justification::centredLeft);

    // --- bottom bar -----------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 7.0f, 70.0f, 18.0f);
    g.setGradientFill (ColourGradient (GlueColours::headerTop, 0.0f, tab.getY(), GlueColours::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (GlueColours::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (GlueColours::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("BUS COMP", tab, Justification::centred);

    auto inset = bar.reduced (90.0f, 7.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffc8cdd2));
    g.drawText ("GLUE", inset, Justification::centred);
}

//==============================================================================
void GluePanel::timerCallback()
{
    using namespace glue::ParamIDs;
    auto& gui = processor.gui;

    lcdThreshold.setContent ("THRESH", glue::formatDb (rawParam (threshold)));
    lcdMakeup.setContent ("MAKEUP", glue::formatDb (rawParam (makeup)));

    const bool compOn = rawParam (compIn) > 0.5f && rawParam (bypass) < 0.5f;
    lcdGr.setContent ("GR", String (gui.grNeedleDb.load(), 1) + " dB");
    lcdPeak.setContent ("PEAK HOLD", String (gui.grPeakDb.load(), 1) + " dB");
    lcdGr.setDimmed (! compOn);
    lcdPeak.setDimmed (! compOn);
    meter.setValues (gui.grNeedleDb.load(), gui.grPeakDb.load());
    meter.setAlpha (compOn ? 1.0f : 0.55f);

    // sidechain status
    String status;
    bool warn = false;
    if (rawParam (extSc) > 0.5f)
    {
        warn = ! gui.auxConnected.load();
        status = warn ? "NO SC INPUT" : "EXT ACTIVE";
    }
    else
    {
        status = "INTERNAL";
    }
    if (status != scStatus || warn != scStatusWarn)
    {
        scStatus = status;
        scStatusWarn = warn;
        repaint (scStatusArea.toNearestInt().expanded (2));
    }

    const int latency = gui.latency.load();
    const String lat = latency == 0 ? String ("No latency") : "Latency " + String (latency) + " samples";
    if (lat != latencyText)
    {
        latencyText = lat;
        repaint (latencyArea.toNearestInt().expanded (2));
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
