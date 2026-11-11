#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "GBusComp.h"

//==============================================================================
class GlueAudioProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    GlueAudioProcessor();
    ~GlueAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override          { return JucePlugin_Name; }
    bool acceptsMidi() const override                    { return false; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                        { return glue::numFactoryPresets; }
    int getCurrentProgram() override                     { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    /** Read-only meters/state for the editor (written on the audio thread). */
    struct GuiState
    {
        std::atomic<float> grNeedleDb   { 0.0f };   // smoothed needle, 0..20
        std::atomic<float> grDb         { 0.0f };   // instantaneous
        std::atomic<float> grPeakDb     { 0.0f };   // peak hold
        std::atomic<bool>  resetPeak    { false };  // set by the editor
        std::atomic<bool>  auxConnected { false };
        std::atomic<int>   latency      { 0 };
        std::atomic<int>   editorWidth  { 0 };      // remembered editor size (0 = default)
    };
    GuiState gui;

private:
    void handleAsyncUpdate() override;
    void selectOversampling (int factorIndex, bool lowLatency);
    void updateDspParameters() noexcept;

    static constexpr int kNumFactors = 4;   // 1x, 2x, 4x, 8x
    using Oversampler = juce::dsp::Oversampling<double>;

    GBusComp comp;

    // [filterType][factorIndex]; index 0 (1x) is unused. Main is up+down, sidechain is up only.
    std::unique_ptr<Oversampler> osMain[2][kNumFactors], osSc[2][kNumFactors];
    Oversampler* activeMain = nullptr;
    Oversampler* activeSc = nullptr;
    int activeFactor = -1;
    bool activeLowLatency = false;

    juce::AudioBuffer<double> mainScratch, scScratch;
    int maxBlock = 0;
    double baseSampleRate = 44100.0;

    std::atomic<int> currentProgram { 0 };
    juce::AudioParameterBool* bypassParam = nullptr;

    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pMakeup = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pRatio = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pScHpf = nullptr;
    std::atomic<float>* pCompIn = nullptr;
    std::atomic<float>* pPeakClip = nullptr;
    std::atomic<float>* pExtSc = nullptr;
    std::atomic<float>* pOversampling = nullptr;
    std::atomic<float>* pLowLatency = nullptr;
    std::atomic<float>* pDcBlock = nullptr;
    std::atomic<float>* pNoise = nullptr;
    std::atomic<float>* pBypass = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlueAudioProcessor)
};
