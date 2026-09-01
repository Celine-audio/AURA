#include "IrExport.h"

#include "FilterDesigner.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace IrExport
{
    juce::AudioBuffer<float> buildBuffer (const std::vector<float>& leftDb,
                                          const std::vector<float>& rightDb,
                                          const Options& options)
    {
        if (leftDb.size() < 2 || leftDb.size() != rightDb.size())
            return {};

        // The curves hold fftSize/2 + 1 bins; the response built from them may be a
        // different length, since both builders resample onto whatever grid they are
        // given.
        const auto binSpan = (int) (leftDb.size() - 1) * 2;
        const auto fftSize = options.length > 0 ? options.length : binSpan;

        // Both builders need a power-of-two length.
        if ((binSpan & (binSpan - 1)) != 0 || (fftSize & (fftSize - 1)) != 0)
            return {};

        auto renderIr = [fftSize, &options] (const std::vector<float>& db)
        {
            const auto magnitudes = FilterDesigner::dbToMagnitudes (db);

            return options.phase == Options::Phase::linear
                       ? FilterDesigner::buildLinearPhaseIR (magnitudes, fftSize)
                       : FilterDesigner::buildMinimumPhaseIR (magnitudes, fftSize);
        };

        if (options.layout == Options::Layout::stereo)
        {
            // Stereo is always the plugin as it stands: left correction on channel 1,
            // right on channel 2. There is nothing to choose here.
            const auto left = renderIr (leftDb);
            const auto right = renderIr (rightDb);

            juce::AudioBuffer<float> buffer (2, fftSize);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (0), left.data(), fftSize);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1), right.data(), fftSize);
            return buffer;
        }

        const auto midDb = FilterDesigner::averageDb (leftDb, rightDb);
        const auto& chosen = options.channel == Options::Channel::left  ? leftDb
                           : options.channel == Options::Channel::right ? rightDb
                                                                        : midDb;

        const auto ir = renderIr (chosen);

        juce::AudioBuffer<float> buffer (1, fftSize);
        juce::FloatVectorOperations::copy (buffer.getWritePointer (0), ir.data(), fftSize);
        return buffer;
    }

    juce::Result writeWav (const juce::File& file,
                           const juce::AudioBuffer<float>& buffer,
                           double sampleRate)
    {
        if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
            return juce::Result::fail ("There is no correction curve to export yet.");

        if (sampleRate <= 0.0)
            return juce::Result::fail ("The sample rate is unknown: start playback once, then export.");

        if (file.existsAsFile() && ! file.deleteFile())
            return juce::Result::fail ("Could not overwrite " + file.getFullPathName());

        auto stream = std::make_unique<juce::FileOutputStream> (file);
        if (! stream->openedOk())
            return juce::Result::fail ("Could not create " + file.getFullPathName());

        const auto options = juce::AudioFormatWriterOptions {}
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (buffer.getNumChannels())
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (std::move (stream));
        auto writer = wav.createWriterFor (out, options);

        if (writer == nullptr)
        {
            file.deleteFile();
            return juce::Result::fail ("Could not write a WAV file to " + file.getFullPathName());
        }

        // createWriterFor() takes ownership of the stream on success.
        if (! writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()))
        {
            writer.reset();
            file.deleteFile();
            return juce::Result::fail ("Writing the impulse response failed. Is the disk full?");
        }

        return juce::Result::ok();
    }
}
