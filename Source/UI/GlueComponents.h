#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Rotary knob in the Virus Delay style.
      - vertical drag (full range in 250 px), Shift = fine
      - scroll wheel, double-click = default
      - stepped parameters snap to their steps
      - small knobs draw their label and their value underneath
    Drawing is delegated to CustomLookAndFeel::drawRotarySlider.
*/
class GlueKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    GlueKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (double)> valueText;   // e.g. "-12.0 dB"

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String currentValueText() const;

private:
    void setValueWithGesture (double v);
    bool isStepped() const { return getInterval() >= 1.0; }

    Size knobSize;
    juce::String label;
    double defaultValue = 0.0;
    float lastDragY = 0.0f;
    double dragProportion = 0.0;
    bool dragging = false, hovering = false;
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
/** Row of radio buttons bound to a choice parameter (e.g. Ratio 2 / 4 / 10). */
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (juce::RangedAudioParameter&, const juce::StringArray& labels, const juce::String& tooltip);
    void resized() override;

private:
    void select (int index);

    juce::RangedAudioParameter& param;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "THRESHOLD   -12.0 dB". */
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
/** Needle gain-reduction meter, 0..20 dB (needle rests on the right and swings left). */
class GainReductionMeter : public juce::Component
{
public:
    void setValues (float needleDb, float peakDb);
    void paint (juce::Graphics&) override;

    static float dbToProportion (float db);

private:
    float needle = 0.0f, peak = 0.0f;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
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
