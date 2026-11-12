#include "GlueComponents.h"
#include "../CustomLookAndFeel.h"

using namespace juce;

namespace
{
    const Colour kScaleText  { 0xffdfe3e6 };
        constexpr float kMeterHalfSweep = 0.66f;   // radians either side of vertical

    Point<float> polar (Point<float> c, float angle, float r)
    {
        return { c.x + std::sin (angle) * r, c.y - std::cos (angle) * r };
    }
}

//==============================================================================
// GlueKnob
//==============================================================================
GlueKnob::GlueKnob (const String& name, bool largeChromeKnob, float sweepRadians, float scaleFactor)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), title (name), large (largeChromeKnob),
      sweep (sweepRadians), sizeScale (scaleFactor)
{
    setName (name);
}

double GlueKnob::proportionOfLengthToValue (double proportion)
{
    return Slider::proportionOfLengthToValue (reversed ? 1.0 - proportion : proportion);
}

double GlueKnob::valueToProportionOfLength (double value)
{
    const double p = Slider::valueToProportionOfLength (value);
    return reversed ? 1.0 - p : p;
}

String GlueKnob::getTooltip()
{
    return title.toUpperCase() + "  " + (valueText ? valueText (getValue()) : String (getValue(), 2));
}

void GlueKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto valueArea = bounds.removeFromBottom (15.0f);
    auto nameArea = bounds.removeFromBottom (18.0f);

    // R = outer radius of the drawn knob (including its own tick ring)
    const float R = std::max (12.0f, std::min ((bounds.getWidth() - 50.0f) * 0.5f, (bounds.getHeight() - 26.0f) * 0.5f) * sizeScale);
    const Point<float> c (bounds.getCentreX(), bounds.getBottom() - R - 6.0f);

    auto angleFor = [this] (double proportion) { return -sweep + 2.0f * sweep * (float) proportion; };

    // scale numbers around the knob
    const auto font = CustomLookAndFeel::uiFont (std::max (9.5f, std::min (11.5f, R * 0.26f)), true);
    g.setFont (font);
    for (auto& s : scale)
    {
        const float a = angleFor (valueToProportionOfLength (s.value));
        const float tw = GlyphArrangement::getStringWidth (font, s.text);
        const float dist = R + 6.0f + std::abs (std::sin (a)) * tw * 0.5f + std::abs (std::cos (a)) * 3.0f;
        const auto box = Rectangle<float> (tw + 4.0f, 13.0f).withCentre (polar (c, a, dist));
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawText (s.text, box.translated (0.0f, 1.0f), Justification::centred);
        g.setColour (kScaleText);
        g.drawText (s.text, box, Justification::centred);
    }

    const auto knobArea = Rectangle<float> (R * 2.0f, R * 2.0f).withCentre (c);
    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       (float) valueToProportionOfLength (getValue()), -sweep, sweep, *this);

    // name
    g.setFont (CustomLookAndFeel::uiFont (14.0f, true));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawText (title, nameArea.translated (0.0f, 1.0f), Justification::centred);
    g.setColour (GlueColours::headerText.withMultipliedAlpha (isEnabled() ? 1.0f : 0.5f));
    g.drawText (title, nameArea, Justification::centred);

    // live value readout (always visible; brighter while dragging)
    const String value = valueText ? valueText (getValue()) : String (getValue(), 1);
    g.setFont (CustomLookAndFeel::uiFont (12.5f, true));
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawText (value, valueArea.translated (0.0f, 1.0f), Justification::centred);
    g.setColour (dragging ? Colour (0xffc9e6ff) : GlueColours::blue);
    g.drawText (value, valueArea, Justification::centred);
}

double GlueKnob::snapToStep (double v, bool fine) const
{
    if (v < snapFloor)
        return getMinimum();                       // e.g. HP below 20 Hz = Off

    const double step = fine ? fineStep : coarseStep;
    double s = std::round (v / step) * step;
    if (s < snapFloor)
        s = snapFloor;
    return jlimit (getMinimum(), getMaximum(), s);
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

    const bool fine = e.mods.isShiftDown();
    const double perPixel = isStepped() ? 1.0 / 160.0 : (fine ? 1.0 / 2500.0 : 1.0 / 250.0);
    dragProportion = jlimit (0.0, 1.0, dragProportion + pixels * perPixel);

    double v = proportionOfLengthToValue (dragProportion);
    if (hasValueSteps())
        v = snapToStep (v, fine);

    if (std::abs (v - getValue()) > 1.0e-9)
    {
        setValue (v, sendNotificationSync);
        repaint();
    }
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

    if (hasValueSteps())
    {
        // whole steps in value terms; "up" turns the knob clockwise (reversed knobs count down)
        const bool fine = e.mods.isShiftDown();
        const double step = fine ? fineStep : coarseStep;
        const double sign = (dir > 0.0f ? 1.0 : -1.0) * (reversed ? -1.0 : 1.0);
        const double current = getValue();

        if (current < snapFloor)                   // from "Off": first notch up lands on the floor
        {
            if (sign > 0.0)
                setValueWithGesture (snapFloor);
            return;
        }

        // step from the current value to the next grid point in that direction
        double target = sign > 0.0 ? std::floor (current / step + 1.0e-3) * step + step
                                   : std::ceil (current / step - 1.0e-3) * step - step;
        target = target < snapFloor ? getMinimum() : jlimit (getMinimum(), getMaximum(), target);
        setValueWithGesture (target);
        return;
    }

    const double step = isStepped() ? getInterval() / (getMaximum() - getMinimum())
                                    : (e.mods.isShiftDown() ? 0.002 : 0.02);
    const double p = jlimit (0.0, 1.0, valueToProportionOfLength (getValue()) + (dir > 0.0f ? step : -step));
    setValueWithGesture (proportionOfLengthToValue (p));
}

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
    const auto face = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, face, 4.0f);

    Graphics::ScopedSaveState state (g);
    {
        Path clip;
        clip.addRoundedRectangle (face.reduced (1.5f), 3.0f);
        g.reduceClipRegion (clip);
    }

    // faint blue backlight
    g.setGradientFill (ColourGradient (GlueColours::blueGlow.withAlpha (0.12f), face.getCentreX(), face.getBottom(),
                                       Colours::transparentBlack, face.getCentreX(), face.getY(), true));
    g.fillRect (face);

    const Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.55f);
    const float rs = pivot.y - face.getY() - face.getHeight() * 0.36f;     // scale radius
    const float halfSweep = kMeterHalfSweep;
    auto angleFor = [halfSweep] (float db) { return -halfSweep + 2.0f * halfSweep * jlimit (0.0f, 20.0f, db) / 20.0f; };

    // scale track + blue value arc (from 0 dB to the needle)
    Path track;
    track.addCentredArc (pivot.x, pivot.y, rs, rs, 0.0f, -halfSweep, halfSweep, true);
    g.setColour (Colour (0xff0a0c0e));
    g.strokePath (track, PathStrokeType (7.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (Colour (0xff1a2633));
    g.strokePath (track, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

    const float na = angleFor (needle);
    if (needle > 0.05f)
    {
        Path value;
        value.addCentredArc (pivot.x, pivot.y, rs, rs, 0.0f, -halfSweep, na, true);
        g.setColour (GlueColours::blueGlow.withAlpha (0.18f));
        g.strokePath (value, PathStrokeType (12.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (GlueColours::blueGlow.withAlpha (0.35f));
        g.strokePath (value, PathStrokeType (7.5f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (GlueColours::blue);
        g.strokePath (value, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Colour (0xffc9e6ff).withAlpha (0.8f));
        g.strokePath (value, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // ticks + numbers
    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    for (int db = 0; db <= 20; ++db)
    {
        const bool major = db % 4 == 0, mid = db % 2 == 0;
        const float a = angleFor ((float) db);
        g.setColour (Colour (0xff9aa1a8).withAlpha (major ? 0.95f : 0.6f));
        g.drawLine (Line<float> (polar (pivot, a, rs + 7.0f), polar (pivot, a, rs + (major ? 14.0f : (mid ? 11.0f : 9.5f)))),
                    major ? 1.6f : 0.9f);
        if (major)
        {
            g.setColour (Colour (0xffdfe3e6));
            g.drawText (String (db), Rectangle<float> (26.0f, 13.0f).withCentre (polar (pivot, a, rs + 24.0f)), Justification::centred);
        }
    }

    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.setColour (GlueColours::labelText);
    g.drawText ("dB", face.withTrimmedTop (face.getHeight() * 0.66f).withHeight (14.0f), Justification::centred);

    // peak-hold marker
    if (peak > 0.05f)
    {
        const float a = angleFor (peak);
        Path tri;
        tri.addTriangle (polar (pivot, a, rs + 6.0f), polar (pivot, a - 0.02f, rs + 13.0f), polar (pivot, a + 0.02f, rs + 13.0f));
        g.setColour (Colour (0xffc9e6ff));
        g.fillPath (tri);
    }

    // needle with blue glow
    const auto tip = polar (pivot, na, rs + 12.0f);
    g.setColour (GlueColours::blueGlow.withAlpha (0.35f));
    g.drawLine (Line<float> (pivot, tip), 5.0f);
    g.setColour (Colour (0xffeef6ff));
    g.drawLine (Line<float> (pivot, tip), 1.8f);
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
    g.fillAll (Colours::black.withAlpha (0.78f));

    auto box = getLocalBounds().toFloat().reduced (12.0f, 40.0f);
    CustomLookAndFeel::drawPanel (g, box, "Glue - Quick Help");

    const char* lines[] =
    {
        "THRESHOLD:  turn right to compress more (0 .. -40 dB)",
        "RANGE:  limits the gain reduction. Fully right = FULL",
        "MAKEUP:  gain on the compressed signal (0 .. +20 dB)",
        "ATTACK (ms) / RELEASE (s):  stepped. A = auto release",
        "RATIO:  2:1, 4:1, 10:1 (soft, program-dependent knee)",
        "COMP IN:  gain change + makeup on/off",
        "MIX:  dry / wet blend for parallel compression",
        "PEAK CLIP:  soft clipper, ceiling -0.5 dBFS",
        "HP:  sidechain high-pass (detector only), OFF .. 500 Hz",
        "EXT IN:  detector listens to the sidechain input",
        "ANALOG:  circuit noise in the detector",
        "LOW LAT:  IIR oversampling filters (less latency)",
        "Top right: oversampling 1x - 8x",
        "Click the meter to reset the peak marker",
        "Knobs: drag, Shift = fine, wheel, double-click = default",
        "Drag the bottom-right corner to resize",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (12.0f, 0.0f).withTrimmedTop (30.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.5f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click anywhere") ? GlueColours::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (27.0f).toNearestInt(), Justification::centredLeft, 2, 0.8f);
    }
}
