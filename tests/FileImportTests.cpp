#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <dsp/AudioFileSpectrum.h>
#include <dsp/MatchEngine.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr int blockSize = 512;

    // A file in the temp folder that is gone again when the test is.
    struct TempAudioFile
    {
        explicit TempAudioFile (const juce::String& extension = ".wav")
            : file (juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("aura-import-" + juce::Uuid().toString() + extension))
        {
        }

        ~TempAudioFile() { file.deleteFile(); }

        juce::File file;
    };

    void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double rate)
    {
        file.getParentDirectory().createDirectory();

        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);

        const auto options = juce::AudioFormatWriterOptions {}
                                 .withSampleRate (rate)
                                 .withNumChannels (buffer.getNumChannels())
                                 .withBitsPerSample (24);

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, options);
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
    }

    // A tone sitting exactly on a bin centre of the analysis FFT at `rate`, so it does
    // not leak, at a different level in each channel.
    juce::AudioBuffer<float> toneOnBin (int bin, double rate, int channels, double seconds)
    {
        const auto fftSize = 1 << spectrumFftOrder;
        const auto freq = (double) bin * rate / (double) fftSize;

        juce::AudioBuffer<float> buffer (channels, (int) (seconds * rate));

        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, (ch == 0 ? 0.5f : 0.125f) * (float) std::sin (2.0 * pi * freq * i / rate));

        return buffer;
    }

    size_t loudestBin (const std::vector<float>& magnitudes)
    {
        return (size_t) std::distance (magnitudes.begin(), std::max_element (magnitudes.begin(), magnitudes.end()));
    }

    // Learns one side of an engine from noise; `dullness` lowpasses it, so two sides
    // learned with different values need a correction between them.
    void learnNoise (MatchEngine& engine, MatchEngine::Side side, float dullness, int seed, int blocks = 80)
    {
        engine.setCapturing (side, true);

        std::vector<float> block (blockSize);
        juce::Random random { seed };
        float state = 0.0f;

        for (int b = 0; b < blocks; ++b)
        {
            for (auto& sample : block)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                state = dullness * state + (1.0f - dullness) * white;
                sample = 0.25f * state;
            }

            for (int ch = 0; ch < 2; ++ch)
            {
                if (side == MatchEngine::Side::source)
                    engine.pushSource (ch, block.data(), blockSize);
                else
                    engine.pushReference (ch, block.data(), blockSize);
            }
        }

        engine.setCapturing (side, false);
    }

    // The spectra of noise, as a file would give them: measured at `rate` by the same
    // analyzer a Learn uses.
    MatchEngine::Spectra noiseSpectra (float dullness, int seed)
    {
        std::array<SpectrumAnalyzer, 2> analyzers;
        std::vector<float> block (blockSize);
        juce::Random random { seed };
        float state = 0.0f;

        for (auto& analyzer : analyzers)
            analyzer.setCapturing (true);

        for (int b = 0; b < 80; ++b)
        {
            for (auto& sample : block)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                state = dullness * state + (1.0f - dullness) * white;
                sample = 0.25f * state;
            }

            for (auto& analyzer : analyzers)
                analyzer.pushBlock (block.data(), blockSize);
        }

        MatchEngine::Spectra spectra;

        for (size_t ch = 0; ch < analyzers.size(); ++ch)
            REQUIRE (analyzers[ch].getAveragedMagnitudes (spectra[ch]));

        return spectra;
    }

    void requireSameCurves (const MatchEngine::CorrectionCurves& a, const MatchEngine::CorrectionCurves& b)
    {
        REQUIRE (a.isValid());
        REQUIRE (b.isValid());
        REQUIRE (a.leftDb.size() == b.leftDb.size());

        for (size_t k = 0; k < a.leftDb.size(); ++k)
        {
            REQUIRE_THAT (a.leftDb[k], Catch::Matchers::WithinAbs (b.leftDb[k], 1.0e-4f));
            REQUIRE_THAT (a.rightDb[k], Catch::Matchers::WithinAbs (b.rightDb[k], 1.0e-4f));
        }
    }
}

//==============================================================================
TEST_CASE ("A file's spectrum is measured per channel at the file's own rate", "[import]")
{
    TempAudioFile temp;
    constexpr int bin = 120;
    writeWav (temp.file, toneOnBin (bin, 44100.0, 2, 2.0), 44100.0);

    const auto result = AudioFileSpectrum::analyse (temp.file);

    REQUIRE (result.succeeded());
    CHECK (result.sampleRate == 44100.0);

    for (const auto& channel : result.magnitudes)
    {
        REQUIRE (channel.size() == (size_t) (1 << spectrumFftOrder) / 2 + 1);
        CHECK (loudestBin (channel) == (size_t) bin);
    }

    // On the analyzer's absolute scale, so the two channels keep their 12 dB apart.
    const auto ratio = result.magnitudes[0][bin] / result.magnitudes[1][bin];
    CHECK_THAT (20.0f * std::log10 (ratio), Catch::Matchers::WithinAbs (12.04f, 0.2f));
}

TEST_CASE ("A mono file fills both channels", "[import]")
{
    TempAudioFile temp;
    writeWav (temp.file, toneOnBin (64, 48000.0, 1, 1.0), 48000.0);

    const auto result = AudioFileSpectrum::analyse (temp.file);

    REQUIRE (result.succeeded());
    CHECK (result.magnitudes[0] == result.magnitudes[1]);
}

TEST_CASE ("A file that cannot be learned from says why", "[import]")
{
    SECTION ("not audio")
    {
        TempAudioFile temp;
        REQUIRE (temp.file.replaceWithText ("not a wave file"));

        const auto result = AudioFileSpectrum::analyse (temp.file);
        CHECK_FALSE (result.succeeded());
        CHECK (result.error.contains (temp.file.getFileName()));
    }

    SECTION ("silent")
    {
        TempAudioFile temp;
        juce::AudioBuffer<float> silence (2, 48000);
        silence.clear();
        writeWav (temp.file, silence, 48000.0);

        CHECK_FALSE (AudioFileSpectrum::analyse (temp.file).succeeded());
    }

    SECTION ("shorter than one analysis window")
    {
        TempAudioFile temp;
        writeWav (temp.file, toneOnBin (64, 48000.0, 2, 0.02), 48000.0);

        CHECK_FALSE (AudioFileSpectrum::analyse (temp.file).succeeded());
    }

    SECTION ("asked to stop")
    {
        TempAudioFile temp;
        writeWav (temp.file, toneOnBin (64, 48000.0, 2, 1.0), 48000.0);

        const std::atomic<bool> stop { true };
        CHECK_FALSE (AudioFileSpectrum::analyse (temp.file, &stop).succeeded());
    }
}

//==============================================================================
TEST_CASE ("An imported take can be matched against a learned one", "[import]")
{
    MatchEngine engine;
    engine.prepare (48000.0, blockSize, 2);

    learnNoise (engine, MatchEngine::Side::source, 0.9f, 1);
    CHECK_FALSE (engine.canMatch());

    engine.importTake (MatchEngine::Side::reference, noiseSpectra (0.0f, 2), 48000.0, "reference.wav");

    CHECK (engine.getImportedTakeName (MatchEngine::Side::reference) == "reference.wav");
    CHECK (engine.canMatch());

    std::vector<float> learned;
    CHECK (engine.getLearnedMagnitudes (MatchEngine::Side::reference, learned));

    REQUIRE (engine.performMatch());
    CHECK (engine.isMatched());
    CHECK_FALSE (engine.isMatchStale());

    // A dull source against a bright reference wants the top lifting.
    const auto& curves = engine.getCorrectionCurves();
    REQUIRE (curves.isValid());
    CHECK (curves.leftDb[curves.leftDb.size() * 3 / 4] > 3.0f);
}

TEST_CASE ("An imported take is put on the engine's bin grid", "[import]")
{
    MatchEngine engine;
    engine.prepare (48000.0, blockSize, 2);

    // Measured at 44.1k, a tone on bin 200 is at 2153 Hz -- bin 183.75 at 48k.
    MatchEngine::Spectra spectra;

    for (auto& channel : spectra)
    {
        channel.assign ((size_t) (1 << spectrumFftOrder) / 2 + 1, 1.0e-4f);
        channel[200] = 1.0f;
    }

    engine.importTake (MatchEngine::Side::reference, spectra, 44100.0, "441.wav");

    std::vector<float> learned;
    REQUIRE (engine.getLearnedMagnitudes (MatchEngine::Side::reference, learned));

    const auto peak = loudestBin (learned);
    CHECK ((peak == 183 || peak == 184));
}

TEST_CASE ("A Learn replaces an imported take", "[import]")
{
    MatchEngine engine;
    engine.prepare (48000.0, blockSize, 2);

    engine.importTake (MatchEngine::Side::reference, noiseSpectra (0.0f, 2), 48000.0, "reference.wav");
    REQUIRE (engine.getImportedTakeName (MatchEngine::Side::reference).isNotEmpty());

    learnNoise (engine, MatchEngine::Side::reference, 0.5f, 3);

    CHECK (engine.getImportedTakeName (MatchEngine::Side::reference).isEmpty());
}

//==============================================================================
TEST_CASE ("The preview is what Match would build", "[preview]")
{
    MatchEngine engine;
    engine.prepare (48000.0, blockSize, 2);

    SECTION ("nothing to preview until both sides hold a take")
    {
        learnNoise (engine, MatchEngine::Side::source, 0.9f, 1);
        CHECK_FALSE (engine.getPreviewCurves().isValid());
    }

    SECTION ("before the first match, it is the curve the match then applies")
    {
        learnNoise (engine, MatchEngine::Side::source, 0.9f, 1);
        learnNoise (engine, MatchEngine::Side::reference, 0.0f, 2);

        const auto preview = engine.getPreviewCurves(); // a copy: matching empties it
        REQUIRE (preview.isValid());

        REQUIRE (engine.performMatch());
        requireSameCurves (engine.getCorrectionCurves(), preview);
    }

    SECTION ("an up-to-date match leaves nothing pending")
    {
        learnNoise (engine, MatchEngine::Side::source, 0.9f, 1);
        learnNoise (engine, MatchEngine::Side::reference, 0.0f, 2);
        REQUIRE (engine.performMatch());

        CHECK_FALSE (engine.getPreviewCurves().isValid());
    }

    SECTION ("a match gone out of date previews the next one, and leaves the applied curve alone")
    {
        learnNoise (engine, MatchEngine::Side::source, 0.9f, 1);
        learnNoise (engine, MatchEngine::Side::reference, 0.0f, 2);
        REQUIRE (engine.performMatch());

        const auto appliedBefore = engine.getCorrectionCurves();

        engine.importTake (MatchEngine::Side::reference, noiseSpectra (0.6f, 4), 48000.0, "darker.wav");
        REQUIRE (engine.isMatchStale());

        const auto preview = engine.getPreviewCurves();
        REQUIRE (preview.isValid());

        // Still the old filter until Match is pressed...
        requireSameCurves (engine.getCorrectionCurves(), appliedBefore);

        // ...and the new one once it is, which is the one that was previewed.
        REQUIRE (engine.performMatch());
        requireSameCurves (engine.getCorrectionCurves(), preview);
        CHECK_FALSE (engine.getPreviewCurves().isValid());
    }

    SECTION ("it follows the settings, like the applied curve does")
    {
        learnNoise (engine, MatchEngine::Side::source, 0.9f, 1);
        learnNoise (engine, MatchEngine::Side::reference, 0.0f, 2);

        const auto full = engine.getPreviewCurves().leftDb;

        MatchEngine::Settings halved;
        halved.design.amount = 0.5f;
        engine.setSettings (halved);

        const auto& half = engine.getPreviewCurves().leftDb;
        REQUIRE (half.size() == full.size());

        // At 2 kHz, where the curve is well inside the limits: higher up it reaches the
        // boost ceiling at full amount, and a clamped value does not halve.
        const auto k = (size_t) std::round (2000.0 / (48000.0 / (double) (1 << spectrumFftOrder)));
        REQUIRE (std::abs (full[k]) < 18.0f);
        CHECK_THAT (half[k], Catch::Matchers::WithinAbs (full[k] * 0.5f, 0.05f));
    }
}
