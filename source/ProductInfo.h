#pragma once

#include <juce_core/juce_core.h>

/**
    The handful of facts about this plugin that are not in CMakeLists.txt, gathered
    in one place so that starting a new project means editing one file rather than
    hunting through prose.

    The About window builds itself from these, so filling them in is the whole of the
    licence-notice work a new plugin has to do.
*/
namespace ProductInfo
{
    /** One line saying what the plugin is, shown under the mark in the About window.
        Sentence case, no full stop -- it is a label, not a sentence. */
    inline constexpr auto tagline = "Spectrum matching EQ";

    /** Where the source lives. The AGPL requires that anyone given a binary can get
        the corresponding source, and this is the address that serves that right, so
        it has to be real before you ship anything. */
    inline constexpr auto repositoryUrl = "https://github.com/Celine-audio/Aura";

    /** The plugin's name drawn as artwork, embedded under this filename.

        Named here rather than at the two places that draw it -- the toolbar and the
        About window -- because those two are meant to wear the same mark, and two
        literals for one asset is how the About window came to be asking for a file
        that was never drawn and quietly setting the name as type instead.

        A plugin with no wordmark yet leaves this as it is and gets the fallback: the
        name in the display face, which is why the lookup is optional rather than
        required. */
    inline constexpr auto wordmarkAsset = "aura.svg";

    inline constexpr auto companyName = "C\xc3\xa9line Audio";
    inline constexpr auto copyrightYear = "2026";

    /** The framework version the notices claim, kept here so the About window and the
        THIRD-PARTY-NOTICES file cannot drift apart. */
    inline constexpr auto juceVersion = "9.0.1";
}
