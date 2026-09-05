#include "ThemePalette.h"

#include "../ProductInfo.h"

namespace Celine::Theme
{
    namespace
    {
        /** The file's own version. Bumped only if the meaning of what is already in one
            changes -- a new colour does not need it, because an unknown key is ignored
            and a missing one keeps its shipped value. */
        constexpr int fileVersion = 1;

        constexpr auto versionKey = "celineTheme";
        constexpr auto nameKey = "name";
        constexpr auto coloursKey = "colours";

        /** "#rrggbb", which is what somebody pasting a colour into a message expects to
            see. The alpha is dropped: every colour in the palette is opaque, and a theme
            file offering a transparency nobody can set would be a promise the editor
            does not keep. */
        juce::String toHex (juce::Colour colour)
        {
            return "#" + colour.toDisplayString (false).toLowerCase();
        }

        /** Accepts what people actually write: with or without the hash, three digits or
            six. Anything else is left alone by the caller. */
        bool fromHex (juce::String text, juce::Colour& out)
        {
            text = text.trim().removeCharacters ("#").toLowerCase();

            if (text.length() == 3)
                text = juce::String::charToString (text[0]) + juce::String::charToString (text[0])
                     + juce::String::charToString (text[1]) + juce::String::charToString (text[1])
                     + juce::String::charToString (text[2]) + juce::String::charToString (text[2]);

            if (text.length() != 6 || ! text.containsOnly ("0123456789abcdef"))
                return false;

            out = juce::Colour (0xff000000u | (juce::uint32) text.getHexValue32());
            return true;
        }
    }

    //==========================================================================
    const std::array<RoleInfo, numRoles>& info() noexcept
    {
        static const std::array<RoleInfo, numRoles> all {
            #define CELINE_THEME_ROLE_INFO(name, label, group, value) RoleInfo { #name, label, group, value },
            CELINE_THEME_ROLES (CELINE_THEME_ROLE_INFO)
            #undef CELINE_THEME_ROLE_INFO
        };

        return all;
    }

    //==========================================================================
    Palette::Palette()
    {
        reset();
        restore();
    }

    void Palette::set (Role role, juce::Colour colour)
    {
        auto& held = colours[(size_t) role];

        // Silent when it has not moved. A colour picker sends a change per mouse move,
        // and most of those land on the value it is already showing.
        if (held == colour)
            return;

        held = colour;
        sendChangeMessage();
    }

    void Palette::reset()
    {
        auto changed = false;

        for (size_t i = 0; i < numRoles; ++i)
        {
            const juce::Colour shipped { info()[i].shipped };

            changed |= colours[i] != shipped;
            colours[i] = shipped;
        }

        changed |= name.isNotEmpty();
        name = {};

        if (changed)
            sendChangeMessage();
    }

    bool Palette::isShipped() const noexcept
    {
        for (size_t i = 0; i < numRoles; ++i)
            if (colours[i] != juce::Colour (info()[i].shipped))
                return false;

        return true;
    }

    void Palette::setName (juce::String newName)
    {
        newName = newName.trim();

        if (name == newName)
            return;

        name = std::move (newName);
        sendChangeMessage();
    }

    //==========================================================================
    juce::var Palette::toVar() const
    {
        auto* entries = new juce::DynamicObject();

        for (size_t i = 0; i < numRoles; ++i)
            entries->setProperty (info()[i].key, toHex (colours[i]));

        auto* root = new juce::DynamicObject();
        root->setProperty (versionKey, fileVersion);
        root->setProperty (nameKey, name);
        root->setProperty (coloursKey, juce::var (entries));

        return juce::var (root);
    }

    juce::Result Palette::fromVar (const juce::var& source)
    {
        const auto* root = source.getDynamicObject();

        if (root == nullptr || ! root->hasProperty (coloursKey))
            return juce::Result::fail (
                juce::String::fromUTF8 ("That is not a C\xc3\xa9line theme file."));

        const auto* entries = root->getProperty (coloursKey).getDynamicObject();

        if (entries == nullptr)
            return juce::Result::fail ("That theme has no colours in it.");

        // Everything the file does not mention keeps the shipped value rather than
        // whatever happened to be on screen: loading a theme has to give the same result
        // whatever was loaded before it.
        for (size_t i = 0; i < numRoles; ++i)
        {
            const auto& entry = info()[i];
            juce::Colour parsed { entry.shipped };

            if (entries->hasProperty (entry.key))
                fromHex (entries->getProperty (entry.key).toString(), parsed);

            colours[i] = parsed;
        }

        name = root->getProperty (nameKey).toString().trim();

        sendChangeMessage();
        return juce::Result::ok();
    }

    juce::Result Palette::loadFrom (const juce::File& file)
    {
        if (! file.existsAsFile())
            return juce::Result::fail ("That theme file no longer exists.");

        juce::var parsed;

        if (juce::JSON::parse (file.loadFileAsString(), parsed).failed())
            return juce::Result::fail ("That file could not be read as a theme.");

        const auto result = fromVar (parsed);

        if (result.wasOk() && name.isEmpty())
            name = file.getFileNameWithoutExtension();

        return result;
    }

    juce::Result Palette::saveTo (const juce::File& file) const
    {
        if (! file.getParentDirectory().createDirectory())
            return juce::Result::fail ("That folder could not be written to.");

        return file.replaceWithText (juce::JSON::toString (toVar(), false, 6))
                   ? juce::Result::ok()
                   : juce::Result::fail ("That file could not be written to.");
    }

    //==========================================================================
    juce::File Palette::storedFile()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "theme";
        options.filenameSuffix = fileExtension;
        options.folderName = juce::String (juce::CharPointer_UTF8 (ProductInfo::companyName));
        options.osxLibrarySubFolder = "Application Support";

        return options.getDefaultFile();
    }

    void Palette::restore()
    {
        const auto stored = storedFile();

        if (stored.existsAsFile())
            loadFrom (stored);
    }

    void Palette::store() const
    {
        // Best effort. A theme that cannot be written is worth neither an alert in the
        // middle of a session nor losing the colours already on screen over.
        saveTo (storedFile());
    }

    //==========================================================================
    Palette& palette()
    {
        static Palette instance;
        return instance;
    }
} // namespace Celine::Theme
