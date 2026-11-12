# Glue

A VCA bus compressor (VST3, Windows x64) built with JUCE 8. The DSP is a double-precision circuit model of a
feedback-style bus compressor. It has a diode-OR peak detector solved with Newton–Raphson, 512-point ratio/rectifier
tables, a 7.8 dB/V VCA, stepped attack/release, Range, Mix, a sidechain HPF, an external sidechain, an optional peak
clipper and 1×–8× oversampling. The GUI is a vertical hardware-style layout (big gain-reduction meter on top, scale values printed around the knobs) in the Virus Delay colours and controls. Attack, Release and Ratio are stepped and snap to the printed values. The window can be resized with the corner handle.
It keeps a fixed aspect ratio, goes from 70 % to 250 %, and remembers its size.

## Download

Every push to `main` builds the plugin on GitHub Actions. The zipped `Glue.vst3` is published to the
[`builds`](../../tree/builds) branch and also attached to each workflow run as an artifact. Unzip it into
`C:\Program Files\Common Files\VST3\`.

## Controls

| Control | Values |
|---|---|
| Threshold | −40 … 0 dB (drag/wheel 1 dB, Shift 0.1 dB) |
| Makeup | 0 … +20 dB, applied to the compressed signal only (drag/wheel 1 dB, Shift 0.1 dB) |
| Attack | 0.01, 0.1, 0.3, 1, 3, 10, 30 ms |
| Release | 0.1, 0.2, 0.4, 0.6, 0.8, 1.2 s, Auto |
| Ratio | 2:1, 4:1, 10:1 |
| Range | Full, or −80 … 0 dB maximum gain reduction (drag/wheel 1 dB, Shift 0.1 dB) |
| Mix | 0 … 100 % (drag/wheel 1 %, Shift 0.1 %) |
| SC HPF | Off, 20 … 500 Hz, detector only (drag/wheel 10 Hz, Shift 1 Hz) |
| Comp In / Peak Clip / Ext SC | switches |
| Oversampling | 1×, 2× (default), 4×, 8×; Low Latency = IIR instead of linear-phase FIR |
| Analog | circuit noise in the detector (default on) |

Hidden parameter: `DC Block` (2 Hz input/output DC blockers, default off).

## Layout

```
Source/GBusComp.{h,cpp}     DSP core (no JUCE dependency) – mirrors the spec formula by formula
Source/ratio_tables.h       ratio/rectifier tables (do not regenerate)
Source/PluginProcessor.*    host wrapper: buses, sidechain routing, oversampling, latency, state
Source/PluginEditor.*       resizable editor
Source/CustomLookAndFeel.*  procedural drawing (shared look with Virus Delay)
Source/UI/GlueComponents.*  knobs, LCDs, GR meter, help
Tests/GBusCompTests.cpp     console tests + WAV renders
```

Places where the spec had to guess are marked `// ASSUMPTION:` in the code: the parameter bounds, the 2× default,
`cos(wc)` and `tan` in the filter designs, the first Newton convergence test, the global noise seed and the auto-release
"paired state".

## Tests

`GBusCompTests [outdir]` prints the ratio-table lookups, the static curves (ratios 2/4/10, threshold −20 dB, attack
0.01 ms, release 0.1 s), Range and Peak Clip checks, the release recovery for every setting (including Auto) and
stability from 44.1 kHz to 768 kHz. It also writes `*_in.wav` / `*_out.wav` pairs at 88.2 kHz (the core only, without the
resampler): sine bursts at −30/−20/−10/0 dBFS, pink noise and a drum loop. CI runs the tests on every push and uploads
the renders as the `GBusCompTests-renders` artifact.

## Building locally (Windows)

```
cmake -B build -A x64
cmake --build build --config Release --target Glue_VST3
```

Add `-DGLUE_BUILD_STANDALONE=ON` for a standalone app, or `-DGLUE_COPY_AFTER_BUILD=ON` to install the plugin after building.
