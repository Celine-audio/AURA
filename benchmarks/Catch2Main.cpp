// Every benchmark .cpp in this directory is compiled into the executable by the
// glob in cmake/Benchmarks.cmake. Nothing needs including from here.

#include "juce_gui_basics/juce_gui_basics.h"
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    // This lets us use JUCE's MessageManager without leaking.
    // PluginProcessor might need this if you use the APVTS for example.
    // You'll also need it for tests that rely on juce::Graphics, juce::Timer, etc.
    // It's nicer DX when placed here vs. manually in Catch2 SECTIONs
    juce::ScopedJuceInitialiser_GUI gui;

    const int result = Catch::Session().run (argc, argv);

    return result;
}
