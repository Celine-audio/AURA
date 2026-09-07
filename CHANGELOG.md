# Changelog

All notable changes to AURA are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Added

- **Match stays offered while a side is being re-learned.** Arming a Learn wipes that
  analyzer, so until the first frame arrives — indefinitely, with the transport stopped —
  the side held nothing, and the Match button read that as "cannot match": it went
  half-lit the instant you pressed Learn and came back up when audio arrived. A side that
  has been learned before is not empty; it holds what it committed last time until the
  new take replaces it, and Match now falls back to that. The first match of a session is
  unchanged: with nothing committed on either side, both still have to be learned first.
- **The Match button turns orange when the match is out of date.** Start a Learn, or
  leave a capture running past a match, and the filter you are hearing is no longer the
  one your captures describe — with nothing on screen to say so. The tab now reads
  **Match out of date** and its button goes orange, which is its own themeable colour
  rather than the house red: red already means "armed, capturing now" here, and the two
  would read as the same thing. A take is identified by its number as well as its
  length, so re-learning for exactly as long as last time still counts as a new take. A
  session reloaded from state is not stale — its captures are the ones its match was
  built from.
- Closing the theme editor with colours you have not saved now asks, offering **Save**,
  **Discard** or **Cancel**. Every way out goes through it — the Close button, the escape
  key and the title bar's own close button.
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

- The preview curve is re-derived when the capture has actually moved on, rather than
  every eighth frame. An analyzer frame covers half an FFT, so one lands about 23 times
  a second at 48k and twice that at 96k — the old fixed divisor redrew seven and a half
  times a second, which visibly lagged what was being learned. Asking the frame counts
  costs nothing and is right at every sample rate, where a divisor is right at one of
  them. Deriving the curve is 0.06 ms and touches only the display; the filter in force
  is untouched until Match is pressed.
- **The correction can cut further than it boosts**, 60 dB down against 24 up, where
  both ends were 24. They are not the same risk: a boost amplifies whatever the capture
  holds at that frequency, and where the source has rolled off — a guitar cab above
  5 kHz — that is the noise floor and the analysis window's leakage rather than signal.
  A cut only ever removes. 60 is where the two phase modes still agree: past roughly 96
  the minimum-phase build floors its own magnitude, and asking for a deeper notch starts
  returning a shallower one.
- The window redraws at 60 Hz rather than 30, so the curve and the meters follow the
  pointer instead of stepping after it. The correction is still rebuilt seven and a half
  times a second while a capture is running — that count is now held against the refresh
  rate rather than written out in ticks, because rebuilding it is the expensive thing in
  that callback and doubling the frames should not double it.
- Menus and tooltips carry a faint rule, the same one the callout bubble wears. On macOS
  the window's own shadow gave them an edge for free; on Windows there is no shadow to
  borrow one from, so they ran into whatever was behind them. Drawn rather than
  inherited, so both platforms show the same thing.
- Tooltips cast a shadow, sitting inside a margin reserved for it. A tooltip is the one
  thing genuinely floating above the window, and a dark panel on a dark window with
  nothing lifting it off is just a slightly different dark. The shadow is drawn rather
  than asked for: JUCE's own shadower builds a rectangle, which behind a rounded panel is
  a dark wedge in each corner, so it stays declined. Menus keep the rule and no shadow --
  a menu is a desktop window sized to its items, so the only way to make a margin for one
  is to grow the window, which moves the menu off the button it was opened from and
  leaves the margin showing as a black box wherever the window turns out to have no
  per-pixel alpha. The tooltip can have one because it is a child of the editor rather
  than a window of its own.
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

- **Starting a Learn no longer flashes the previous take.** The learned curve falls back
  to the snapshot a committed match was taken from, which is all a reloaded session has
  to draw. Starting a Learn wipes the analyzer, so until the first frame arrived that
  fallback stood in — putting the old curve on screen for a moment before the live one
  replaced it. A side being learned now reports nothing until it has something, and the
  fallback is left to the case it was written for.
- **The spectrum no longer flickers while audio is playing.** An analyzer frame covers
  half an FFT, so one arrives about every 43 ms, while the window now redraws every 17 —
  most ticks therefore found no new frame even mid-playback, and the trace, which fades
  when nothing is arriving, started fading on the first of them and snapped back on the
  next. It now waits a quarter of a second before it begins, which is longer than the gap
  between frames at any rate the plugin will see and still short enough that a stopped
  transport does not leave a trace sitting on the graph. The fade itself is held against
  the refresh rate, so it takes the second it always took.
- **Dragging a control no longer spikes the CPU.** A rebuild designs the correction's
  impulse response, and it was constructing the 32768-point transform to do it with on
  every call — about 15 ms of setup against 0.2 ms of actual transforms, twice per
  rebuild, every 120 ms for as long as the drag lasted. That is a third of a core, on the
  thread that also has to draw the window. The transform is now kept between rebuilds,
  and when the channels are linked — which is how the plugin ships, and where both
  channels carry the same curve to the bit — the second response is copied rather than
  built a second time. A stereo rebuild went from 31 ms to 1.
- **The caret in a value box was invisible on the light strip.** It was the last thing
  in the box still taking its colour from the look and feel, which sets it for the dark
  half of the design -- so on the near-white panel the text cursor was white on white.
  It takes the row's own ink now, like the text and the selection around it.
- **The value text on the light strip turned white while you edited it.** Not the text
  colour, as it looked: opening the editor selects the whole value, so what you see the
  instant you click in is `highlightedTextColourId` -- which the look and feel sets for
  the dark half of the design, because it has no way of knowing a particular row stands
  on the other one. The selection now takes the row's own ink, with a wash of that ink
  behind it, so it reads on either side of the split.
- AURA's bottom panel -- the phase label, the phase picker and the Export button -- now
  follows a theme change. Its colours were applied once at construction, so every other
  control in the window moved with the theme and those three did not.
- The house mark and the plugin's wordmark are the same size in every plugin. GALLERY
  drew them at 26 and 18 pixels where AURA and SPACE used 20 and 14, on a header band
  that is the same height in all three.
- **Clicking into a value box no longer draws a border round it.** The slider's text box
  asked for one in the armed colour while it was being edited -- the last rule left
  anywhere in the window, and one that appeared on a click, which is exactly what made it
  read as a system control dropped into the design.
- The digits you type into a value box are the theme's ink. JUCE fills
  `textWhenEditingColourId` from its own colour scheme rather than leaving it unset, so
  the text being edited was never taking its colour from the theme.
- **Discarding a theme now puts the colours back.** It marked the change abandoned and
  left it on screen, so "discard" only meant "do not write the file" -- the window behind
  it kept the colours you had just rejected until something else reloaded the theme.
- **The theme window no longer opens behind the plugin.** Building it by hand to
  intercept every way of closing it lost the two things `DialogWindow::LaunchOptions`
  does for you: it is on top when the host keeps its own windows on top -- Ableton does,
  and the window was unreachable without closing the plugin -- and it is told the scale
  the editor is being shown at.
- Past ten seconds the learned-time readout printed the whole double. `juce::String`
  treats zero decimal places as "as many as it takes" — it is what the plain
  `String(double)` constructor passes — so "12 s learned" came out as "12.3457 s learned".
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
