#pragma once

/*
    AURA's own themeable colours, added to the house list in `ui/ThemeRoles.h`.

    A plugin declares here whatever the shared kit has no name for. These three are the
    clearest case there is: the whole window is a comparison between two spectra and the
    correction between them, so telling those three apart *is* the plugin. A theme that
    could not reach them could not re-skin it.

    They are chosen as a set, and the set is the point: blue and red are what mix to
    violet, so the colour of the thing the plugin builds says where it came from. A theme
    is free to break that; the shipped values keep it.

    See ui/ThemeRoles.h for the shape of an entry and for the warning about renaming.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)                                                    \
    X (current,    "Current signal",  "Curves", 0xff4fc9e8)                             \
    X (reference,  "Reference",       "Curves", 0xfff2545b)                             \
    X (correction, "Correction",      "Curves", 0xff9761dc)
