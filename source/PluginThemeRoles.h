#pragma once

/*
    AURA's own themeable colours, added to the house list in CelineUI/ThemeRoles.h.

    Two families. The first is PHONICS's: the page is drawn in its neutral greys, a scale
    from the window's ground up to the ink, each named for what it is used for rather than
    for how light it is. They carry PHONICS's names and values on purpose -- the two
    plugins share one look, so a theme written in either loads into the other and moves
    the same greys.

    The second is what only this plugin draws.

    The curve colours are the clearest case there is: the whole window is a comparison
    between two spectra and the correction between them, so telling those three apart
    *is* the plugin. They are chosen as a set, and the set is the point: blue and red are
    what mix to violet, so the colour of the thing the plugin builds says where it came
    from. A theme is free to break that; the shipped values keep it.

    The fourth is the one state only this plugin has: a match that no longer reflects what
    has been learned since. Orange rather than the house's red, because red here already
    means "armed, capturing now" and the two would read as the same thing.

    The band furniture is here for the same reason: only this plugin draws a *band* on its
    graph, a stretch of the spectrum with two edges you take hold of and move.

    Capture is the last. Only this plugin listens and keeps what it hears.

    See CelineUI/ThemeRoles.h for the shape of an entry and for the warning about renaming.
*/
#define CELINE_PLUGIN_THEME_ROLES(X)                                                    \
    X (phWindow,  "Window",            "Page",   0xff0f0f0f)                            \
    X (phPage,    "Page",              "Page",   0xff171717)                            \
    X (phWell,    "Well",              "Page",   0xff111111)                            \
    X (phTile,    "Tile",              "Page",   0xff1f1f1f)                            \
    X (phControl, "Control",           "Page",   0xff262626)                            \
    X (phTrack,   "Track",             "Page",   0xff333333)                            \
    X (phRule,    "Rule",              "Page",   0xff2e2e2e)                            \
    X (phInk,     "Ink",               "Page",   0xfff5f5f5)                            \
    X (phDim,     "Value, secondary",  "Page",   0xffa3a3a3)                            \
    X (phLabel,   "Label",             "Page",   0xff737373)                            \
    X (phFaint,   "Label, faint",      "Page",   0xff525252)                            \
    X (phIndex,   "Section note",      "Page",   0xff4a4a4a)                            \
                                                                                        \
    X (current,    "Current signal",    "Curves", 0xff4fc9e8)                           \
    X (reference,  "Reference",         "Curves", 0xfff2545b)                           \
    X (correction, "Correction",        "Curves", 0xff9761dc)                           \
    X (stale,      "Match out of date", "Curves", 0xffe8913f)                           \
                                                                                        \
    X (graphLine,  "Band edge",         "Band",   0xff8a8a8a)                           \
    X (graphShade, "Outside the band",  "Band",   0xff0a0a0a)                           \
                                                                                        \
    X (record,     "Recording",         "Capture", 0xffff3b6b)

/** PHONICS's values for the house's shared roles, so the kit's windows -- About, the theme
    editor, the update question -- and every popup and field wear the page's greys rather
    than the house aubergine. See PluginHouseOverrides.h for the rest of that identity. The
    accent is kept. */
#define CELINE_PLUGIN_SHARED_ROLE_VALUES(X)                                             \
    X (consoleBackground, 0xff171717)  /* phPage: the ground behind everything    */    \
    X (background,        0xff111111)  /* phWell: the recessed panes             */    \
    X (line,              0xff2e2e2e)  /* phRule: the one rule on the page       */    \
    X (headerBackground,  0xff0f0f0f)  /* phWindow: the band behind the toolbar  */    \
    X (headerText,        0xfff5f5f5)  /* phInk                                  */    \
    X (graphText,         0xffa3a3a3)  /* phDim                                  */    \
    X (grid,              0xff333333)  /* phTrack                                */    \
    X (button,            0xff262626)  /* phControl: what a button is filled with */   \
    X (field,             0xff111111)  /* phWell                                 */    \
    X (surface,           0xff262626)  /* phControl                              */    \
    X (surfaceBright,     0xff333333)  /* phTrack: hover and selection           */    \
    X (track,             0xff333333)  /* phTrack                                */    \
    X (handle,            0xfff5f5f5)  /* phInk                                  */    \
    X (text,              0xfff5f5f5)  /* phInk                                  */    \
    X (textDim,           0xffa3a3a3)  /* phDim                                  */    \
    X (comment,           0xff737373)  /* phLabel                                */    \
    X (textDisabled,      0xff525252)  /* phFaint                                */    \
    X (chrome,            0xff1f1f1f)  /* phTile: the popup and window panel     */
