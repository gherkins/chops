# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

chops is a low-latency performance slice sampler plugin (C++20, JUCE 8, CMake;
VST3 + AU + Standalone; AGPLv3). One sample per instance, sliced into freely
*overlapping sections*, each mapped chromatically from C1 (MIDI 36) for pad
triggering, with Battery-style mid-sample loop regions and lo-fi DSP.

## Commands

```sh
make run     # incremental build + launch the standalone app
make         # build all formats; also refreshes ~/Library/Audio/Plug-Ins
make test    # build + run the test suite via ctest
```

- The Makefile auto-applies a `CXXFLAGS` fallback for machines whose
  CommandLineTools are missing libc++ headers. When invoking `cmake` directly
  on such a machine, export the same flags first (see Makefile header).
- Single test binary, run directly for output on failure:
  `./build/chops_tests_artefacts/Release/chops_tests` (assert-style; no filter
  flags — it is one sequential main).
- Plugin validation (do both after plugin-facing changes):
  `pluginval --strictness-level 10 --validate ~/Library/Audio/Plug-Ins/VST3/chops.vst3`
  and `auval -v aumu Chps Mxgk`.
- Drive the standalone UI without manual drag & drop:
  `chops_tests --make-standalone-state "$HOME/Library/Application Support/chops.settings"`
  writes a demo document (sliced loop, loop region, reverse) the app restores on
  launch. `--verify-standalone-state <file>` runs a settings file back through
  the restore path. Note: the standalone's settings file lives directly in
  `Application Support` (no `chops/` subfolder).

## Workflow rule (mandatory)

When a change is finished:

1. `make test` must be green; run pluginval + auval for plugin-facing changes.
2. Build all formats (`make`) so the installed VST3/AU under
   `~/Library/Audio/Plug-Ins` are refreshed for DAW testing
   (CMake `COPY_PLUGIN_AFTER_BUILD` handles the copy — just build).
3. Commit with a changelog-style message: imperative one-line summary, then a
   bulleted body of user-facing changes.
4. Push to `origin`.

## Architecture

The core invariant: **the audio thread only ever reads an immutable
`Document`**; all edits happen on the message thread by copying, mutating, and
republishing.

- **Model** (`src/model/Document.h`): `Document` = shared `SampleData` +
  `vector<Section>` + `GlobalParams`. Sections are free ranges (may overlap,
  each with optional loop region, mode, reverse, pitch/fine, sr/drive
  overrides). Zero-slice state is represented as one whole-file section, so
  the workflow never branches. Copying a Document is cheap — the sample buffer
  is behind `shared_ptr`.
- **Edits** (`src/model/Edits.h/.cpp`): pure functions over a mutable Document
  copy; the UI calls them via `ChopsEditor::applyEdit` which then
  `processor.setDocument(...)`. Convention: structural ops (split/remove/
  auto-slice) re-sort and remap notes chromatically; per-lane range/loop/fx
  edits never remap (notes stay stable, overlap allowed).
- **Threading** (`src/engine/RealtimeSwap.h`): single-producer latest-value
  exchange. `acquire()/endBlock()` on the audio thread are wait-free; retired
  documents are deleted only `kSafetyBlocks` (128) blocks after being swapped
  out. Voices keep raw pointers into retired sample data — safe because any
  voice whose sample left the current document is fast-faded (~2 ms) well
  inside that window.
- **Engine** (`src/engine/Engine.cpp`): 32 preallocated voices; sample-accurate
  MIDI by splitting each block at event offsets (raw status-byte parsing — no
  `MidiMessage` allocation on the audio thread); mono-per-section retrigger
  steal with fast fade. Every block, each running voice's DSP (`resolveFx`:
  section override falls back to global; `srOverride 0` / `driveOverride < 0`
  mean "follow global") AND its loop region are re-resolved from the *current*
  document — this is what makes FX tweaks and loop-point drags land live on
  sustaining notes. Loop wrap uses fmod, not repeated subtraction, so a live
  edit that strands the phase far outside the region re-enters in O(1).
- **Voice** (`src/engine/Voice.cpp`, JUCE-free via `Playback.h`): Catmull-Rom
  interpolated read → sample-and-hold decimator (deliberately no
  anti-aliasing) → `tanh(g*x)/tanh(g)` waveshaper → smoothed gain. LoopRun
  mode: wrap `loopEnd→loopStart` only while `held`; note-off clears `held`, so
  the pass in flight finishes and playback runs through to the section end
  (Battery loop-until-release + Renoise loop-exit). Sub-ms attack ramp
  declicks starts; Gate uses a ~3 ms release ramp.
- **State** (`src/state/State.cpp`): flagship always-embed requirement. The
  compressed blob is built ONCE at sample load (already-compressed sources
  embedded verbatim; PCM re-encoded to FLAC at ≤24 bit) and reused byte-for-
  byte on every save — never re-encode in `getStateInformation`, hosts like
  Reaper snapshot state into undo history on every param tweak. Save→load→save
  must stay byte-identical (tested).
- **UI** (`src/PluginEditor.cpp`, `src/ui/`): editor listens to the processor
  (`ChangeBroadcaster`) and pulls model snapshots; a 30 Hz timer reads the
  engine's playhead atomics. Always exactly two waveforms on screen: the main
  `WaveDisplay` (markers: click adds, drag moves linked boundaries,
  double-click removes, wheel zooms — the ONLY place section boundaries are
  edited) and ONE `SliceLane` bound to the selected slice — first by default,
  then last-triggered (pads and MIDI select; never switches during an active
  gesture). The lane shows exactly the section range, no context padding, and
  owns only the loop region and per-slice settings. Waveform painting goes through
  `PeakCache` (mip-mapped min/max) + shared `render::drawWave`, which flips to
  a sample-level path when zoomed past ~2 frames/pixel.
- **Tests** (`tests/EngineTests.cpp`): bit-exact philosophy — identity-ramp
  buffers (`sample[i] == i`) make loop/phase math assertable with float `==`
  (Catmull-Rom has linear precision, rate 1 keeps integer phases); int16
  sources make FLAC round-trips byte-exact. Prefer extending this style over
  epsilon comparisons.
