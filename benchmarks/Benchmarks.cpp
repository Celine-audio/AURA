#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/SpectrumAnalyzer.h"

#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Boot performance")
{
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Processor destructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::destructable_object<PluginProcessor>> storage (size_t (meter.runs()));
        for (auto& s : storage)
            s.construct();
        meter.measure ([&] (int i) { storage[(size_t) i].destruct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;

        // due to complex construction logic of the editor, let's measure open/close together
        meter.measure ([&] (int /* i */) {
            auto editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return plugin.getActiveEditor();
        });
    };
}

TEST_CASE ("Match rebuild performance")
{
    // A knob drag re-derives the curves and rebuilds the IR, so these two costs
    // decide how often the convolution can be reloaded without stalling anything.
    const int fftSize = 4096;
    const int numBins = fftSize / 2 + 1;

    std::vector<float> source (numBins), reference (numBins);
    juce::Random random { 4321 };
    for (int k = 0; k < numBins; ++k)
    {
        source[(size_t) k] = 0.05f + random.nextFloat() * 0.5f;
        reference[(size_t) k] = 0.05f + random.nextFloat() * 0.5f;
    }

    FilterDesigner::Params params;

    BENCHMARK ("Correction curve, 1/3 octave smoothing")
    {
        params.smoothingOctaves = 1.0f / 3.0f;
        return FilterDesigner::computeCorrectionDb (source, reference, 48000.0, params);
    };

    BENCHMARK ("Correction curve, 3 octave smoothing")
    {
        params.smoothingOctaves = 3.0f;
        return FilterDesigner::computeCorrectionDb (source, reference, 48000.0, params);
    };

    const auto curve = FilterDesigner::computeCorrectionDb (source, reference, 48000.0, params);
    const auto magnitudes = FilterDesigner::dbToMagnitudes (curve);

    BENCHMARK ("Linear-phase IR build")
    {
        return FilterDesigner::buildLinearPhaseIR (magnitudes, fftSize);
    };
}

TEST_CASE ("UI frame performance")
{
    // What one frame of a knob drag costs on the message thread. The editor's timer
    // runs at 30 Hz, so everything here has to fit inside ~33 ms alongside the knob's
    // own repaint — if it doesn't, the knob visibly stutters under the mouse.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    auto pushNoise = [&] (int numBlocks, float dullness, int seed)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        juce::Random random { seed };
        float state = 0.0f;

        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const auto white = random.nextFloat() * 2.0f - 1.0f;
                state = dullness * state + (1.0f - dullness) * white;
                const auto sample = (dullness > 0.0f ? state * 2.5f : white) * 0.25f;
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }

            plugin.processBlock (buffer, midi);
        }
    };

    pushNoise (0, 0.0f, 1);

    plugin.setReferenceCapturing (true);
    pushNoise (80, 0.0f, 1);
    plugin.setReferenceCapturing (false);

    plugin.setSourceCapturing (true);
    pushNoise (80, 0.85f, 2);
    plugin.setSourceCapturing (false);

    plugin.performMatch();

    auto* editor = plugin.createEditorAndMakeActive();
    pushNoise (40, 0.7f, 3);

    BENCHMARK ("Correction curves, both channels")
    {
        plugin.markCorrectionDirty();
        return plugin.getCorrectionCurves().leftDb.size();
    };

    juce::Image canvas (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

    BENCHMARK ("Editor repaint")
    {
        juce::Graphics g (canvas);
        editor->paintEntireComponent (g, true);
        return canvas.getWidth();
    };

    plugin.editorBeingDeleted (editor);
    delete editor;
}

TEST_CASE ("Audio thread performance")
{
    // The hot path. AURA convolves every sample against a 4096-tap linear-phase
    // filter, so this is where a bad decision costs a user their track count.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random random { 1234 };

    const auto fill = [&]
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = (random.nextFloat() * 2.0f - 1.0f) * 0.25f;
            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }
    };

    BENCHMARK_ADVANCED ("processBlock, unmatched")
    (Catch::Benchmark::Chronometer meter)
    {
        fill();
        meter.measure ([&] (int) { plugin.processBlock (buffer, midi); return buffer.getSample (0, 0); });
    };

    plugin.setReferenceCapturing (true);
    for (int b = 0; b < 80; ++b) { fill(); plugin.processBlock (buffer, midi); }
    plugin.setReferenceCapturing (false);

    plugin.setSourceCapturing (true);
    for (int b = 0; b < 80; ++b) { fill(); plugin.processBlock (buffer, midi); }
    plugin.setSourceCapturing (false);

    plugin.performMatch();

    BENCHMARK_ADVANCED ("processBlock, matched")
    (Catch::Benchmark::Chronometer meter)
    {
        fill();
        meter.measure ([&] (int) { plugin.processBlock (buffer, midi); return buffer.getSample (0, 0); });
    };
}

//==============================================================================
// Splits the audio-thread cost of the convolution in two: the directly-convolved
// head, which is what buys zero latency, and the partitioned tail.
TEST_CASE ("Convolver head and tail")
{
    const auto make = [] (int irLength)
    {
        auto conv = std::make_unique<PartitionedConvolver>();
        conv->prepare (2, irLength);

        juce::AudioBuffer<float> ir (2, irLength);
        juce::Random r (1);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < irLength; ++i)
                ir.setSample (c, i, r.nextFloat() * 0.01f);

        conv->setImpulseResponse (ir);
        return conv;
    };

    auto headOnly = make (256);     // numPartitions == 0, so the tail is skipped
    auto full     = make (8192);

    juce::AudioBuffer<float> block (2, 512);
    juce::Random r (2);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 512; ++i)
            block.setSample (c, i, r.nextFloat() - 0.5f);

    juce::AudioBuffer<float> work (2, 512);

    BENCHMARK ("head only, 256-tap direct")
    {
        work.makeCopyOf (block);
        headOnly->process (work);
        return work.getSample (0, 0);
    };

    BENCHMARK ("head plus tail, 8192 taps")
    {
        work.makeCopyOf (block);
        full->process (work);
        return work.getSample (0, 0);
    };
}

//==============================================================================
TEST_CASE ("Analyzer throughput")
{
    // Six of these run at once while learning with the editor open: two capture
    // channels each for source and reference, plus the two live traces.
    SpectrumAnalyzer analyzer;
    analyzer.setCapturing (true);

    std::vector<float> block (512);
    juce::Random r (3);
    for (auto& s : block) s = r.nextFloat() - 0.5f;

    BENCHMARK ("pushBlock, 512 samples")
    {
        analyzer.pushBlock (block.data(), 512);
        return analyzer.getFrameCount();
    };
}
