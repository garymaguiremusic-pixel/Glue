#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/GlueComponents.h"

//==============================================================================
/** The whole user interface, laid out at its base size (360 x 758). The editor
    below scales it to the window size. */
class GluePanel : public juce::Component,
                  private juce::Timer
{
public:
    static constexpr int kBaseWidth = 360, kBaseHeight = 758;

    explicit GluePanel (GlueAudioProcessor&);
    ~GluePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    static void drawCaption (juce::Graphics&, const juce::String&, juce::Rectangle<float> area, float size = 14.5f);
    float rawParam (const char* id) const;

    GlueAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    // Knobs (name, large chrome?, sweep either side of vertical in radians, size)
    GlueKnob thresholdKnob { "THRESHOLD", true, 2.35f, 1.15f };
    GlueKnob rangeKnob     { "RANGE", false, 2.35f, 0.75f };
    GlueKnob makeupKnob    { "MAKEUP", true, 2.35f, 1.15f };
    GlueKnob attackKnob    { "ATTACK - ms", true, 2.0f, 1.15f };
    GlueKnob ratioKnob     { "RATIO", false, 0.7f, 0.7f };
    GlueKnob releaseKnob   { "RELEASE - s", true, 2.0f, 1.15f };
    GlueKnob mixKnob       { "MIX", false, 2.35f, 0.75f };
    GlueKnob hpKnob        { "HP", false, 2.35f, 0.8f };

    juce::TextButton compInButton { "Comp In" }, peakClipButton { "Peak Clip" }, extScButton { "Ext In" },
                     noiseButton { "Analog" }, lowLatButton { "Low Latency" }, bypassButton { "Bypass" };
    juce::ComboBox presetBox, oversamplingBox;

    GainReductionMeter meter;
    LcdDisplay lcdGr, lcdPeak;
    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 500 };

    // Attachments (declared after the controls so they are destroyed first)
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<ComboAttachment> oversamplingAttachment;

    // Layout / state
    juce::Rectangle<float> meterPanel, scStatusArea;
    juce::String scStatus;
    int lastShownProgram = -1, lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GluePanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 70 % .. 250 %); remembers its size. */
class GlueAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit GlueAudioProcessorEditor (GlueAudioProcessor&);
    void resized() override;

private:
    GlueAudioProcessor& processor;
    GluePanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueAudioProcessorEditor)
};
