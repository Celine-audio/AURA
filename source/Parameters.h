#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

/**
    The plugin's parameters: their IDs in one place, and the layout that declares
    them. Everything that reads a parameter goes through ParamID rather than a string
    literal, so a typo is a compile error instead of a null pointer at runtime.
*/
namespace ParamID
{
    inline constexpr auto bypass     = "bypass";
    inline constexpr auto amount     = "amount";
    inline constexpr auto smoothing  = "smoothing";
    inline constexpr auto link       = "link";
    inline constexpr auto outputGain = "outputGain";
    inline constexpr auto lowFreq    = "lowFreq";
    inline constexpr auto highFreq   = "highFreq";
    inline constexpr auto phase      = "phase";

    /** Every parameter the filter has to be rebuilt for. Output gain is deliberately
        absent: it is a ramped trim on the way out, not part of the filter. Bypass
        likewise just gates the convolution.

        Phase is here even though it leaves the correction curve untouched — the curve
        is the same either way, only the response realising it differs — because
        re-deriving a curve costs far less than the risk of a second code path. */
    inline constexpr std::array filterShaping { amount, smoothing, link, lowFreq, highFreq, phase };
}

namespace Parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
