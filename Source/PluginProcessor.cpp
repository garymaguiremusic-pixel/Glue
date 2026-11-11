#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
GlueAudioProcessor::GlueAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GlueState", glue::createParameterLayout())
{
    using namespace glue::ParamIDs;
    pThreshold    = apvts.getRawParameterValue (threshold);
    pMakeup       = apvts.getRawParameterValue (makeup);
    pRange        = apvts.getRawParameterValue (range);
    pAttack       = apvts.getRawParameterValue (attack);
    pRelease      = apvts.getRawParameterValue (release);
    pRatio        = apvts.getRawParameterValue (ratio);
    pMix          = apvts.getRawParameterValue (mix);
    pScHpf        = apvts.getRawParameterValue (scHpf);
    pCompIn       = apvts.getRawParameterValue (compIn);
    pPeakClip     = apvts.getRawParameterValue (peakClip);
    pExtSc        = apvts.getRawParameterValue (extSc);
    pOversampling = apvts.getRawParameterValue (oversampling);
    pLowLatency   = apvts.getRawParameterValue (lowLatency);
    pDcBlock      = apvts.getRawParameterValue (dcBlock);
    pNoise        = apvts.getRawParameterValue (noise);
    pBypass       = apvts.getRawParameterValue (bypass);
    bypassParam   = dynamic_cast<AudioParameterBool*> (apvts.getParameter (bypass));
}

GlueAudioProcessor::~GlueAudioProcessor()
{
    cancelPendingUpdate();
}

//==============================================================================
bool GlueAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out || (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()))
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != AudioChannelSet::mono() && sc != AudioChannelSet::stereo())
            return false;
    }
    return true;
}

//==============================================================================
void GlueAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    maxBlock = jmax (1, samplesPerBlock);

    mainScratch.setSize (2, maxBlock);
    scScratch.setSize (2, maxBlock);

    for (int type = 0; type < 2; ++type)
        for (int f = 1; f < kNumFactors; ++f)
        {
            const auto filter = type == 1 ? Oversampler::filterHalfBandPolyphaseIIR
                                          : Oversampler::filterHalfBandFIREquiripple;
            osMain[type][f] = std::make_unique<Oversampler> (2, (size_t) f, filter, true, true);
            osSc[type][f]   = std::make_unique<Oversampler> (2, (size_t) f, filter, true, true);
            osMain[type][f]->initProcessing ((size_t) maxBlock);
            osSc[type][f]->initProcessing ((size_t) maxBlock);
        }

    activeFactor = -1;
    selectOversampling ((int) pOversampling->load(), pLowLatency->load() > 0.5f);
    setLatencySamples (gui.latency.load());
}

void GlueAudioProcessor::reset()
{
    for (auto* os : { activeMain, activeSc })
        if (os != nullptr)
            os->reset();
    comp.reset();
}

void GlueAudioProcessor::selectOversampling (int factorIndex, bool lowLatency)
{
    factorIndex = jlimit (0, kNumFactors - 1, factorIndex);
    activeFactor = factorIndex;
    activeLowLatency = lowLatency;

    const int type = lowLatency ? 1 : 0;
    activeMain = factorIndex > 0 ? osMain[type][factorIndex].get() : nullptr;
    activeSc   = factorIndex > 0 ? osSc[type][factorIndex].get()   : nullptr;

    for (auto* os : { activeMain, activeSc })
        if (os != nullptr)
            os->reset();

    // Rate-dependent coefficients only: no allocation, safe on the audio thread.
    comp.setSampleRate (baseSampleRate * (double) (1 << factorIndex));
    updateDspParameters();
    comp.reset();

    const int latency = activeMain != nullptr ? roundToInt (activeMain->getLatencyInSamples()) : 0;
    if (latency != gui.latency.exchange (latency))
    {
        if (MessageManager::existsAndIsCurrentThread())
            setLatencySamples (latency);
        else
            triggerAsyncUpdate();
    }
}

void GlueAudioProcessor::handleAsyncUpdate()
{
    setLatencySamples (gui.latency.load());
}

void GlueAudioProcessor::updateDspParameters() noexcept
{
    comp.setThreshold ((double) pThreshold->load());
    comp.setMakeup ((double) pMakeup->load());
    comp.setRange ((double) pRange->load() <= glue::kRangeMin + 0.01 ? -106.0 : (double) pRange->load());
    comp.setMix ((double) pMix->load());
    comp.setScHpf ((double) pScHpf->load());
    comp.setAttack (roundToInt (pAttack->load()));   // stepped: always exactly one of the listed settings
    comp.setRelease (roundToInt (pRelease->load()));   // stepped: always exactly one of the listed settings
    comp.setRatio (roundToInt (pRatio->load()));   // stepped: always exactly one of the listed settings
    comp.setCompIn (pCompIn->load() > 0.5f);
    comp.setPeakClip (pPeakClip->load() > 0.5f);
    comp.setDcBlock (pDcBlock->load() > 0.5f);
    comp.setAnalogNoise (pNoise->load() > 0.5f);
}

//==============================================================================
void GlueAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    auto main = getBusBuffer (buffer, false, 0);
    const int numMain = main.getNumChannels();
    if (numMain == 0 || numSamples == 0 || maxBlock == 0)
        return;

    AudioBuffer<float> aux;
    if (getBusCount (true) > 1)
        aux = getBusBuffer (buffer, true, 1);
    const int numAux = getBusCount (true) > 1 && getBus (true, 1)->isEnabled() ? aux.getNumChannels() : 0;

    // Oversampling choice is non-automatable; switching resets the oversamplers and the detector.
    const int factor = jlimit (0, kNumFactors - 1, (int) pOversampling->load());
    const bool lowLatency = pLowLatency->load() > 0.5f;
    if (factor != activeFactor || lowLatency != activeLowLatency)
        selectOversampling (factor, lowLatency);

    if (gui.resetPeak.exchange (false))
        comp.resetPeakHold();

    const bool bypassed = pBypass->load() > 0.5f;
    const bool extSc = pExtSc->load() > 0.5f;
    const bool useAux = extSc && numAux > 0;
    gui.auxConnected.store (numAux > 0);

    updateDspParameters();
    comp.setScInGain (extSc && ! useAux ? 0.0 : 1.0);   // Ext SC on but nothing connected: no compression

    double* mainPtrs[2] = { mainScratch.getWritePointer (0), mainScratch.getWritePointer (1) };
    double* scPtrs[2]   = { scScratch.getWritePointer (0),   scScratch.getWritePointer (1) };

    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = jmin (maxBlock, numSamples - pos);

        // float -> double; a mono main input feeds both lanes
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = main.getReadPointer (jmin (ch, numMain - 1), pos);
            for (int i = 0; i < n; ++i)
                mainPtrs[ch][i] = (double) src[i];
        }
        if (useAux)
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* src = aux.getReadPointer (jmin (ch, numAux - 1), pos);
                for (int i = 0; i < n; ++i)
                    scPtrs[ch][i] = (double) src[i];
            }

        dsp::AudioBlock<double> mainBlock (mainPtrs, 2, 0, (size_t) n);
        dsp::AudioBlock<double> scBlock (scPtrs, 2, 0, (size_t) n);

        if (activeMain != nullptr)
        {
            auto up = activeMain->processSamplesUp (mainBlock);
            // Without the aux, the sidechain is the main input: GBusComp reads sc[i] before writing x[i].
            auto upSc = useAux ? activeSc->processSamplesUp (scBlock) : up;
            if (! bypassed)
                comp.processBlock (up.getChannelPointer (0), up.getChannelPointer (1),
                                   upSc.getChannelPointer (0), upSc.getChannelPointer (1), (int) up.getNumSamples());
            activeMain->processSamplesDown (mainBlock);
        }
        else if (! bypassed)
        {
            comp.processBlock (mainPtrs[0], mainPtrs[1],
                               useAux ? scPtrs[0] : mainPtrs[0], useAux ? scPtrs[1] : mainPtrs[1], n);
        }

        for (int ch = 0; ch < numMain; ++ch)
        {
            float* dst = main.getWritePointer (ch, pos);
            for (int i = 0; i < n; ++i)
                dst[i] = (float) mainPtrs[ch][i];
        }
    }

    gui.grNeedleDb.store (bypassed ? 0.0f : (float) comp.getMeterNeedleDb());
    gui.grDb.store (bypassed ? 0.0f : (float) comp.getGainReductionDb());
    gui.grPeakDb.store ((float) comp.getPeakHoldDb());
}

//==============================================================================
void GlueAudioProcessor::setCurrentProgram (int index)
{
    if (! isPositiveAndBelow (index, glue::numFactoryPresets))
        return;

    currentProgram.store (index);
    const auto& p = glue::factoryPresets[index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    using namespace glue::ParamIDs;
    set (threshold, p.threshold);
    set (makeup,    p.makeup);
    set (range,     p.range);
    set (attack,    (float) p.attack);
    set (release,   (float) p.release);
    set (ratio,     (float) p.ratio);
    set (mix,       p.mix);
    set (scHpf,     p.scHpf < glue::kScHpfOff ? glue::kScHpfOff - 1.0f : p.scHpf);
    set (compIn,    1.0f);
}

const String GlueAudioProcessor::getProgramName (int index)
{
    return isPositiveAndBelow (index, glue::numFactoryPresets) ? String (glue::factoryPresets[index].name) : String();
}

//==============================================================================
void GlueAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("editorWidth", gui.editorWidth.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GlueAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = ValueTree::fromXml (*xml);
            currentProgram.store (jlimit (0, glue::numFactoryPresets - 1, (int) state.getProperty ("program", 0)));
            gui.editorWidth.store ((int) state.getProperty ("editorWidth", 0));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
AudioProcessorEditor* GlueAudioProcessor::createEditor()
{
    return new GlueAudioProcessorEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GlueAudioProcessor();
}
