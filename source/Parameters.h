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
    inline constexpr auto running    = "running";
    inline constexpr auto window     = "window";

    /** Every parameter the filter has to be rebuilt for. Output gain is deliberately
        absent: it is a ramped trim on the way out, not part of the filter. Bypass
        likewise just gates the convolution.

        Phase is here even though it leaves the correction curve untouched — the curve
        is the same either way, only the response realising it differs — because
        re-deriving a curve costs far less than the risk of a second code path. */
    inline constexpr std::array filterShaping { amount, smoothing, link, lowFreq, highFreq, phase };

    /** The two that decide what a take is rather than how the curve is shaped: whether
        it is only the last few seconds, and how many. They go to the engine by a path
        of their own, because a change to them rebuilds nothing by itself -- the match
        follows on its own timer, if one is in force. */
    inline constexpr std::array runningControls { running, window };
}

namespace Parameters
{
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
