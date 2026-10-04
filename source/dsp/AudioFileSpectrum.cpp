#include "AudioFileSpectrum.h"

namespace
{
    /** How much is decoded at a time. Big enough that the reader's per-call overhead
        disappears, small enough that a cancel is answered at once. */
    constexpr int readBlockSize = 1 << 16;

    /** Below this the file holds nothing to match to: a spectrum this quiet is the
        noise floor, and a correction aimed at it would be the 24 dB ceiling everywhere. */
    constexpr float silenceThreshold = 1.0e-6f;

    void registerFormats (juce::AudioFormatManager& formats)
    {
        // WAV, AIFF, FLAC and Ogg everywhere, plus whatever the platform decodes --
        // which on macOS is MP3, AAC and Apple Lossless through Core Audio.
        formats.registerBasicFormats();
    }
}

namespace AudioFileSpectrum
{
    Result analyse (const juce::File& file, const std::atomic<bool>* shouldStop)
    {
        Result result;

        const auto fail = [&result] (const juce::String& why)
        {
            result.error = why;
            return result;
        };

        juce::AudioFormatManager formats;
        registerFormats (formats);

        const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
            return fail ("\"" + file.getFileName() + "\" could not be read as audio.");

        if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
            return fail ("\"" + file.getFileName() + "\" holds no audio.");

        std::array<SpectrumAnalyzer, 2> analyzers;

        for (auto& analyzer : analyzers)
            analyzer.setCapturing (true);

        // Reading two channels or one, never more: a mono file is copied into both.
        const auto channels = (int) juce::jmin (2u, reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, readBlockSize);

        for (juce::int64 position = 0; position < reader->lengthInSamples; position += readBlockSize)
        {
            if (shouldStop != nullptr && shouldStop->load())
                return fail ("Cancelled.");

            const auto count = (int) juce::jmin ((juce::int64) readBlockSize, reader->lengthInSamples - position);

            if (! reader->read (&buffer, 0, count, position, true, channels > 1))
                return fail ("\"" + file.getFileName() + "\" could not be decoded past "
                             + juce::String ((double) position / reader->sampleRate, 1) + " s.");

            for (int ch = 0; ch < (int) analyzers.size(); ++ch)
                analyzers[(size_t) ch].pushBlock (buffer.getReadPointer (juce::jmin (ch, channels - 1)), count);
        }

        for (size_t ch = 0; ch < analyzers.size(); ++ch)
            if (! analyzers[ch].getAveragedMagnitudes (result.magnitudes[ch]))
                return fail ("\"" + file.getFileName() + "\" is too short to learn from.");

        auto loudest = 0.0f;

        for (const auto& channel : result.magnitudes)
            for (const auto magnitude : channel)
                loudest = juce::jmax (loudest, magnitude);

        if (loudest < silenceThreshold)
            return fail ("\"" + file.getFileName() + "\" is silent.");

        result.sampleRate = reader->sampleRate;
        return result;
    }

    juce::String supportedWildcard()
    {
        juce::AudioFormatManager formats;
        registerFormats (formats);
        return formats.getWildcardForAllFormats();
    }
}
