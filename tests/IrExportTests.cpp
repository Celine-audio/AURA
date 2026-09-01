#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <dsp/IrExport.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <vector>

namespace
{
    constexpr int fftSize = 4096;
    constexpr size_t numBins = fftSize / 2 + 1;

    // An IR's DC gain is the sum of its taps, which is how we tell which curve
    // ended up on which channel.
    double dcGain (const juce::AudioBuffer<float>& buffer, int channel)
    {
        double sum = 0.0;
        const auto* samples = buffer.getReadPointer (channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sum += (double) samples[i];
        return sum;
    }

    const std::vector<float> leftDb (numBins, 6.0f);   // +6 dB -> DC gain 2.0
    const std::vector<float> rightDb (numBins, -6.0f); // -6 dB -> DC gain 0.5
}

TEST_CASE ("IrExport writes a mono buffer from the selected channel", "[export]")
{
    IrExport::Options options;
    options.layout = IrExport::Options::Layout::mono;

    SECTION ("left")
    {
        options.channel = IrExport::Options::Channel::left;
        const auto buffer = IrExport::buildBuffer (leftDb, rightDb, options);

        REQUIRE (buffer.getNumChannels() == 1);
        REQUIRE (buffer.getNumSamples() == fftSize);
        CHECK_THAT ((float) dcGain (buffer, 0), Catch::Matchers::WithinAbs (2.0f, 1.0e-2f));
    }

    SECTION ("right")
    {
        options.channel = IrExport::Options::Channel::right;
        const auto buffer = IrExport::buildBuffer (leftDb, rightDb, options);

        REQUIRE (buffer.getNumChannels() == 1);
        CHECK_THAT ((float) dcGain (buffer, 0), Catch::Matchers::WithinAbs (0.5f, 1.0e-2f));
    }

    SECTION ("L+R averages the two curves in dB, so +6 and -6 cancel to 0 dB")
    {
        options.channel = IrExport::Options::Channel::mid;
        const auto buffer = IrExport::buildBuffer (leftDb, rightDb, options);

        REQUIRE (buffer.getNumChannels() == 1);
        CHECK_THAT ((float) dcGain (buffer, 0), Catch::Matchers::WithinAbs (1.0f, 1.0e-2f));
    }
}

TEST_CASE ("IrExport stereo carries both corrections", "[export]")
{
    IrExport::Options options;
    options.layout = IrExport::Options::Layout::stereo;
    options.channel = IrExport::Options::Channel::mid;

    const auto buffer = IrExport::buildBuffer (leftDb, rightDb, options);

    REQUIRE (buffer.getNumChannels() == 2);
    REQUIRE (buffer.getNumSamples() == fftSize);

    CHECK_THAT ((float) dcGain (buffer, 0), Catch::Matchers::WithinAbs (2.0f, 1.0e-2f));
    CHECK_THAT ((float) dcGain (buffer, 1), Catch::Matchers::WithinAbs (0.5f, 1.0e-2f));
}

TEST_CASE ("IrExport stereo ignores the channel selection", "[export]")
{
    // Stereo always writes the plugin as it stands, so every channel setting has
    // to produce the same true-stereo file.
    IrExport::Options options;
    options.layout = IrExport::Options::Layout::stereo;

    const auto reference = IrExport::buildBuffer (leftDb, rightDb, options);
    REQUIRE (reference.getNumChannels() == 2);

    for (auto channel : { IrExport::Options::Channel::left,
                          IrExport::Options::Channel::right,
                          IrExport::Options::Channel::mid })
    {
        options.channel = channel;
        const auto buffer = IrExport::buildBuffer (leftDb, rightDb, options);

        REQUIRE (buffer.getNumChannels() == 2);
        CHECK_THAT ((float) dcGain (buffer, 0), Catch::Matchers::WithinAbs (2.0f, 1.0e-2f));
        CHECK_THAT ((float) dcGain (buffer, 1), Catch::Matchers::WithinAbs (0.5f, 1.0e-2f));

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                REQUIRE_THAT (buffer.getSample (ch, i),
                              Catch::Matchers::WithinAbs (reference.getSample (ch, i), 0.0f));
    }
}

TEST_CASE ("IrExport rejects missing or mismatched curves", "[export]")
{
    const IrExport::Options options;

    CHECK (IrExport::buildBuffer ({}, {}, options).getNumSamples() == 0);
    CHECK (IrExport::buildBuffer (leftDb, std::vector<float> (8, 0.0f), options).getNumSamples() == 0);
}

TEST_CASE ("IrExport writes a readable 32-bit float WAV", "[export]")
{
    IrExport::Options options;
    options.layout = IrExport::Options::Layout::stereo;
    options.channel = IrExport::Options::Channel::mid;

    const auto buffer = IrExport::buildBuffer (leftDb, rightDb, options);
    REQUIRE (buffer.getNumSamples() == fftSize);

    const auto file = juce::File::createTempFile (".wav");
    const auto result = IrExport::writeWav (file, buffer, 48000.0);

    REQUIRE (result.wasOk());
    REQUIRE (file.existsAsFile());

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wav.createReaderFor (new juce::FileInputStream (file), true));

    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    CHECK (reader->sampleRate == 48000.0);
    CHECK (reader->bitsPerSample == 32);
    CHECK (reader->usesFloatingPointData);
    CHECK (reader->lengthInSamples == fftSize);

    juce::AudioBuffer<float> readBack (2, fftSize);
    REQUIRE (reader->read (&readBack, 0, fftSize, 0, true, true));

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < fftSize; ++i)
            REQUIRE_THAT (readBack.getSample (ch, i),
                          Catch::Matchers::WithinAbs (buffer.getSample (ch, i), 1.0e-6f));

    file.deleteFile();
}

TEST_CASE ("IrExport rejects a curve length no FFT can produce", "[export]")
{
    // 100 bins implies a 198-sample IR, which is not a power of two.
    const std::vector<float> odd (100, 0.0f);
    CHECK (IrExport::buildBuffer (odd, odd, {}).getNumSamples() == 0);
}
