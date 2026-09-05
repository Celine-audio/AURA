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
// These three are chosen as a set, and the set is the point: blue and red are what mix
// to violet, so the colour of the thing the plugin builds says where it came from. Hue
// bears it out -- 199 and 357 degrees, with the correction's violet at 262, very near
// the midpoint of the two going round through purple.

/** The signal going through the plugin now. */
inline juce::Colour current() { return colour (Role::current); }

/** The material being matched to. */
inline juce::Colour reference() { return colour (Role::reference); }

/** The correction the plugin is applying. */
inline juce::Colour correction() { return colour (Role::correction); }
