# chops

A low-latency performance slice sampler plugin (VST3 / AU / Standalone), inspired by
NI Battery, Amigo Sampler, and the Renoise sampler.

- **Drag & drop a sample** — it is *always embedded in the plugin state* (FLAC-compressed),
  so projects never break from missing files.
- **Slices are overlapping sections**, not partitions: each has its own start/end, an
  optional Battery-style loop region anywhere inside it, and its own playback mode.
- **Pad triggering**: sections map chromatically from C1 (MIDI 36). Zero latency reported,
  sample-accurate MIDI, RAM-resident playback.
- **LoopRun mode**: note-on plays into the loop and holds there; note-off finishes the
  current loop pass, then plays the rest of the sample to the section end.
- **Lo-fi DSP** per section and global: sample-rate reduction (sample-and-hold, no
  anti-aliasing on purpose), waveshaping distortion, repitch/finetune, reverse.

## Building

Requires CMake ≥ 3.22 and a C++20 compiler. JUCE 8 is fetched automatically.

```sh
make run    # build and launch the standalone app
make        # build all formats (VST3, AU, Standalone)
make test   # run the engine/state test suite
```

(Or invoke CMake directly: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.)

Built plugins are copied to `~/Library/Audio/Plug-Ins` on macOS.

## License

AGPLv3 — see [LICENSE](LICENSE). JUCE is used under its AGPLv3 option.
