# Changelog

All notable changes to AURA are recorded here.

Follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)

## [Unreleased]

### Fixed

- The plugin no longer fades in over the first 50 ms after the host prepares it.
  `juce::dsp::Gain::reset()` snaps its smoother to the *current* target, and Gain's
  target starts at zero, so resetting before the gain had been set left the output
  ramping up from silence on every `prepareToPlay`.

## [1.0.0] — 2026-09-01

First release.

[Unreleased]: https://github.com/Celine-audio/AURA/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Celine-audio/AURA/releases/tag/v1.0.0
