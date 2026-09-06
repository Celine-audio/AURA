# Changelog

All notable changes to AURA are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

- `tests/ThemeReachTests.cpp`, which renders the whole editor, moves every colour the
  theme has, renders it again, and fails if anything the design ships is still on screen.
  It found four real bugs the day it was first run across all four plugins.
- **A theme is now this plugin's own**, in `<name>.celthm` under the company folder
  rather than one file shared by the house. Every instance of it on the machine wears
  the same colours whatever host or format it is loaded as, and an existing shared theme
  is inherited on first run so nothing is lost by the split. Themes stay cross-
  compatible: one exported from another Céline plugin still loads, and the colours this
  one does not have are simply skipped.
- **Button backgrounds and text fields are separate colours in the theme.** They shipped
  as one — every button wore the same slate as every panel — so a theme could not lift
  the controls off the surfaces they sit on. Two new roles, **Button** and **Text
  field**, ship at exactly the values they replace, so nothing looks different until
  somebody moves them.
- **A theming engine.** Every colour the interface draws with is editable at runtime,
  from **Theme…** in the settings menu, and can be written to and read from a `.celthm`
  file to be kept or shared. Changes show at once — the palette is what everything draws
  from, so there is no Apply to forget.
- The theme file is shared by every Céline plugin: one `theme.celthm` under the company
  folder, so theming one of them themes all of them. A key a build does not know is
  ignored and a key it knows but the file omits keeps its shipped value, which is what
  lets one file serve three plugins with different palettes.
- Plugin-specific colours are in the theme too, not just the chrome — the current,
  reference and correction curves, which are what the whole window is a comparison
  between. A palette that could not reach them could not re-skin the plugin.

### Changed

- The look and feel is split: `ui/LookAndFeelBase` carries everything the four plugins
  draw the same way, and `ui/PluginLookAndFeel` is a subclass for what this one does
  differently. Fifteen files under `source/ui/` are now byte-identical across all four,
  which is what makes the shared kit a move rather than a merge — see `CELINEUI.md`.
- Formats : **Fx|EQ** to VST3, **lv2:EQPlugin** to LV2, **equalizer** to CLAP,
  and **EQ** to AAX.
- The CLAP build declares that it handles mono/stereo.
- `Theme`'s accessors are lookups rather than constants. The shipped values, the editor
  labels and the file keys are generated from one list (`ui/ThemeRoles.h`), so the enum,
  the table, the `.celthm` format and the editor's rows cannot drift apart.
- Every control that took its colours once in a constructor now gathers them into an
  `applyColours()` called from `lookAndFeelChanged()` as well, so a theme change reaches
  them. A snapshot does not follow a theme, and the failure is silent: half the window
  in the new colours and half in the old.
- `textDisabled` and `tabInactive` are roles of their own rather than aliases of
  `comment` and `chrome`. Shipping at the same value is not the same as being one
  colour, and a theme has to be able to pull them apart.

### Added

- Tooltips. Every control has one now.
- `source/ProductInfo.h`, holding info about hte plugin that are not in
  CMakeLists: the tagline, the repository URL, the wordmark asset, the copyright. The
  About window builds itself from these.

### Changed

- The shared house kit is now the same code as GALLERY's, file for file:
  `Theme`, `Fonts`, `EmbeddedAssets`, `IconButton`, `PluginLookAndFeel`,
  `ParameterControl` and `AboutPanel`. What stays AURA's own is what AURA decides for
  itself — the `current`/`reference`/`correction` roles, and `PlotGeometry`, whose plot
  carries two dB scales where GALLERY's carries one.
- Dropdowns adopt CelineUI elements.
- `AuraLookAndFeel` is `PluginLookAndFeel`, and `auraKeepFont` is `celineKeepFont`.
- Text is Céline White (`F9FBFF`) throughout, where it had been the old warm off-white.
- Icon buttons are fill-only.
- Sliders take the house drag behaviour: the wheel does nothing, fine modifier drags finely rather than switching to JUCE's velocity mode.
- About window overhaul.

### Fixed

- **The yellow ring around whatever you were editing is gone.** JUCE draws a focus
  outline as a separate desktop window, and its default is a rounded rectangle at a fixed
  radius of three — so on a field rounded to the house radius it traced a shape the
  control does not have, sitting slightly off its corners. It also lived only as long as
  that window did, which is why it appeared on one launch and not the next. Nothing here
  needs it: a field being edited says so with its caret and its selection.
- **The theme editor's own Close button follows the accent it is showing you.** Its
  colours were set once when the window opened, so picking a new accent recoloured every
  other control in the plugin and left the button next to the swatch on the old one. The
  window's title, subtitle and status line had the same fault.
- **The toolbar's mark did not follow the theme.** The logo and the wordmark were tinted
  once when the window opened, and tinting is destructive — so they stayed on whatever
  colour the theme happened to be at that moment.
- **The About window did not follow the theme at all.** It is a window of its own, so the
  editor's `sendLookAndFeelChange` never reached it; it now listens to the palette
  directly, and its marks are re-read from the binary rather than re-tinted.
- **Group headings no longer escape the colour list.** They are painted by the panel in
  the scrolled list's coordinates, and nothing clipped them — so a heading scrolled past
  the top carried on being drawn above the list, over the subtitle and the footer. It
  showed up as headings appearing in the middle of the window whenever something made
  the panel repaint underneath the colour picker.
- The colour picker no longer paints a square panel inside a rounded bubble. It filled
  its own background, which met the bubble's rounded corners and lost the argument.
- **Theming one instance now reaches the others.** Each plugin format is a separately
  loaded module with its own copy of everything static, so the VST3 and the AU open in
  one session were two palettes that never met — theming one left the other on the old
  colours until it was reloaded. A window reads the saved theme when it opens, which is
  the moment it can matter; nothing watches the disk in the background. Colours you are
  in the middle of choosing are never overwritten by what another instance saved.
- Building a palette no longer schedules a save of the file it has just read. Reading
  any colour builds it, and the first read can come from a static initialiser — before
  there is a message loop for the save to wait on, which JUCE asserts about.
- **A theme you pick is kept — when you press Save.** It used to be live until you
  closed the plugin and then gone, because nothing wrote it. There is now a **Save**
  button in the theme editor, lit only while there is something to keep, and the status
  line says whether there is. Editing itself touches nothing: a colour picker sends a
  change per mouse move, and a preference is not worth a file per mouse move.
- Text fields no longer draw a ring when you click into them. The caret already says
  where the typing goes, and it was the one edge in the window that arrived on a click.
- A parameter moved by host automation no longer touches the audio thread with work it
  may not do. `parameterChanged` runs on whichever thread moved the parameter, and it
  was writing the engine's settings struct and posting an async update from there —
  a write racing the message thread's reads of the same fields, and a message post that
  takes a lock and can allocate. The settings now have one writer: a move made in the
  window goes across on the spot, as before, and automation sets a flag a timer picks
  up. Nothing else changes, including when the curve and an export see a control moved
  in the window — which has to be immediately, or Export would write the setting before
  the one just made.
- The About window sized itself before loading its artwork, so `resized()` measured
  drawables that did not exist yet and placed every format mark at nothing. Only the
  dialog resizing it afterwards hid this.
- A missing embedded asset asserts in Debug again instead of silently drawing nothing.

- The plugin no longer fades in over the first 50 ms after the host prepares it.
  `juce::dsp::Gain::reset()` snaps its smoother to the *current* target, and Gain's
  target starts at zero, so resetting before the gain had been set left the output
  ramping up from silence on every `prepareToPlay`.

## [1.0.0] — 2026-09-01

First release.

[Unreleased]: https://github.com/Celine-audio/AURA/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Celine-audio/AURA/releases/tag/v1.0.0
