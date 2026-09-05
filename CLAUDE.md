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

- `source/` — processor, editor, parameters, product facts (`ProductInfo.h`)
- `source/dsp/` — `MatchEngine` (learning and the correction), `FilterDesigner`
  (minimum and linear phase), `PartitionedConvolver`, `SpectrumAnalyzer`, `IrExport`
- `source/ui/` — the house kit (`Theme.h`, `Fonts`, `PluginLookAndFeel`, `AboutPanel`,
  `ParameterControl`, `IconButton`, `EmbeddedAssets`) plus AURA's own:
  `SpectrumDisplay`, `PhaseTabs`, `ExportPanel`, `PlotGeometry`
- `tests/` — Catch2; `benchmarks/` — Catch2 benchmarks
- `assets/` — embedded as BinaryData by `cmake/Assets.cmake`, everything in the folder
- `JUCE/`, `cmake/`, `modules/clap-juce-extensions` — submodules

**SharedCode** is an INTERFACE library linking the source into both the plugin and the
test targets, which is what keeps them from violating the ODR.

## The house kit

`source/ui/` is shared, near-verbatim, with the other Céline plugins — the same files
are in GALLERY. Treat a change to any of them as a change to all of them, and keep the
two in step:

`Theme.h`, `Fonts`, `EmbeddedAssets`, `IconButton`, `PluginLookAndFeel`,
`ParameterControl`, `AboutPanel`.

What is *not* shared is anything a plugin decides for itself: `Theme`'s own roles
(AURA adds `current`, `reference` and `correction`), `PlotGeometry` (AURA's plot
carries two dB scales where GALLERY's carries one), and the panels above.

Conventions the kit relies on, all of them silent when broken:

- **Assets are looked up by filename, never by the BinaryData identifier.** JUCE
  derives those identifiers by *stripping* characters rather than replacing them, so
  `arrow-pointer-solid-full.svg` becomes `arrowpointersolidfull_svg`. Use
  `Celine::Assets::drawable("name.svg")`; an asset that may legitimately be absent
  passes `IfMissing::returnNull`, or it asserts on every launch in Debug.
- **Colours come from `Theme`, never from a hex literal at the call site.** They are
  function-local statics on purpose: held as static `Colour` objects, nothing orders
  their initialisation against another translation unit's, and whichever link order
  won got an opaque black.
- **A component's `setSize` goes last in its constructor.** It fires `resized()`, and
  `resized()` measures artwork and children that must exist by then. Called early, it
  silently places everything at zero size — and a window that later opens at a
  different size hides the bug completely.
- **Every control gets a tooltip**, and the editor owns one `juce::TooltipWindow`
  parented to itself, with `setOpaque(false)`: a tooltip paints a rounded panel, and
  an opaque component must fill every pixel it owns, so the corners outside the
  rounding come out as square spikes of whatever was in the buffer.

## Key configuration

Edit `CMakeLists.txt` for `PROJECT_NAME`, `PRODUCT_NAME`, `COMPANY_NAME`, `BUNDLE_ID`,
`FORMATS`, `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE`. The version is read from the
`VERSION` file. Everything else a new plugin has to say about itself — the tagline, the
repository URL, the wordmark asset — is in `source/ProductInfo.h`, which the About
window builds itself from.

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
