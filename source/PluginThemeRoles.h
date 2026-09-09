#pragma once

/*
    AURA's own themeable colours, added to the house list in `ui/ThemeRoles.h`.

    A plugin declares here whatever the shared kit has no name for. The curve colours are
    the clearest case there is: the whole window is a comparison between two spectra and
    the correction between them, so telling those three apart *is* the plugin. A theme
    that could not reach them could not re-skin it.

    They are chosen as a set, and the set is the point: blue and red are what mix to
    violet, so the colour of the thing the plugin builds says where it came from. A theme
    is free to break that; the shipped values keep it.

    The fourth is the one state only this plugin has: a match that no longer reflects what
    has been learned since. Orange rather than the house's red, because red here already
    means "armed, capturing now" and the two would read as the same thing. And its own
    role rather than one of the shared states, for the same reason the curves are not
    shared -- nothing else in the house has a match to go out of date.

    The band furniture is here for the same reason, less obviously. Every plugin in the
    house draws a graph, so labels and grid lines are shared -- but only this one draws a
    *band* on it, a stretch of the spectrum with two edges you take hold of and move. A
    plugin with no band has nothing to say about the colour of its edge, and offering it
    the control anyway is how a theme editor fills up with rows that do nothing.

    The light panel is the third case, and the one that took longest to see for what it
    is. The house design is two-tone -- dark chrome, near-white panels -- but only this
    window actually puts a panel in front of you: the band along the bottom that
    Smoothing and L/R Link stand on. So the ground, the ink that goes on it, and the
    grip that has to be findable against it are this plugin's three colours, not the
    house's. A window with no light half was being offered all three and could paint
    with none of them.

    Capture is the fourth. Only this plugin listens and keeps what it hears.

    See ui/ThemeRoles.h for the shape of an entry and for the warning about renaming.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)                                                    \
    X (current,    "Current signal",    "Curves", 0xff4fc9e8)                           \
    X (reference,  "Reference",         "Curves", 0xfff2545b)                           \
    X (correction, "Correction",        "Curves", 0xff9761dc)                           \
    X (stale,      "Match out of date", "Curves", 0xffe8913f)                           \
                                                                                        \
    X (graphLine,  "Band edge",         "Band",   0xffd9d9d9)                           \
    X (graphShade, "Outside the band",  "Band",   0xff3b334b)                           \
                                                                                        \
    X (panel,         "The panel",      "Light panel", 0xfff9fbff)                      \
    X (textOnPanel,   "Ink on it",      "Light panel", 0xff28262e)                      \
    X (handleOnPanel, "A grip on it",   "Light panel", 0xff28262e)                      \
                                                                                        \
    X (record,     "Recording",         "Capture", 0xfff92672)
