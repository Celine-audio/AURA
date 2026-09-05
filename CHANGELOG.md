# Changelog

All notable changes to AURA are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

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
