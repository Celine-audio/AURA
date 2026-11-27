#pragma once

/*
    AURA's identity beyond its colours: the faces and the corner radius the kit draws its
    own windows with (see CelineUI/HouseOverrides.h). The colours are in
    PluginThemeRoles.h, as CELINE_PLUGIN_SHARED_ROLE_VALUES.

    PHONICS's, value for value. AURA wears PHONICS's look -- neutral greys, square
    controls, Inter labels, JetBrains Mono numbers -- by the decision of the person who
    makes these plugins, and these are the choices that carry it into the kit's windows:
    About, the theme editor, the update question, and every popup, field and tooltip.

    The accent is kept, and only for what is selected or being touched.
*/

/** Square everywhere. Every rounded path in the kit reads this, so a button, a field, a
    popup and a window's panel are all square without any of them being touched. */
#define CELINE_PLUGIN_CORNER_RADIUS 0.0f

/** Inter, in the two weights the page uses: the kit's "light" is the label face, its
    "bold" the heavier one kept for titles, and the logo face the same as the titles. */
#define CELINE_PLUGIN_FONT_LIGHT "Inter-Bold.ttf"
#define CELINE_PLUGIN_FONT_BOLD  "Inter-ExtraBold.ttf"
#define CELINE_PLUGIN_FONT_LOGO  "Inter-ExtraBold.ttf"
