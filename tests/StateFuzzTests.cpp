#include <PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

/*
    What a host is allowed to hand back, which is anything at all.

    setStateInformation is the one entry point a plugin does not control the input to.
    A project file truncated by a crash, a session saved by a newer build, a host that
    hands the wrong plugin's chunk to the wrong plugin -- all of these arrive here, and
    all of them arrive as a pointer and a length with nothing to say which it is.

    AURA carries a second thing in its chunk: the learned match, as a child element
    lifted out before the parameter tree is replaced and put back afterwards. So there
    are two ways for a chunk to be wrong here rather than one, and a child that is
    missing, truncated or nonsense has to leave a working plugin behind either way.

    The bar is not that the settings survive. It is that the plugin comes back with a
    working parameter tree and keeps passing audio, because the alternative is a crash
    inside the host's project load, which takes the session with it.
*/
namespace
{
    constexpr double rate = 48000.0;
    constexpr int blockSize = 512;

    void expectStillWorking (PluginProcessor& plugin)
    {
        REQUIRE (plugin.getAPVTS().getParameter (ParamID::amount) != nullptr);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
        {
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            plugin.processBlock (buffer, midi);
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                REQUIRE (std::isfinite (buffer.getSample (channel, s)));
    }
}

TEST_CASE ("Malformed state is survived rather than trusted", "[processor][state]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (rate, blockSize);

    juce::MemoryBlock good;
    plugin.getStateInformation (good);
    REQUIRE (good.getSize() > 0);

    SECTION ("nothing at all")
    {
        plugin.setStateInformation (nullptr, 0);
        expectStillWorking (plugin);
    }

    SECTION ("a length that does not match the data")
    {
        plugin.setStateInformation (good.getData(), 0);
        expectStillWorking (plugin);

        plugin.setStateInformation (good.getData(), 1);
        expectStillWorking (plugin);
    }

    SECTION ("good state, truncated anywhere")
    {
        // Every prefix of a valid chunk, which is what a crash mid-write leaves.
        for (int size = 1; size < (int) good.getSize(); size += 17)
        {
            plugin.setStateInformation (good.getData(), size);
            REQUIRE (plugin.getAPVTS().getParameter (ParamID::amount) != nullptr);
        }

        expectStillWorking (plugin);
    }

    SECTION ("random bytes")
    {
        juce::Random random { 20260912 };

        for (int attempt = 0; attempt < 64; ++attempt)
        {
            juce::MemoryBlock noise ((size_t) random.nextInt ({ 1, 512 }));

            for (size_t i = 0; i < noise.getSize(); ++i)
                noise[i] = (char) random.nextInt (256);

            plugin.setStateInformation (noise.getData(), (int) noise.getSize());
        }

        expectStillWorking (plugin);
    }

    SECTION ("another plugin's state")
    {
        // Well-formed XML in a binary chunk, with a tag this plugin has never heard of.
        // replaceState on a foreign tree throws away every parameter, so the guard for
        // this is a tag-name check rather than a well-formedness one.
        juce::XmlElement foreign ("SomeOtherPluginsState");
        foreign.setAttribute ("gain", 0.5);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (foreign, block);

        plugin.setStateInformation (block.getData(), (int) block.getSize());

        REQUIRE (plugin.getAPVTS().getParameter (ParamID::amount) != nullptr);
        expectStillWorking (plugin);
    }

    SECTION ("values a long way outside every range")
    {
        auto tree = plugin.getAPVTS().copyState();

        for (auto child : tree)
            if (child.hasProperty ("value"))
                child.setProperty ("value", 1.0e30, nullptr);

        if (const auto xml = tree.createXml())
        {
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            plugin.setStateInformation (block.getData(), (int) block.getSize());
        }

        expectStillWorking (plugin);
    }

    SECTION ("the match child is missing")
    {
        // A chunk written before the match was ever stored, which is what a session
        // saved by an older build is.
        auto tree = plugin.getAPVTS().copyState();

        if (const auto xml = tree.createXml())
        {
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            plugin.setStateInformation (block.getData(), (int) block.getSize());
        }

        expectStillWorking (plugin);
    }

    SECTION ("the match child is nonsense")
    {
        auto tree = plugin.getAPVTS().copyState();

        if (auto xml = tree.createXml())
        {
            auto* match = xml->createNewChildElement ("MatchState");
            match->setAttribute ("sampleRate", -1.0);
            match->setAttribute ("bins", -7);
            match->addTextElement ("not a spectrum");

            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            plugin.setStateInformation (block.getData(), (int) block.getSize());
        }

        expectStillWorking (plugin);
    }

    SECTION ("good state still restores after all of that")
    {
        plugin.setStateInformation (nullptr, 0);
        plugin.setStateInformation (good.getData(), (int) good.getSize());

        expectStillWorking (plugin);
    }
}
