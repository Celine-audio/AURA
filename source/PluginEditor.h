#pragma once

#include "PluginProcessor.h"
#include "ui/AboutPanel.h"
#include "ui/ExportPanel.h"
#include "ui/AuraLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/ParameterControl.h"
#include "ui/PhaseTabs.h"
#include "ui/IconButton.h"
#include "ui/SpectrumDisplay.h"

//==============================================================================
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshState();
    void refreshDisplay();

    // A Learn always starts a fresh take, and brings its own tab up so you can watch
    // the capture build.
    void toggleLearn (PhaseTabBar::Stage stage);
    void performMatch();

    // The licence notice, which inside a host is the only route to it.
    void showSettingsMenu();

    void showExportPanel();
    void chooseFileAndExport (IrExport::Options options);

    PluginProcessor& processorRef;
    AuraLookAndFeel lookAndFeel;

    SpectrumDisplay display;
    PhaseTabBar tabBar;

    // Icons rather than words, the way Celine's toolbar is built: three square
    // buttons on a pitch of Theme::buttonSize, with the artwork recoloured to the
    // palette rather than drawn in whatever colours the file carries.
    // A word rather than a glyph: "what does the floppy disk do" is a question a
    // toolbar should not be asking, and this is the only button here that writes a
    // file. Bypass and settings keep their icons, which are unambiguous.
    juce::TextButton exportButton { "EXPORT IR" };
    Celine::IconButton bypassButton { "Bypass", "power-off-solid-full.svg" };
    Celine::IconButton settingsButton { "Settings", "gear-solid-full.svg" };

    // The house mark, top right, tinted to the text colour. Celine draws it the
    // same way and off the same file.
    // Fade state for the two moving traces: the last frame count seen from each
    // analyzer, and how far its trace has decayed since that stopped changing.
    std::int64_t lastCurrentFrames = -1, lastReferenceFrames = -1;
    float currentFade = 1.0f, referenceFade = 1.0f;

    std::unique_ptr<juce::Drawable> logo;
    std::unique_ptr<juce::Drawable> wordmark;
    juce::Rectangle<int> logoBounds, wordmarkBounds;

    // Painted bands, laid out in resized() and filled in paint() because they sit
    // behind child components rather than being components themselves.
    juce::Rectangle<int> toolbarBand, amountPanel, outputPanel, bottomBand;

    // The bottom band is the design's light panel, so everything standing on it has
    // to flip to dark ink. Done once, after construction, rather than at each site.
    void applyPanelColours();

    // Linear phase or minimum phase: the same curve, realised two ways, and the only
    // control here that changes what the plugin costs in latency.
    juce::Label phaseLabel { {}, "PHASE" };
    juce::ComboBox phaseBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAttachment;

    // The two that act on the signal ride down either side of the graph, in the
    // order they act: how much correction goes in on the left, how much level comes
    // out on the right.
    FaderControl amountFader { processorRef.getAPVTS(), ParamID::amount,     "Amount" };
    FaderControl outputFader { processorRef.getAPVTS(), ParamID::outputGain, "Output" };

    // The two that shape the curve sit along the bottom as a list of settings. The
    // band limits are not here: they are dragged on the graph itself, where you can
    // see what they are doing.
    SliderRowControl smoothingRow { processorRef.getAPVTS(), ParamID::smoothing, "Smoothing" };
    SliderRowControl linkRow      { processorRef.getAPVTS(), ParamID::link,      "L/R Link" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Bypass reads its state back out of the parameter rather than out of the
    // button, so the icon's red fill follows automation as well as clicks.
    void refreshBypassLook();

    // Writes a band edge dragged on the graph back to the parameters, bracketed so
    // the host sees one gesture.
    static const char* parameterFor (SpectrumDisplay::BandEdge) noexcept;
    void setBand (SpectrumDisplay::BandEdge, float hz);
    void beginBandGesture (SpectrumDisplay::BandEdge, bool starting);

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Refilled every frame and handed to the display, which copies into storage it
    // already owns. Members rather than locals so neither side allocates per frame.
    std::vector<float> liveCurrentScratch, liveReferenceScratch,
                       learnedCurrentScratch, learnedReferenceScratch;

    // The correction is only re-derived a few times a second while a capture is
    // accumulating; the live traces still move at the full frame rate.
    int curveTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
