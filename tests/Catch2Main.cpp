// All test files are included in the executable via the Glob in CMakeLists.txt

#include "juce_gui_basics/juce_gui_basics.h"
#include <ui/ThemePalette.h>
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    // This lets us use JUCE's MessageManager without leaking.
    // PluginProcessor might need this if you use the APVTS for example.
    // You'll also need it for tests that rely on juce::Graphics, juce::Timer, etc.
    // It's nicer DX when placed here vs. manually in Catch2 SECTIONs
    juce::ScopedJuceInitialiser_GUI gui;

    // And off the real theme, for the same reason. The palette writes itself whenever a
    // colour changes, and the theming tests change every colour there is -- pointed at
    // the real file, running the suite would rewrite whatever palette the person at this
    // machine had chosen.
    const juce::File themeFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getChildFile ("celine-tests-" + juce::Uuid().toString() + ".celthm");
    Celine::Theme::Palette::useFileForTesting (themeFile);

    const int result = Catch::Session().run (argc, argv);

    themeFile.deleteFile();

    return result;
}
#include <catch2/catch_test_macros.hpp>
