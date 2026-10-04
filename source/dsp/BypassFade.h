#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

/**
    Bypass as a crossfade rather than a switch.

    Switching it used to pop, for three reasons that all had to go:

      - The convolution stopped being fed while bypassed, so on the way back in it
        resumed from input a third of a second old. The jump between that and the
        present went through a curve with up to 24 dB of boost in it, and came out as
        a burst ten times the level of the signal.
      - Wet and dry were swapped in one sample. A minimum-phase correction shifts the
        phase of everything it touches, so the two are never in step and the swap is a
        step in the waveform.
      - In linear phase the correction delays the signal by half its length, and the
        plugin reports that to the host -- but the bypassed signal was not delayed. Every
        toggle jumped 85 ms, and while bypassed the host's latency compensation put the
        dry signal that far early.

    So the processor runs the correction whatever the bypass says, this keeps a copy of
    the dry input delayed by the same latency, and the two are blended over fadeSeconds.
    Equal-gain, because the two are the same material and add coherently.

    Everything here is the audio thread's, except prepare() and reset().
*/
class BypassFade
{
public:
    /** Long enough that the blend is not heard as a click, short enough that pressing
        bypass still feels like pressing a switch. */
    static constexpr double fadeSeconds = 0.03;

    /** maxDelaySamples bounds the latency pushDry() may be asked to match. Blocks larger
        than maxBlockSize are not accepted -- the processor hands them over in pieces. */
    void prepare (int numChannels, int maxBlockSize, int maxDelaySamples, double sampleRate);

    /** Clears the delay and lands the blend on `bypassed` at once, with no fade: for a
        fresh start, where there is nothing to fade from. */
    void reset (bool bypassed);

    int getMaxBlockSize() const noexcept { return dry.getNumSamples(); }

    /** Takes a copy of the input before it is processed, delayed by `latencySamples` so
        it lines up with what the correction will give back. */
    void pushDry (const juce::AudioBuffer<float>& input, int latencySamples) noexcept;

    /** Blends `processed` -- the same samples pushDry() was last given, after the
        correction and the output trim -- with that dry copy, moving towards all dry
        when `bypassed` and all processed when not. */
    void mix (juce::AudioBuffer<float>& processed, bool bypassed) noexcept;

private:
    juce::AudioBuffer<float> dry;
    std::vector<std::vector<float>> delayLines;
    int mask = 0;
    int writeIndex = 0;
    int dryChannels = 0;

    // 1 is all processed, 0 all dry.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> wet;
};
