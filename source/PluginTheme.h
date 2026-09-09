// AURA's own colour accessors, included inside namespace Celine::Theme by ui/Theme.h.
// The roles behind them are declared in PluginThemeRoles.h.
//
// No include guard and no includes of its own: this is a fragment, included at one
// point inside a namespace, and anything it needs is already there.

//======================================================================
// What each colour *means* on the graph. Named by job rather than by colour, so the
// meaning survives a change of palette -- which is now something anybody can make from
// the Theme window.
//
// The first three are chosen as a set, and the set is the point: blue and red are what mix
// to violet, so the colour of the thing the plugin builds says where it came from. Hue
// bears it out -- 199 and 357 degrees, with the correction's violet at 262, very near
// the midpoint of the two going round through purple.

/** The signal going through the plugin now. */
inline juce::Colour current() { return colour (Role::current); }

/** The material being matched to. */
inline juce::Colour reference() { return colour (Role::reference); }

/** The correction the plugin is applying. */
inline juce::Colour correction() { return colour (Role::correction); }

//======================================================================

/** A match that no longer reflects what has been learned since. Orange rather than the
    house's red, because red here already means "armed, capturing now". */
inline juce::Colour stale() { return colour (Role::stale); }

//======================================================================
// The band: the stretch of the spectrum being worked on, and everything outside it.
// Shared with nothing, because nothing else in the house draws one.

/** The band edges: the vertical rules you can take hold of and move. Not line(), which
    they used to be -- these are a control drawn as a line, and a theme that could not
    brighten them without brightening every border had no way to make them findable. */
inline juce::Colour graphLine() { return colour (Role::graphLine); }

/** The veil over the frequencies outside the band. Drawn at low alpha, so this is read
    as a tint rather than a fill. */
inline juce::Colour graphShade() { return colour (Role::graphShade); }

//======================================================================
// The light panel: the band along the bottom, and everything standing on it.
//
// The house design is two-tone and says so, but this is the one window that has the
// second tone in it. So the ground and the two inks are declared here rather than in
// the kit -- a plugin whose window is dark throughout has nothing to say about any of
// them.

/** The band itself, near-white: the design's other half. */
inline juce::Colour panel() { return colour (Role::panel); }

/** Ink on it, where the window's usual light-on-dark would be invisible. */
inline juce::Colour textOnPanel() { return colour (Role::textOnPanel); }

/** A knob cap or slider grip standing on it, dark for the same reason. handle() is
    the near-white one every other control in the window wears; against this ground it
    would disappear. */
inline juce::Colour handleOnPanel() { return colour (Role::handleOnPanel); }

//======================================================================

/** Anything live and committing -- the dot while a stage is listening, and the Learn
    button while it runs. Only this plugin captures anything. */
inline juce::Colour record() { return colour (Role::record); }
