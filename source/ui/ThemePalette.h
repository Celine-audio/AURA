#pragma once

#include "ThemeRoles.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace Celine::Theme
{
    //==========================================================================
    /** Every colour the interface can be told to use, in the order `ThemeRoles.h`
        lists them. */
    enum class Role
    {
        #define CELINE_DECLARE_THEME_ROLE(name, label, group, value) name,
        CELINE_THEME_ROLES (CELINE_DECLARE_THEME_ROLE)
        #undef CELINE_DECLARE_THEME_ROLE
        count
    };

    inline constexpr auto numRoles = (size_t) Role::count;

    /** One role, as data: what a `.celthm` file calls it, what the editor calls it,
        which group it is edited under, and the value the design ships with. */
    struct RoleInfo
    {
        const char* key;
        const char* label;
        const char* group;
        std::uint32_t shipped;
    };

    /** All of them, in `Role` order, so `info()[(size_t) role]` is that role's. */
    const std::array<RoleInfo, numRoles>& info() noexcept;

    //==========================================================================
    /**
        The colours in force, and the file they can be written to and read from.

        A ChangeBroadcaster because a colour is not something anything polls: the window
        listens, and repaints itself and re-applies the look and feel when told. There is
        one of these per process -- see `palette()` -- so a plugin instance changing a
        colour changes it in every instance in the session, which is what somebody
        picking a colour expects to happen.

        Nothing here is realtime-safe, and nothing needs to be: this is the message
        thread's, read at paint time and written from the theme editor.
    */
    class Palette : public juce::ChangeBroadcaster
    {
    public:
        Palette();

        juce::Colour get (Role role) const noexcept { return colours[(size_t) role]; }

        /** Sets one colour and tells the interface. Silent when the colour has not
            actually moved, so dragging a colour picker across a value it is already on
            does not repaint the window. */
        void set (Role, juce::Colour);

        /** Back to what the design ships with. */
        void reset();
        bool isShipped() const noexcept;

        /** What this theme is called. Carried in the file so a shared theme arrives
            with its name rather than as the filename somebody happened to save it as. */
        juce::String getName() const { return name; }
        void setName (juce::String);

        //======================================================================
        // The .celthm file: JSON, one object of key to "#rrggbb".
        //
        // Readable and diffable on purpose -- a theme is a thing people pass around and
        // paste into messages. Keys a build does not know are ignored and keys it knows
        // but the file omits keep their shipped value, so a theme written by a plugin
        // with more colours than this one still loads, and still loads after a colour is
        // added here.

        static constexpr auto fileExtension = ".celthm";
        static constexpr auto fileWildcard = "*.celthm";

        juce::var toVar() const;
        juce::Result fromVar (const juce::var&);

        juce::Result loadFrom (const juce::File&);
        juce::Result saveTo (const juce::File&) const;

        /** Where the theme in force is kept between sessions: one file under the
            company folder, shared by every Céline plugin. Theming one of them themes
            all of them, which is the point of a house look. */
        static juce::File storedFile();

        /** Reads `storedFile()` if it is there. Anything wrong with it is not worth
            interrupting a session for -- the plugin opens on the shipped colours. */
        void restore();
        void store() const;

    private:
        std::array<juce::Colour, numRoles> colours;
        juce::String name;
    };

    //==========================================================================
    /** The one in force.

        A function-local static for the same reason the colours themselves used to be:
        nothing orders one translation unit's initialisation against another's, and a
        palette built at namespace scope could be read by a look and feel constructed
        before it. This one cannot exist before its first use. */
    Palette& palette();

    /** The colour a role is wearing right now. Every accessor in Theme.h is one of
        these, and paint code calls them freely -- it is an array index behind a guard
        the compiler reduces to a relaxed load. */
    inline juce::Colour colour (Role role) noexcept { return palette().get (role); }
} // namespace Celine::Theme
