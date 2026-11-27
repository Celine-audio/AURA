# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

**AURA** — a spectrum-matching EQ, built on Céline Audio's house plugin template
(derived from [Pamplejuce](https://github.com/sudara/pamplejuce)), carrying the house
look, About window, licensing and CI.

It learns the average spectrum of what is playing, learns the spectrum of a reference,
and builds the correction between them. The correction can be applied live or exported
as an impulse response. Three stages, and the tab bar is them: Current, Reference, EQ
Curve.

## Build commands

CLion's default directories are used so CLI and IDE builds share one cache.

```bash
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

```bash
cmake --build cmake-build-debug
```

```bash
./cmake-build-debug/Tests
```

```bash
./cmake-build-release/Benchmarks
```

Release builds go in `cmake-build-release`. `ctest --test-dir cmake-build-debug`
runs tests and benchmarks together. On macOS a universal binary needs
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

## Structure

- `source/` — processor, editor, parameters, product facts (`ProductInfo.h`), and
  this plugin's own theme colours (`PluginThemeRoles.h` for the roles,
  `PluginTheme.h` for the accessors)
- `source/dsp/` — `MatchEngine` (learning and the correction), `FilterDesigner`
  (minimum and linear phase), `PartitionedConvolver`, `SpectrumAnalyzer`, `IrExport`
- `source/ui/` — AURA's own interface: `SpectrumDisplay`, `PhaseTabs`, `ExportPanel`,
  `PlotGeometry`, `WindowStepper`; PHONICS's `Style` (fonts, `Ui::Space`) and `Controls`
  (fields, segments, the toolbar's trim, plus AURA's `ParameterBox`); and
  `PluginLookAndFeel` — the seam the house kit is extended through. The kit itself is in
  `modules/CelineUI`; see "The house kit"
- `source/PluginHouseOverrides.h` — the faces and corner radius the kit draws its own
  windows with (see "The window's look")
- `tests/` — Catch2; `benchmarks/` — Catch2 benchmarks
- `assets/` — embedded as BinaryData by `cmake/Assets.cmake`, everything in the folder
- `JUCE/`, `cmake/`, `modules/clap-juce-extensions`, `modules/CelineUI` — submodules

**SharedCode** is an INTERFACE library linking the source into both the plugin and the
test targets, which is what keeps them from violating the ODR.

## The house kit

The shared interface — the theming engine and its editor, `LookAndFeelBase`, `Fonts`,
`EmbeddedAssets`, `IconButton`, `AboutPanel`, `ParameterControl` — is not in this
repository. It is **CelineUI** (github.com/Celine-audio/CelineUI), a submodule at
`modules/CelineUI` shared by AURA, GALLERY and Céline, and it is included by repository:
`#include <CelineUI/Theme.h>`. Its README lists what each plugin has to provide —
`source/ProductInfo.h`, `source/PluginThemeRoles.h`, `source/PluginTheme.h`,
`source/ui/PluginLookAndFeel.h`, and the fonts and marks in `assets/` — and the rules
the kit relies on.

- **Don't edit the kit inside `modules/CelineUI`.** A submodule checks out a commit,
  not a branch, and a change made there belongs to another repository. Edit the
  shared checkout beside the plugins (`../CelineUI`) and build against it with
  `-DCELINE_UI_DIR=../CelineUI`; a kit change is a change to every plugin, so try it
  in all of them before committing it.
- **Commit the kit first, then move this plugin's submodule to that commit.** CI builds
  exactly the commit pinned here, and a pin to a kit commit that isn't pushed breaks
  every CI run and every fresh clone. `git config push.recurseSubmodules check`
  refuses that push.
- The kit's own tests, `ThemeTests` and `ThemeReachTests`, live in CelineUI and run in
  this plugin's suite; `CMakeLists.txt` adds them.

What is *not* shared is anything a plugin decides for itself: `Theme`'s own roles
(AURA adds `current`, `reference` and `correction`), `PlotGeometry` (AURA's plot
carries two dB scales where GALLERY's carries one), and the panels above.

Conventions the kit relies on, all of them silent when broken:

- **Assets are looked up by filename, never by the BinaryData identifier.** JUCE
  derives those identifiers by *stripping* characters rather than replacing them, so
  `arrow-pointer-solid-full.svg` becomes `arrowpointersolidfull_svg`. Use
  `Celine::Assets::drawable("name.svg")`; an asset that may legitimately be absent
  passes `IfMissing::returnNull`, or it asserts on every launch in Debug.
- **Colours come from `Theme`, never from a hex literal at the call site**, and every one
  of them is a lookup rather than a constant: what they answer is whatever theme is in
  force. Two consequences, both of which are silent when broken:

  - **Read them at paint time.** A colour taken once in a constructor and handed to
    `setColour` is a snapshot, and a snapshot does not follow a theme change. Where a JUCE
    widget insists on being *told* its colours, gather them into an `applyColours()` and
    call it from both the constructor and an override of `lookAndFeelChanged()` — which is
    what the window calls on every child when the theme moves.
  - **A new colour goes in `CelineUI/ThemeRoles.h`** (or the plugin's own `PluginThemeRoles.h`),
    which is the one list the enum, the `.celthm` key, the editor's label and the shipped
    value are all generated from. `Theme.h` is where it is given a name and a reason.
- **A component's `setSize` goes last in its constructor.** It fires `resized()`, and
  `resized()` measures artwork and children that must exist by then. Called early, it
  silently places everything at zero size — and a window that later opens at a
  different size hides the bug completely.
- **Every control gets a tooltip**, and the editor owns one `juce::TooltipWindow`
  parented to itself, with `setOpaque(false)`: a tooltip paints a rounded panel, and
  an opaque component must fill every pixel it owns, so the corners outside the
  rounding come out as square spikes of whatever was in the buffer.

## Bypass

**The host's bypass is the plugin's own** (`PluginProcessor::getBypassParameter`).
Without it the host's button went through JUCE's default bypassed path, which neither
moved the toolbar button, nor faded, nor delayed by the latency the plugin reports.

It is a 30 ms crossfade (`source/dsp/BypassFade`) between the finished output and a copy
of the input delayed by that latency — which in linear phase is half a filter, 85 ms —
and **the correction keeps running while bypassed**. That looks like something to
optimise; it is not. A convolution not fed while bypassed resumes from input a third of a
second old, through up to 24 dB of boost, and came out as a burst ten times the level of
the signal. The trim is not ramped to unity either: bypass takes it away by fading out
the signal it is part of. `BypassFade.h` has the measurements, `tests/BypassTests.cpp`
holds them, and it is the house bypass now — Céline and the template carry the same
class, and GALLERY, which has no latency to match, the same fade written inline.

## Running

**Running** (`ParamID::running`, with `ParamID::window` for how many seconds, 1–30)
makes each learned take only the last few seconds rather than everything since Learn
was pressed, and keeps a match in force following it. Its control is `WindowStepper`
(`source/ui/`), PROCESSING's RUNNING row: an ON/OFF switch field, lit while Running is
on, and beside it a "− 5.0 S +" stepper of fields that sets the length -- half-second
steps up to ten seconds, whole ones past it, a held button repeats, a double-click on the
readout types a value. The stepper stays usable, dimmed, while Running is off.

- **The window is read, not recorded.** Every capture analyzer keeps, beside its
  long-term sum, a ring of quarter-second segments of the same power
  (`SpectrumAnalyzer::prepareHistory`), sized in `MatchEngine::prepare` for the longest
  window. Running only changes how many of the newest segments the average reads
  (`setWindowSegments`), so turning it on mid-take or moving the window loses nothing.
  The ring is reallocated only when the sample rate changes the cut.
- **Running never makes the first match.** Match still commits a correction;
  `MatchEngine::updateRunningMatch()` then rebuilds it from the takes whenever they have
  moved on, at most every `runningUpdateIntervalMs` (250 ms). It is driven from the
  processor's timer, not the editor's, so it keeps following with the window closed.
- **A window change is a change of take.** No frame arrives and no Learn starts, so
  `isMatchStale()` also watches `windowRevision`.
- While Running with a match in force, the EQ tab says "Match running" instead of
  "out of date", and no preview is drawn: the match is about to become it.

`tests/RunningTests.cpp` holds all of this.

## Update checks

Céline's mechanism, carried over (`source/UpdateCheck.h`). It asks GitHub's
`releases/latest` for the repository in `ProductInfo::repositoryUrl` whether a newer
AURA is out. **Off until the user says yes**: the first time a window is on screen they
are asked once, closing the question is a no, and the settings menu has "Check for
updates at launch". The answer is per machine, in `<company folder>/AURA.settings`, with
the same keys Céline uses. **One request per process, at most**, on a background thread
with a 5 s timeout (on Linux through curl, `NEEDS_CURL`, and `libcurl4-openssl-dev` in
the Linux CI), never retried. **Only a strictly newer version counts**, compared as
numbers, and an unreadable one never does. Download opens the releases page. The tests
never touch the network: a test editor is never on screen, so it never starts the check.

## The window's look

**PHONICS's look, taken whole** -- the user's decision: AURA and PHONICS are one visual
family. Neutral greys, square corners, Inter Medium labels in spaced capitals, JetBrains
Mono for every number, and the accent only for what is selected or being touched. The
source is PHONICS's own code, not a re-drawing of it: `ui/Style`, `ui/Controls` and
`ui/PluginLookAndFeel` are PHONICS's files, `PluginHouseOverrides.h` is its values (square
corners, Inter in the kit's windows), and `PluginThemeRoles.h` carries its `ph*` greys under
their own names and its values for the shared roles -- so a theme saved in one loads into
the other and moves the same greys. When PHONICS's look changes, change these with it.

- **The toolbar** is PHONICS's: the house mark, a hairline, the name in Inter ExtraBold;
  at the right EXPORT IR in words, the OUTPUT trim (`Ui::OutputFader`), and bypass and
  settings as square icon buttons, bypass filling with `danger()` while bypassed.
- **The page** is `phPage`, 10 px in from the window, its contents `Ui::Space::outer` in:
  the SPECTRUM title row, the graph, and three sheets along the foot.
- **The graph** is a PHONICS frequency plot: the plot on a well with solid rules at the
  named frequencies, its two dB scales and the frequencies in small mono on the page
  round it. PREVIEW is a field in its top right, lit in `stale()`'s orange.
- **STAGES** (`PhaseTabs`) is a sheet like PHONICS's mic sheet: three rows on a well,
  the selected one a step lighter with a short accent bar, each with its curve's colour
  as a legend line, a status in plain text (red while recording), and its fields -- FILE,
  then LEARN (lit in `record()`) or MATCH (lit in `correction()`, in `stale()` when out of
  date). **START BOTH** is in the heading band over FILE and LEARN. It starts both Learns
  *fresh* -- the one already running included -- so the two takes begin on the same
  block; with both running it reads STOP BOTH and stops them.
- **MATCH** (AMOUNT, SMOOTHING, L/R LINK) and **PROCESSING** (PHASE as segments, RUNNING,
  and LATENCY as plain text) are labelled rows on a well. A value is a `Ui::ParameterBox`:
  a field dragged up or down, shift for fine, double-click for the default, the drag run
  in the parameter's own normalised range and the field showing the parameter's own text.
- **Editable and information are drawn differently**, as in PHONICS: what can be changed
  is a field with a rule round it; what is only reported (a status, the latency, the
  scales) is plain text with no box.

The window keeps a fixed 2:1 aspect and its **minimum width, 1000 px**, is what leaves the
stage rows room for their status beside MATCH and PROCESSING.

## Releases

`.github/workflows/release.yml` builds on all three platforms and attaches one zip per
platform to the GitHub release, plus a **macOS disk image** and a **Windows installer**,
the way Céline does. Triggered by pushing a `v*` tag; `workflow_dispatch` produces the
same files without making a release.

Every archive holds VST3, CLAP and LV2, plus AU on macOS (universal). **The AAX is built
but shipped nowhere**: it needs Avid's PACE signature to load, and there is no licence to
sign it with. It stays in `FORMATS` so it keeps compiling; the zips exclude `AAX/`, and
the disk image and the installer never list it. Nothing is code signed, so Gatekeeper
and SmartScreen will warn.

- **The disk image** (`packaging/dmg/settings.py`, built by a pinned `dmgbuild` in
  `macos.yml`) is a drag-and-drop window: one row per format, the bundle on the left and
  a shortcut to the system-wide folder it installs into on the right. Finder takes the
  window size as the whole frame, title bar included, and shows the user's own tab, path
  and status bars in it, so the layout leaves room for all of them. `CLAP` and `LV2` do
  not exist on a Mac until something has installed into them, so on a fresh machine
  those two shortcuts lead nowhere.
- **The installer** (`packaging/installer.iss`, Inno Setup in `windows.yml`) puts the
  plugins in Common Files' `VST3`, `CLAP` and `LV2`, each format a component, all ticked.
  Its license page is `COPYING`. **Its `AppId` must never change**, and is AURA's own: it
  is how Windows knows a new version replaces an old one, and sharing Céline's would make
  each plugin's installer think it was upgrading the other.
- **The artwork** for both is drawn by `packaging/make-artwork.swift`, which is GALLERY's
  script so the house's installers share one identity: the product's wordmark alone in
  the header, the same tiles, arrows and type, and the wizard's icon, arrow and wordmark.
  Only the ground is AURA's own, the graph's gridlines where Céline and GALLERY draw the
  schematic's dots. It is drawn from the wordmark, the app icon, the Jura fonts and the
  theme's colours, and committed. Run
  `swift packaging/make-artwork.swift` from the root after changing any of those. The
  DMG's rows are written in both that script and `settings.py`, and must agree.

## Key configuration

Edit `CMakeLists.txt` for `PROJECT_NAME`, `PRODUCT_NAME`, `COMPANY_NAME`, `BUNDLE_ID`,
`FORMATS`, `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE`. The version is read from the
`VERSION` file. Everything else a new plugin has to say about itself — the tagline, the
repository URL, the wordmark asset — is in `source/ProductInfo.h`, which the About
window builds itself from.

## Theming

Every colour is editable at runtime, from **Theme…** in the settings menu, and a theme
can be written to and read from a `.celthm` file to be shared.

The shape of it, in four files:

- **`CelineUI/ThemeRoles.h`** — the list, as an X-macro. One entry carries four things that
  have to agree: the identifier the code uses, the label the editor shows, the group it
  is edited under, and the value the design ships with. The enum, the info table, the
  file's keys and the editor's rows are all generated from it. A plugin's own colours go
  in **`source/PluginThemeRoles.h`** — GALLERY's four cabinets, AURA's three curves —
  and its accessors in **`PluginTheme.h`**, which `Theme.h` includes inside the
  namespace so `Theme::irSlot(2)` reads exactly like `Theme::chrome()`.
- **`CelineUI/ThemePalette.h/.cpp`** — the colours in force, the `.celthm` reader and writer,
  and a `ChangeBroadcaster` so a change reaches every open window. One per process, in a
  function-local static: a palette at namespace scope could be read by a look and feel
  constructed before it.
- **`CelineUI/Theme.h`** — the accessors, each a lookup, each documented with what it is *for*.
- **`CelineUI/ThemePanel.h/.cpp`** — the editor. Live: a colour changed there reaches the
  window behind it on the next repaint, so there is no Apply to forget.

**Renaming a role breaks every theme anybody has saved**, because the identifier is the
key in the file. Adding one is free — an unknown key is ignored and a missing one keeps
its shipped value, which is what lets a theme written by a plugin with more colours than
this one still load.

**Each plugin keeps its own theme**, in `<PRODUCT_NAME>.celthm` under the company
folder — one file per plugin, so every instance of it on the machine wears the same
colours whatever host or format it is loaded as. A cab loader and a circuit designer are
not obliged to look alike.

They stay cross-compatible: a theme exported from one loads into another, and the
ignore-unknown-keys rule is what makes that work — the colours the other plugin does not
have are skipped, and the ones it has that the file omits keep their shipped values.

**Editing a colour writes nothing.** It changes what is on screen and marks the palette
unsaved; `Palette::store()` is the only thing that touches the file, and the theme
editor's **Save** button is the only thing that calls it. A colour picker sends a change
per mouse move, so anything automatic here is a file per mouse move.

**A plugin format is its own loaded module, with its own copy of everything static**, so
the VST3 and the AU open in one session are two palettes that never meet. They are
brought into step by `Palette::refreshFromDisk()`, called when a window opens and when
the theme editor opens — the two moments it can matter. Nothing polls, and nothing runs
in the background. Unsaved colours are left alone, so opening a second window cannot
take away what you are in the middle of choosing.

Two things a theme change has to do, and both are easy to leave out:
`PluginLookAndFeel::applyPalette()` re-reads everything JUCE is *told* rather than asks
for, and `sendLookAndFeelChange()` gives every child a chance to do the same. The window
does both in its `changeListenerCallback`.

## Code quality

Resolve every compile warning. Warnings are errors here.

LSP/clangd reports false positives against JUCE's module system ("undeclared
identifier", "file not found"). Ignore those unless the build actually fails.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. Some useful ones:

- [melatonin_blur](https://github.com/sudara/melatonin_blur) — fast cross-platform blurs for C++ UI (shadows, glows, frosted glass)
- [melatonin_perfetto](https://github.com/sudara/melatonin_perfetto) — performance tracing with Perfetto, great for profiling `processBlock` and paint calls
- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.
