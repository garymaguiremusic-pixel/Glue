#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace glue
{
    namespace ParamIDs
    {
        inline constexpr const char* threshold    = "threshold";
        inline constexpr const char* makeup       = "makeup";
        inline constexpr const char* range        = "range";
        inline constexpr const char* attack       = "attack";
        inline constexpr const char* release      = "release";
        inline constexpr const char* ratio        = "ratio";
        inline constexpr const char* mix          = "mix";
        inline constexpr const char* scHpf        = "scHpf";
        inline constexpr const char* compIn       = "compIn";
        inline constexpr const char* peakClip     = "peakClip";
        inline constexpr const char* extSc        = "extSc";
        inline constexpr const char* oversampling = "oversampling";
        inline constexpr const char* lowLatency   = "lowLatency";
        inline constexpr const char* dcBlock      = "dcBlock";
        inline constexpr const char* noise        = "noise";
        inline constexpr const char* bypass       = "bypass";
    }

    // Range: the bottom of the knob (-80 dB) means "Full" (the DSP uses -106 dB there).
    inline constexpr float kRangeMin = -80.0f;
    inline constexpr float kScHpfOff = 20.0f;      // below this the SC HPF is "Off" (2 Hz internally)

    inline const juce::StringArray& attackNames()
    {
        static const juce::StringArray names { "0.01 ms", "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" };
        return names;
    }
    inline const juce::StringArray& releaseNames()
    {
        static const juce::StringArray names { "0.1 s", "0.2 s", "0.4 s", "0.6 s", "0.8 s", "1.2 s", "Auto" };
        return names;
    }
    inline const juce::StringArray& ratioNames()
    {
        static const juce::StringArray names { "2:1", "4:1", "10:1" };
        return names;
    }
    inline const juce::StringArray& oversamplingNames()
    {
        static const juce::StringArray names { "1x", "2x", "4x", "8x" };
        return names;
    }

    //==============================================================================
    // Value formatting (shared by the host text and the editor)
    inline juce::String formatDb (float v, int decimals = 1)
    {
        if (std::abs (v) < 0.05f) v = 0.0f;
        return (v > 0.0f ? "+" : "") + juce::String (v, decimals) + " dB";
    }
    inline juce::String formatRange (float v)  { return v <= kRangeMin + 0.05f ? juce::String ("Full") : formatDb (v); }
    inline juce::String formatMix (float v)    { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    inline juce::String formatScHpf (float hz)
    {
        if (hz < kScHpfOff) return "Off";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    //==============================================================================
    inline juce::NormalisableRange<float> skewedRange (float start, float end, float centre)
    {
        juce::NormalisableRange<float> r (start, end);
        r.setSkewForCentre (centre);
        return r;
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        using FloatAttr = AudioParameterFloatAttributes;
        std::vector<std::unique_ptr<RangedAudioParameter>> params;

        auto floatParam = [&params] (const char* id, const char* name, NormalisableRange<float> range, float def,
                                     std::function<String (float, int)> toText, const char* label)
        {
            params.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                                     FloatAttr().withStringFromValueFunction (std::move (toText))
                                                                                .withLabel (label)));
        };
        auto choiceParam = [&params] (const char* id, const char* name, const StringArray& items, int def,
                                      bool automatable = true)
        {
            params.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def,
                                                                      AudioParameterChoiceAttributes().withAutomatable (automatable)));
        };
        auto boolParam = [&params] (const char* id, const char* name, bool def, bool automatable = true)
        {
            params.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def,
                                                                    AudioParameterBoolAttributes().withAutomatable (automatable)));
        };

        // ASSUMPTION: host-facing bounds (Threshold -40..0, Makeup 0..+20, SC HPF up to 500 Hz) were not decoded.
        floatParam (ParamIDs::threshold, "Threshold", NormalisableRange<float> (-40.0f, 0.0f, 0.01f), 0.0f,
                    [] (float v, int) { return formatDb (v); }, "dB");
        floatParam (ParamIDs::makeup, "Makeup", NormalisableRange<float> (0.0f, 20.0f, 0.01f), 0.0f,
                    [] (float v, int) { return formatDb (v); }, "dB");
        floatParam (ParamIDs::range, "Range", NormalisableRange<float> (kRangeMin, 0.0f, 0.01f), kRangeMin,
                    [] (float v, int) { return formatRange (v); }, "dB");
        choiceParam (ParamIDs::attack,  "Attack",  attackNames(),  2);
        choiceParam (ParamIDs::release, "Release", releaseNames(), 3);
        choiceParam (ParamIDs::ratio,   "Ratio",   ratioNames(),   1);
        floatParam (ParamIDs::mix, "Mix", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f,
                    [] (float v, int) { return formatMix (v); }, "");
        floatParam (ParamIDs::scHpf, "SC HPF", skewedRange (kScHpfOff - 1.0f, 500.0f, 90.0f), kScHpfOff - 1.0f,
                    [] (float v, int) { return formatScHpf (v); }, "Hz");
        boolParam (ParamIDs::compIn,   "Comp In",   true);
        boolParam (ParamIDs::peakClip, "Peak Clip", false);
        boolParam (ParamIDs::extSc,    "Ext SC",    false);
        choiceParam (ParamIDs::oversampling, "Oversampling", oversamplingNames(), 1, false);  // ASSUMPTION: 2x default
        boolParam (ParamIDs::lowLatency, "Low Latency", false, false);
        boolParam (ParamIDs::dcBlock,  "DC Block",  false);
        boolParam (ParamIDs::noise,    "Analog Noise", true, false);
        boolParam (ParamIDs::bypass,   "Bypass",    false);

        return { params.begin(), params.end() };
    }

    //==============================================================================
    /** Starting points (own settings). Fields: thr, makeup, range, attack, release, ratio, mix, scHpf. */
    struct Preset
    {
        const char* name;
        float threshold, makeup, range;
        int attack, release, ratio;
        float mix, scHpf;
    };

    inline constexpr Preset factoryPresets[] =
    {
        { "Init",               0.0f,  0.0f, kRangeMin, 2, 3, 1, 1.00f, 0.0f },
        { "Mix Bus Gentle",   -12.0f,  2.0f, kRangeMin, 4, 6, 0, 1.00f, 60.0f },
        { "Mix Bus Glue",     -16.0f,  3.0f, -8.0f,     4, 6, 1, 1.00f, 90.0f },
        { "Drum Bus Punch",   -20.0f,  5.0f, kRangeMin, 4, 1, 1, 1.00f, 0.0f },
        { "Drum Bus Smash",   -32.0f, 10.0f, kRangeMin, 2, 0, 2, 0.45f, 0.0f },
        { "Parallel Crush",   -35.0f, 12.0f, kRangeMin, 0, 0, 2, 0.30f, 0.0f },
        { "Room Pump",        -28.0f,  8.0f, -15.0f,    5, 2, 2, 0.70f, 150.0f },
        { "Vocal Bus",        -18.0f,  4.0f, -10.0f,    3, 6, 1, 1.00f, 120.0f },
        { "Bass Control",     -14.0f,  3.0f, -6.0f,     5, 4, 1, 1.00f, 0.0f },
        { "Master Touch",      -8.0f,  1.0f, -4.0f,     5, 6, 0, 1.00f, 40.0f },
    };
    inline constexpr int numFactoryPresets = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));
}
