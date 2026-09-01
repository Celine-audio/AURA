#include "AboutPanel.h"

#include "EmbeddedAssets.h"
#include "Fonts.h"
#include "Theme.h"

#include <cmath>

using namespace Celine;

namespace
{
    /** The About window's prose. One string rather than a stack of labels: a licence
        summary trimmed to fit a layout is a licence summary that has been changed. */
    juce::String aboutBodyText()
    {
        return juce::String::fromUTF8 (
            "Copyright \xc2\xa9 2026 C\xc3\xa9line Audio.\n"
            "\n"
            "Built " __DATE__ " -- JUCE 9.0.1, C++23.\n"
            "\n"
            "\n"
            "LICENCE\n"
            "\n"
            "AURA is free software: you may redistribute it and modify it under the terms of the GNU Affero General Public Licence, version 3.\n"
            "\n"
            "It comes with ABSOLUTELY NO WARRANTY, to the extent permitted by law.\n"
            "\n"
            "Source, including the exact commit this build came from:\n"
            "    https://github.com/Celine-audio/Aura\n"
            "\n"
            "Full licence text:\n"
            "    https://www.gnu.org/licenses/agpl-3.0.html\n"
            "\n"
            "\n"
            "WHY AGPL\n"
            "\n"
            "AURA being free open-source software using the JUCE framework, using its free licence, it inherits its AGPLv3 terms. AURA is then under the GNU AGPL v3 licence.\n"
            "\n"
            "In practice:\n"
            "\n"
            "  * Using it costs nothing and obliges nothing. The licence governs distributing the software, not what you make with it. Audio you process through AURA, and the curves and impulse responses you export, are your own work.\n"
            "\n"
            "  * You may fork, modify and redistribute it, provided you do so under the AGPLv3 licence and pass the source on. You may not relicense it or ship a closed-source build of it.\n"
            "\n"
            "  * Anyone you give a binary to is entitled to the corresponding source for that exact build. Development happens in public and each release is built from a tagged commit, which is how that right is served.\n"
            "\n"
            "\n"
            "THIRD-PARTY COMPONENTS\n"
            "\n"
            "Bundled inside every build, keeping their own licences rather than AURA's:\n"
            "\n"
            "  JUCE 9.0.1 ................... AGPLv3, \xc2\xa9 Raw Material Software Limited\n"
            "  clap-juce-extensions ......... MIT, \xc2\xa9 2019-2020 Paul Walker\n"
            "  Font Awesome Free icons ...... CC BY 4.0, \xc2\xa9 Fonticons, Inc.\n"
            "  Jura typeface ................ SIL Open Font Licence 1.1, \xc2\xa9 2019 The Jura Project Authors\n"
            "  JetBrains Mono typeface ...... SIL Open Font Licence 1.1, \xc2\xa9 2020 The JetBrains Mono Project Authors\n"
            "  Nico Moji typeface ........... SIL Open Font Licence 1.1, \xc2\xa9 2016 The Nico Moji Project Authors\n"
            "\n"
            "Libraries JUCE vendors inside its own modules, compiled in as part of JUCE and all permissively licensed:\n"
            "\n"
            "  VST\xc2\xae" "3 SDK .................... MIT, \xc2\xa9 2025 Steinberg Media Technologies GmbH\n"
            "  LunaSVG and PlutoVG .......... MIT. JUCE 9's SVG parser\n"
            "  LV2 SDK ...................... ISC\n"
            "  HarfBuzz ..................... MIT\n"
            "  SheenBidi .................... Apache 2.0\n"
            "  zlib, pnglib ................. zlib\n"
            "  jpeglib ...................... Independent JPEG Group\n"
            "  FLAC, Ogg Vorbis ............. BSD\n"
            "  AudioUnitSDK ................. Apache 2.0 (macOS builds only)\n"
            "\n"
            "VST is a registered trademark of Steinberg Media Technologies GmbH.\n"
            "\n"
            "Font Awesome Free is CC BY 4.0, which makes attribution a condition of use rather than a courtesy.\n"
            "\n"
            "Used only to build and test AURA:\n"
            "\n"
            "  Pamplejuce ................... MIT, \xc2\xa9 2022 Sudara Williams. The CMake setup\n"
            "                                 and CI started as this template, and the file\n"
            "                                 you are reading replaced its licence here\n"
            "  cmake-includes ............... MIT, \xc2\xa9 Sudara Williams. The shared CMake\n"
            "                                 modules in cmake/, carried as a submodule\n"
            "  Catch2 3.8.1 ................. Boost Software Licence 1.0\n"
            "  CPM.cmake .................... MIT\n"
            "\n"
            "The repository's LICENSE and THIRD-PARTY-NOTICES files carry the full account, including the verbatim licence of every bundled work.\n");
    }

}

//==============================================================================
AboutPanel::AboutPanel (const juce::String& heading)
{
    setLookAndFeel (&lookAndFeel);
    setSize (700, 620);

    // Shown as supplied — these are other people's marks, not our artwork.
    vstMark  = Celine::Assets::drawable ("vst-compatible.png");
    auMark   = Celine::Assets::drawable ("format-au.svg");
    clapMark = Celine::Assets::drawable ("format-clap.png");
    lv2Mark  = Celine::Assets::drawable ("format-lv2.svg");

    // The identity, as artwork, and it leads the window: Apple requires the Audio
    // Units mark to be "clearly subordinate in both size and placement to the primary
    // company or product identity". Placement is the clearer half -- this pair is at
    // the top and read first, the format marks are in the footer. On size, the badge
    // is one 50px square where the identity is a lockup running the better part of
    // 300px across, so the comparison to make is the lockup's, not the house mark's
    // alone. If that ever needs more headroom, grow this pair rather than shrinking
    // the marks, which is why the two sizes were raised together.
    logo = Celine::Assets::drawable ("logo.svg");
    wordmark = Celine::Assets::drawable ("aura.svg");

    for (auto* art : { &logo, &wordmark })
        if (*art != nullptr)
            Celine::Assets::tint (**art, Theme::text());

    // One line under the mark saying what it is, since the mark itself does not.
    // Not the product name again — that is what the artwork above it already says.
    juce::ignoreUnused (heading);
    subtitle.setText (juce::String::fromUTF8 ("Spectrum matching EQ \xc2\xb7 C\xc3\xa9line Audio"),
                      juce::dontSendNotification);
    subtitle.setFont (Fonts::light (12.0f));
    subtitle.setColour (juce::Label::textColourId, Theme::comment());
    subtitle.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitle);

    // Beside the mark rather than buried in the notices. Which build you are running
    // is the first thing anyone opens this window to find out, and under the AGPL it
    // is also what identifies the source this binary corresponds to. Monospaced,
    // because it is a number to be read off and quoted rather than prose.
    version.setText (JucePlugin_VersionString, juce::dontSendNotification);
    version.setFont (Fonts::mono (13.0f));
    version.setColour (juce::Label::textColourId, Theme::comment());
    version.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (version);

    body.setMultiLine (true, true);
    body.setReadOnly (true);
    body.setScrollbarsShown (true);
    body.setCaretVisible (false);

    // Read-only still allows Select All and Copy, which is how the source URL
    // gets out of here.
    body.setPopupMenuEnabled (true);

    // JetBrains Mono, which is embedded for exactly this: the notices are a
    // dot-leader table and a proportional face turns them into ragged prose.
    body.setFont (Fonts::mono (13.0f));
    body.setColour (juce::TextEditor::backgroundColourId, Theme::background());
    body.setColour (juce::TextEditor::textColourId, Theme::textDim());
    // No outline: the notices already read as a panel, being the one dark rectangle
    // on the chrome, and a rule around them was the last light line in the window.
    body.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    body.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    body.setText (aboutBodyText(), false);
    addAndMakeVisible (body);

    // The one action here, in the correction's violet like Export: this is the
    // window's own button, not a piece of chrome.
    close.setColour (juce::TextButton::buttonColourId, Theme::correction());
    close.setColour (juce::TextButton::textColourOffId, Theme::chrome());
    addAndMakeVisible (close);
}

AboutPanel::~AboutPanel()
{
    setLookAndFeel (nullptr);
}

void AboutPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::chrome());

    if (logo != nullptr && ! logoBounds.isEmpty())
        logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    if (wordmark != nullptr && ! wordmarkBounds.isEmpty())
        wordmark->drawWithin (g, wordmarkBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    // A rule under the masthead, dividing the identity from the text. The only line
    // in the window, which is why it can be this faint and still do the job.
    if (! logoBounds.isEmpty())
    {
        g.setColour (Theme::line().withAlpha (0.15f));
        g.drawHorizontalLine (logoBounds.getBottom() + 26,
                              (float) logoBounds.getX(), (float) getWidth() - 18.0f);
    }

    for (const auto& mark : { std::pair { vstMark.get(), vstBounds },
                              std::pair { auMark.get(), auBounds },
                              std::pair { clapMark.get(), clapBounds },
                              std::pair { lv2Mark.get(), lv2Bounds } })
        if (mark.first != nullptr && ! mark.second.isEmpty())
            mark.first->drawWithin (g, mark.second.toFloat(), juce::RectanglePlacement::centred, 1.0f);
}

void AboutPanel::resized()
{
    auto area = getLocalBounds().reduced (18);

    // The masthead: both marks fitted to their own aspects and sat on a common
    // centre line, exactly as the toolbar does it, so the two windows agree.
    {
        auto masthead = area.removeFromTop (38);

        const auto place = [&masthead] (const std::unique_ptr<juce::Drawable>& art,
                                        int height, juce::Rectangle<int>& out)
        {
            if (art == nullptr)
                return;

            const auto ink = art->getDrawableBounds();
            const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
            const auto width = juce::roundToInt ((float) height * aspect);

            out = masthead.removeFromLeft (width).withSizeKeepingCentre (width, height);
        };

        place (logo, 38, logoBounds);
        masthead.removeFromLeft (22);
        place (wordmark, 27, wordmarkBounds);

        // Whatever is left of the row, which puts it just past the wordmark.
        masthead.removeFromLeft (14);
        version.setBounds (masthead);
    }

    area.removeFromTop (6);
    subtitle.setBounds (area.removeFromTop (16));

    // Clear of the rule paint() draws under the masthead.
    area.removeFromTop (22);

    auto row = area.removeFromBottom (76);
    close.setBounds (row.removeFromRight (96).withSizeKeepingCentre (96, 32));

    constexpr int gap = 20;

    // What a square mark would stand; the others are scaled from it below.
    constexpr float markSize = 50.0f;

    // Every mark is given the height that puts the same *area* on the page, rather
    // than the same height. Three of these are within a few percent of square (1.07,
    // 1.00, 0.95) and LV2 is 1.59 wide, so matching heights would have let LV2 sprawl
    // half as wide again as its neighbours and read as the loudest of the four. Equal
    // area is what makes a row of differently-shaped marks look evenly weighted. They
    // share one centre line, so they are aligned as well as balanced.
    const auto place = [&row] (const std::unique_ptr<juce::Drawable>& mark,
                               juce::Rectangle<int>& out)
    {
        if (mark == nullptr)
            return;

        const auto ink = mark->getDrawableBounds();
        const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;

        const auto height = juce::roundToInt (markSize / std::sqrt (aspect));
        const auto width = juce::roundToInt ((float) height * aspect);

        out = row.removeFromLeft (width).withSizeKeepingCentre (width, height);
    };

    place (vstMark, vstBounds);   row.removeFromLeft (gap);
    place (auMark, auBounds);     row.removeFromLeft (gap);
    place (clapMark, clapBounds); row.removeFromLeft (gap);
    place (lv2Mark, lv2Bounds);

    area.removeFromBottom (12);
    body.setBounds (area);
}

//==============================================================================
void showAboutWindow (juce::Component* associatedComponent)
{
    const juce::String product { JucePlugin_Name };

    auto panel = std::make_unique<AboutPanel> (product);

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "About " + product;
    options.dialogBackgroundColour = Theme::chrome();
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = associatedComponent;

    auto* raw = panel.get();
    options.content.setOwned (panel.release());

    // Async and self-deleting: a modal loop inside a host is how you hang a DAW.
    auto* window = options.launchAsync();

    // The window, not the content: a DialogWindow sizes itself around whatever it is
    // given, so a constraint set on the panel alone is one the drag never consults.
    if (window != nullptr)
        window->setResizeLimits (AboutPanel::minimumWidth, AboutPanel::minimumHeight, 1100, 1300);

    const juce::Component::SafePointer<juce::DialogWindow> dialog (window);

    raw->close.onClick = [dialog]
    {
        if (dialog != nullptr)
            dialog->exitModalState (0);
    };
}
