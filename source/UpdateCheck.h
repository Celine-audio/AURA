#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <optional>

/** Checks GitHub once per launch for a newer AURA release. Céline's mechanism, carried
    over: the same rules, the same prompts, the same settings key names.

    Off until the user enables it. The choice is stored in a per-machine settings file
    in the company folder, beside the theme. At most one request per process, never
    retried. Only a strictly newer version is announced; an unreadable version never
    is. The request sends nothing about the user. */
namespace UpdateCheck
{
    /** Timeout for connecting, and again for reading the reply. */
    inline constexpr int timeoutMs = 5000;

    /** GitHub's latest release for ProductInfo::repositoryUrl, which excludes drafts and
        pre-releases. Derived rather than written out, so the repository is named once. */
    juce::URL latestReleaseApi();

    /** The releases page "Download" opens. */
    juce::URL releasesPage();

    /** This build's version, from the VERSION file. */
    juce::String installedVersion();

    /** The product's name for people and for folders, without the version. */
    juce::String productName();

    //==========================================================================
    /** A release worth announcing. */
    struct Release
    {
        /** Without the leading "v", e.g. "1.5.0". */
        juce::String version;
    };

    /** "v1.2.0" -> {1, 2, 0}. Trailing text in a component is ignored ("0-beta" is 0).
        Empty if a component has no digits. */
    juce::Array<int> parseVersion (const juce::String& text);

    /** True if `candidate` is strictly newer than `installed`. Missing components count
        as zero. False if either cannot be read. */
    bool isNewer (const juce::String& candidate, const juce::String& installed);

    /** The release a `releases/latest` reply describes, or nothing for an error, a
        draft, a pre-release or a tag that is not a version. */
    std::optional<Release> parseLatestRelease (const juce::String& json);

    //==========================================================================
    /** Whether to check, and whether the user has been asked. */
    class Preferences
    {
    public:
        /** Uses the real settings file. */
        Preferences();

        /** Uses the given settings file. For tests. */
        explicit Preferences (const juce::File& settingsFile);

        /** `<company folder>/AURA.settings`: per machine, shared by every instance and
            format. Céline keeps the same two keys in its own file. */
        static juce::File defaultSettingsFile();
        static juce::PropertiesFile::Options settingsOptions();

        /** Off unless the user enabled it. */
        bool isEnabled() const;
        void setEnabled (bool shouldCheck);

        /** Whether the user has been asked yet. Asked only once. */
        bool hasBeenOffered() const;
        void markOffered();

    private:
        juce::File settingsFile;
    };

    //==========================================================================
    /** The single check of this process, shared by all windows. */
    namespace Session
    {
        /** Starts the check on a background thread the first time; does nothing afterwards. */
        void startIfNeeded();

        /** Whether startIfNeeded() has been called. */
        bool hasStarted();

        /** The newer release the check found, returned once per process, then never again. */
        std::optional<Release> takeUpdateToAnnounce();
    } // namespace Session
} // namespace UpdateCheck
