#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "MatchEngine.h"

#include <atomic>

/**
    Learns a take from an audio file instead of from what is playing: the whole file is
    run through the same analyzers a Learn uses, so the spectrum that comes out is on
    the same absolute scale and averaged the same way, and a take from a file and a take
    from the transport can be matched against one another.

    Pure, and slow enough to matter -- a song is several million samples to decode and
    a few thousand FFTs -- so it is called off the message thread. See
    PluginProcessor::importTakeFromFile.
*/
namespace AudioFileSpectrum
{
    struct Result
    {
        MatchEngine::Spectra magnitudes;

        /** The rate the file plays at, which is the grid the spectra are on. */
        double sampleRate = 0.0;

        /** Empty on success; otherwise a sentence fit to show someone. */
        juce::String error;

        bool succeeded() const noexcept { return error.isEmpty(); }
    };

    /** Reads `file` from start to end and averages its spectrum, per channel. A mono
        file fills both channels, and anything past the second channel is ignored --
        the match is stereo, not surround.

        Stops early and fails when `shouldStop` turns true, which is how a plugin being
        closed mid-read gets its thread back. */
    Result analyse (const juce::File& file, const std::atomic<bool>* shouldStop = nullptr);

    /** The file patterns analyse() can read, for a file chooser: "*.wav;*.aiff;...". */
    juce::String supportedWildcard();
}
