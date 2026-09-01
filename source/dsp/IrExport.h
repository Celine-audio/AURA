#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <vector>

/**
    Turns the plugin's final correction curves into an impulse response file.

    The curves handed in are the ones the plugin is actually applying — smoothing,
    band limits and L/R link already baked in — so an exported IR loaded into a
    convolver reproduces what you hear.
*/
namespace IrExport
{
    struct Options
    {
        enum class Layout
        {
            mono,   // one channel, derived from the channel named below
            stereo  // two channels: the left correction, then the right
        };

        /** Which correction a mono export is derived from. Ignored for a stereo
            export, which always carries both channels as they are. */
        enum class Channel
        {
            left,
            right,
            mid // the average of both, i.e. "L+R"
        };

        /** Which response realises the curve. Minimum phase is the useful default
            for a file: it costs the convolver loading it no latency and leaves no
            pre-ringing ahead of a transient. Linear phase is there so an export can
            reproduce exactly what the plugin is doing in its own linear mode. */
        enum class Phase
        {
            linear,
            minimum
        };

        Layout layout = Layout::stereo;
        Channel channel = Channel::mid;
        Phase phase = Phase::minimum;

        /** Taps written to the file. Zero derives it from the curve's own grid; the
            plugin passes the length of the filter it is actually running, so the file
            reproduces what you hear rather than a re-rendering of it at some other
            resolution. */
        int length = 0;
    };

    /** Builds the impulse response from final per-channel dB curves.

        A stereo export always writes the left correction on channel 1 and the right
        on channel 2 — exactly the plugin's current state. A mono export renders the
        single curve named by options.channel.

        Returns an empty buffer if the curves are missing, mismatched, or not a
        length a power-of-two FFT can produce. */
    juce::AudioBuffer<float> buildBuffer (const std::vector<float>& leftDb,
                                          const std::vector<float>& rightDb,
                                          const Options& options);

    /** Writes the buffer as a 32-bit float WAV. Returns a failure Result with a
        human-readable message if the file could not be written. */
    juce::Result writeWav (const juce::File& file,
                           const juce::AudioBuffer<float>& buffer,
                           double sampleRate);
}
