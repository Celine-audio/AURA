#include "Parameters.h"

namespace Parameters
{
    namespace
    {
        juce::AudioParameterFloatAttributes percent (const char* label)
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel (label)
                .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() * 0.01f; });
        }

        // A bipolar percentage always shows its sign, so which side of zero you are on
        // is readable without looking at the fader.
        juce::AudioParameterFloatAttributes signedPercent()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("%")
                .withStringFromValueFunction ([] (float v, int)
                {
                    const auto value = juce::roundToInt (v * 100.0f);
                    return (value > 0 ? "+" : "") + juce::String (value);
                })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() * 0.01f; });
        }

        juce::NormalisableRange<float> frequencyRange (float low, float high, float centre)
        {
            juce::NormalisableRange<float> range { low, high };
            range.setSkewForCentre (centre);
            return range;
        }

        juce::AudioParameterFloatAttributes frequency()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int)
                {
                    if (v < 1000.0f)
                        return juce::String (juce::roundToInt (v));

                    return juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " k";
                })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    const auto value = t.getFloatValue();
                    return t.containsIgnoreCase ("k") ? value * 1000.0f : value;
                });
        }

        auto boolParam (const char* id, const char* name, bool defaultValue)
        {
            return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, defaultValue);
        }

        auto floatParam (const char* id, const char* name,
                         juce::NormalisableRange<float> range, float defaultValue,
                         juce::AudioParameterFloatAttributes attributes)
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue, std::move (attributes));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (boolParam (ParamID::bypass, "Bypass", false));

        // Bipolar, like Logic's Apply slider: negative amounts apply the inverse of the
        // match, pushing the source away from the reference instead of towards it.
        layout.add (floatParam (ParamID::amount, "Amount",
                                juce::NormalisableRange<float> { -1.0f, 1.0f }, 1.0f,
                                signedPercent()));

        layout.add (floatParam (ParamID::smoothing, "Smoothing",
                                juce::NormalisableRange<float> { 0.0f, 3.0f, 0.0f, 0.5f }, 1.0f / 3.0f,
                                juce::AudioParameterFloatAttributes()
                                    .withLabel ("oct")
                                    .withStringFromValueFunction ([] (float v, int)
                                    {
                                        return v <= 0.0f ? juce::String ("Off") : juce::String (v, 2);
                                    })));

        // 1 = both channels share the averaged correction (the classic mono-style match),
        // 0 = each channel is corrected from its own capture.
        layout.add (floatParam (ParamID::link, "L/R Link",
                                juce::NormalisableRange<float> { 0.0f, 1.0f }, 1.0f,
                                percent ("%")));

        layout.add (floatParam (ParamID::outputGain, "Output",
                                juce::NormalisableRange<float> { -12.0f, 12.0f, 0.1f }, 0.0f,
                                juce::AudioParameterFloatAttributes()
                                    .withLabel ("dB")
                                    .withStringFromValueFunction ([] (float v, int)
                                    {
                                        return (v > 0.0f ? "+" : "") + juce::String (v, 1);
                                    })));

        layout.add (floatParam (ParamID::lowFreq, "Low Freq",
                                frequencyRange (20.0f, 2000.0f, 200.0f), 20.0f, frequency()));

        layout.add (floatParam (ParamID::highFreq, "High Freq",
                                frequencyRange (500.0f, 20000.0f, 2000.0f), 20000.0f, frequency()));

        // Linear phase leaves the phase response flat and costs half the response in
        // latency; minimum phase costs none at all and bends phase instead. Minimum is
        // the default: it is the one that behaves like an EQ rather than like a
        // process, with nothing to compensate and no pre-ringing to explain.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamID::phase, 1 }, "Phase",
            juce::StringArray { "Linear", "Minimum" }, 1));

        return layout;
    }
}
