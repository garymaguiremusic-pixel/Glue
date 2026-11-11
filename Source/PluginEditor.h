#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "CustomLookAndFeel.h"
#include "UI/GlueComponents.h"

//==============================================================================
/** The whole user interface, laid out at its base size (600 x 400). The editor
    below scales it to the window size. */
class GluePanel : public juce::Component,
                  private juce::Timer
{
public:
    static constexpr int kBaseWidth = 600, kBaseHeight = 400;

    explicit GluePanel (GlueAudioProcessor&);
    ~GluePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    float rawParam (const char* id) const;

    GlueAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    // Knobs
    GlueKnob thresholdKnob { GlueKnob::Size::Large, "Threshold" };
    GlueKnob makeupKnob    { GlueKnob::Size::Large, "Makeup" };
    GlueKnob attackKnob    { GlueKnob::Size::Small, "Attack" };
    GlueKnob releaseKnob   { GlueKnob::Size::Small, "Release" };
    GlueKnob rangeKnob     { GlueKnob::Size::Small, "Range" };
    GlueKnob mixKnob       { GlueKnob::Size::Small, "Mix" };
    GlueKnob scHpfKnob     { GlueKnob::Size::Small, "SC HPF" };

    std::unique_ptr<ChoiceButtons> ratioButtons;

    juce::TextButton bypassButton { "Bypass" }, compInButton { "Comp In" }, peakClipButton { "Peak Clip" },
                     extScButton { "Ext SC" }, noiseButton { "Analog" }, lowLatencyButton { "Low Latency" };
    juce::ComboBox presetBox, oversamplingBox;

    LcdDisplay lcdThreshold, lcdMakeup, lcdGr, lcdPeak;
    GainReductionMeter meter;
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
    juce::Rectangle<float> compPanel, dynPanel, grPanel, rangePanel, scPanel, outPanel, scStatusArea, latencyArea;
    juce::String scStatus, latencyText;
    bool scStatusWarn = false;
    int lastShownProgram = -1;

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
