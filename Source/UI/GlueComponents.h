#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Rotary knob (chrome with a blue value arc, or a small black knob) with scale
    numbers printed around it and its name underneath.
      - vertical drag (full range in 250 px), Shift = fine
      - optional value steps: drag and wheel move in whole steps (e.g. 1 dB, 10 Hz),
        Shift = fine steps (0.1 dB, 1 Hz)
      - scroll wheel, double-click = default
      - the current value is always shown under the name
      - stepped parameters snap to their steps
      - "reversed" knobs run from the parameter's maximum (left) to its minimum (right)
*/
class GlueKnob : public juce::Slider
{
public:
    struct ScaleLabel { double value; juce::String text; };

    GlueKnob (const juce::String& name, bool largeChromeKnob, float sweepRadians = 2.35f, float sizeScale = 1.0f);

    bool isLarge() const noexcept { return large; }

    void setScale (std::vector<ScaleLabel> labels, int numTicks) { scale = std::move (labels); ticks = numTicks; repaint(); }
    void setReversed (bool r) noexcept           { reversed = r; }
    void setDefaultValue (double v) noexcept     { defaultValue = v; }

    /** Drag/wheel steps (0 = continuous). Values below offBelow jump to the minimum (e.g. HP "Off"). */
    void setValueSteps (double coarse, double fine, double offBelow = -1.0e30)
    {
        coarseStep = coarse; fineStep = fine; snapFloor = offBelow;
    }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> valueText;   // shown under the name, e.g. "-12.0 dB"

    double proportionOfLengthToValue (double proportion) override;
    double valueToProportionOfLength (double value) override;

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void setValueWithGesture (double v);
    bool isStepped() const { return getInterval() >= 1.0; }
    bool hasValueSteps() const { return coarseStep > 0.0; }
    double snapToStep (double v, bool fine) const;

    juce::String title;
    bool large;
    float sweep, sizeScale;
    std::vector<ScaleLabel> scale;
    int ticks = 0;
    bool reversed = false;
    double defaultValue = 0.0;
    float lastDragY = 0.0f;
    double dragProportion = 0.0;
    double coarseStep = 0.0, fineStep = 0.0, snapFloor = -1.0e30;
    bool dragging = false;
};

//==============================================================================
/** Connects a GlueKnob to a parameter (range, skew, steps, default) with host gestures. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, GlueKnob&);
    ~KnobAttachment();

private:
    GlueKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "GR   3.2 dB". */
class LcdDisplay : public juce::Component
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setDimmed (bool shouldDim);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    std::function<void()> onClick;

private:
    juce::String nameText, valueText;
    bool dimmed = false;
};

//==============================================================================
/** Gain-reduction needle meter (0..20 dB, left to right). Click to reset the peak marker. */
class GainReductionMeter : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    void setValues (float needleDb, float peakDb);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }

    std::function<void()> onClick;

private:
    float needle = 0.0f, peak = 0.0f;
};

//==============================================================================
/** Round "?" button. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
