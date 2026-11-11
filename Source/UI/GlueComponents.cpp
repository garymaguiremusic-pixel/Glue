#include "GlueComponents.h"
#include "../CustomLookAndFeel.h"

using namespace juce;

//==============================================================================
// GlueKnob
//==============================================================================
GlueKnob::GlueKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String GlueKnob::currentValueText() const
{
    return valueText ? valueText (getValue()) : String (getValue(), 2);
}

String GlueKnob::getTooltip()
{
    return label.toUpperCase() + "  " + currentValueText();
}

void GlueKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        knobArea = bounds;
    }
    else
    {
        auto valueArea = bounds.removeFromBottom (13.0f);
        auto labelArea = bounds.removeFromBottom (13.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
        g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
        g.setColour ((hovering || dragging) ? GlueColours::blue.brighter (0.3f) : GlueColours::blue);
        g.drawFittedText (currentValueText(), valueArea.toNearestInt(), Justification::centred, 1, 0.7f);
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

void GlueKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void GlueKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = true;
    lastDragY = e.position.y;
    dragProportion = valueToProportionOfLength (getValue());
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void GlueKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    // Incremental drag in knob-travel space: the whole range in 250 px, Shift = 10x finer.
    // Stepped parameters accumulate unsnapped travel so slow drags still move them.
    const double pixels = (double) (lastDragY - e.position.y);
    lastDragY = e.position.y;

    const double perPixel = isStepped() ? 1.0 / 160.0 : (e.mods.isShiftDown() ? 1.0 / 2500.0 : 1.0 / 250.0);
    dragProportion = jlimit (0.0, 1.0, dragProportion + pixels * perPixel);

    const double v = proportionOfLengthToValue (dragProportion);
    if (std::abs (v - getValue()) > 1.0e-9)
        setValue (v, sendNotificationSync);
}

void GlueKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void GlueKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void GlueKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    if (isStepped())
    {
        setValueWithGesture (getValue() + (dir > 0.0f ? getInterval() : -getInterval()));
        return;
    }

    const double step = e.mods.isShiftDown() ? 0.002 : 0.02;
    const double p = jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + (dir > 0.0f ? step : -step));
    setValueWithGesture (proportionOfLengthToValue (p));
}

void GlueKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void GlueKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, GlueKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto r = p.getNormalisableRange();
    NormalisableRange<double> range ((double) r.start, (double) r.end, (double) r.interval, (double) r.skew, r.symmetricSkew);
    knob.setNormalisableRange (range);
    knob.setDefaultValue ((double) p.convertFrom0to1 (p.getDefaultValue()));

    if (! knob.valueText)
        knob.valueText = [&p] (double v) { return p.getText (p.convertTo0to1 ((float) v), 32); };

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// ChoiceButtons
//==============================================================================
ChoiceButtons::ChoiceButtons (RangedAudioParameter& p, const StringArray& labels, const String& tooltip)
    : param (p),
      attachment (p, [this] (float v)
      {
          const int index = roundToInt (v);
          for (int i = 0; i < buttons.size(); ++i)
              buttons[i]->setToggleState (i == index, dontSendNotification);
      })
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new TextButton (labels[i]));
        b->setTooltip (tooltip);
        b->onClick = [this, i] { select (i); };
        addAndMakeVisible (b);
    }
    attachment.sendInitialUpdate();
}

void ChoiceButtons::select (int index)
{
    attachment.setValueAsCompleteGesture ((float) index);
}

void ChoiceButtons::resized()
{
    const int n = buttons.size();
    const int gap = 4;
    const int w = (getWidth() - gap * (n - 1)) / jmax (1, n);
    for (int i = 0; i < n; ++i)
        buttons[i]->setBounds (i * (w + gap), 0, i == n - 1 ? getWidth() - i * (w + gap) : w, getHeight());
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, dimmed);

    const auto textColour = dimmed ? Colour (0xff8a9097) : Colours::white;
    auto textArea = b.reduced (6.0f, 0.0f);

    const auto font = CustomLookAndFeel::uiFont (11.0f, true);
    g.setFont (font);

    // value on the right; the name gets whatever is left (squeezed if needed)
    const float valueWidth = GlyphArrangement::getStringWidth (font, valueText) + 4.0f;
    auto nameArea = textArea.withTrimmedRight (valueWidth).toNearestInt();

    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawFittedText (nameText, nameArea.translated (0, 1), Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), Justification::centredRight);
    g.setColour (textColour);
    g.drawFittedText (nameText, nameArea, Justification::centredLeft, 1, 0.75f);
    g.drawText (valueText, textArea, Justification::centredRight);
}

//==============================================================================
// GainReductionMeter
//==============================================================================
float GainReductionMeter::dbToProportion (float db)
{
    // slightly expanded at the bottom so small amounts of glue are easy to read
    return std::pow (jlimit (0.0f, 20.0f, db) / 20.0f, 0.7f);
}

void GainReductionMeter::setValues (float needleDb, float peakDb)
{
    if (std::abs (needleDb - needle) > 0.01f || std::abs (peakDb - peak) > 0.05f)
    {
        needle = needleDb;
        peak = peakDb;
        repaint();
    }
}

void GainReductionMeter::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b, 4.0f);

    // faint blue backlight
    g.setGradientFill (ColourGradient (GlueColours::blueGlow.withAlpha (0.10f), b.getCentreX(), b.getBottom(),
                                       Colours::transparentBlack, b.getCentreX(), b.getY(), true));
    g.fillRoundedRectangle (b.reduced (2.0f), 3.0f);

    const Point<float> pivot (b.getCentreX(), b.getBottom() - 10.0f);
    const float R = std::min (b.getWidth() * 0.42f, b.getHeight() - 42.0f);
    constexpr float halfSweep = 1.0f;   // radians either side of vertical

    // 0 dB at the right end, 20 dB at the left end
    auto angleFor = [] (float db) { return halfSweep - 2.0f * halfSweep * dbToProportion (db); };
    auto pointAt = [pivot] (float angle, float r) { return Point<float> (pivot.x + std::sin (angle) * r, pivot.y - std::cos (angle) * r); };

    // scale track
    Path track;
    track.addCentredArc (pivot.x, pivot.y, R, R, 0.0f, -halfSweep, halfSweep, true);
    g.setColour (Colour (0xff0a0c0e));
    g.strokePath (track, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff1a2633));
    g.strokePath (track, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc (from 0 dB leftwards to the needle)
    const float needleAngle = angleFor (needle);
    if (needle > 0.05f)
    {
        Path value;
        value.addCentredArc (pivot.x, pivot.y, R, R, 0.0f, needleAngle, halfSweep, true);
        g.setColour (GlueColours::blueGlow.withAlpha (0.25f));
        g.strokePath (value, PathStrokeType (9.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (GlueColours::blue);
        g.strokePath (value, PathStrokeType (3.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ticks + numbers
    g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
    for (float db : { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 20.0f })
    {
        const bool major = db == 0.0f || db == 2.0f || db == 4.0f || db == 6.0f || db == 10.0f || db == 14.0f || db == 20.0f;
        const float a = angleFor (db);
        g.setColour (Colour (0xffb9c3cc).withAlpha (major ? 0.9f : 0.5f));
        g.drawLine (Line<float> (pointAt (a, R + 5.0f), pointAt (a, R + (major ? 11.0f : 8.0f))), major ? 1.4f : 0.9f);

        if (major)
        {
            const auto p = pointAt (a, R + 19.0f);
            g.setColour (Colour (0xffc8cdd2));
            g.drawText (String ((int) db), Rectangle<float> (20.0f, 10.0f).withCentre (p), Justification::centred);
        }
    }

    // peak-hold marker
    if (peak > 0.05f)
    {
        const float a = angleFor (peak);
        Path tri;
        const auto tip = pointAt (a, R - 4.0f);
        const auto l = pointAt (a - 0.04f, R + 3.0f), r = pointAt (a + 0.04f, R + 3.0f);
        tri.addTriangle (tip, l, r);
        g.setColour (Colour (0xffc9e6ff));
        g.fillPath (tri);
    }

    // label
    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    g.setColour (GlueColours::labelText.withAlpha (0.8f));
    g.drawText ("GR dB", Rectangle<float> (b.getX() + 8.0f, b.getBottom() - 16.0f, 50.0f, 12.0f), Justification::centredLeft);

    // needle
    const auto tipPoint = pointAt (needleAngle, R + 2.0f);
    const auto basePoint = pointAt (needleAngle, 8.0f);
    g.setColour (GlueColours::blueGlow.withAlpha (0.35f));
    g.drawLine (Line<float> (basePoint, tipPoint), 4.5f);
    g.setColour (Colour (0xffeef6ff));
    g.drawLine (Line<float> (basePoint, tipPoint), 1.6f);

    // pivot cap
    const auto cap = Rectangle<float> (14.0f, 14.0f).withCentre (pivot);
    g.setGradientFill (ColourGradient (Colour (0xff6a6f75), cap.getX(), cap.getY(), Colour (0xff141618), cap.getX(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (Colours::black.withAlpha (0.8f));
    g.drawEllipse (cap, 0.8f);
}

//==============================================================================
// Help
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (GlueColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (16.0f, 20.0f);
    CustomLookAndFeel::drawPanel (g, box, "Glue - Quick Help");

    const char* lines[] =
    {
        "THRESHOLD:  where compression starts (-40 .. 0 dB). The knee is soft and depends on the program",
        "MAKEUP:  gain after compression (0 .. +20 dB), applied to the compressed (wet) signal only",
        "ATTACK / RELEASE:  stepped like the hardware. AUTO release = fast recovery with a slow tail",
        "RATIO:  2:1, 4:1 or 10:1. The detector is a feedback design, so the ratio is program-dependent",
        "RANGE:  maximum gain reduction. Fully left = Full (no limit)",
        "MIX:  dry / compressed blend for parallel compression",
        "SC HPF:  high-pass filter on the detector only (Off, 20 .. 500 Hz) - stops the low end pumping",
        "EXT SC:  the detector listens to the plugin's sidechain input (no input connected = no compression)",
        "COMP IN:  switches the gain change (and makeup) on/off. PEAK CLIP: soft clipper at -0.5 dBFS",
        "OVERSAMPLING:  1x - 8x. LOW LATENCY uses minimum-phase IIR filters instead of linear-phase FIR",
        "Knobs: drag, Shift = fine, scroll wheel, double-click = default. Drag the corner to resize.",
        "Click the PEAK readout to reset the peak-hold marker.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (12.0f, 0.0f).withTrimmedTop (30.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.0f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click anywhere") ? GlueColours::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (21.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
