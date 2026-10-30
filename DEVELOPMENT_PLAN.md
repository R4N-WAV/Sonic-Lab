# ATELIER by R4N: Development plan

Work proceeds in small rounds. Each round ends with a CI build you can install and test in a DAW.

## History

- **v0.0** (SONIC LAB, standalone): DSP core started. It was carried over unchanged, and the device/ASIO layer was dropped when the project moved to VST3.
- **v0.1 — this round.** VST3 foundation:
  - stereo + sidechain buses, all parameters automatable, state saved with the DAW project, latency reporting
  - sources: DAW input, generator, audio file
  - chain: Tube → EQ (reorder, enable, time-aligned A/B)
  - live spectrum, harmonics/THD/THD+N, and a black-box transfer function with coherence and delay estimate (chain mode and reference-plugin mode)
  - installer `.exe`, pluginval in CI

## Next rounds

| Round | Content |
|---|---|
| v0.2 | Feedback fixes from DAW testing. Dynamic add/remove of chain modules. Gain-matched A/B. Per-stage metering. Group delay and impulse response views. Region select and loop for files. |
| v0.3 | **Compressor Lab**: opto, FET, VCA, vari-mu, diode bridge as distinct detector/gain-element models; gain-reduction history; static curve measurement. |
| v0.4 | **Analog Lab II**: tape (hysteresis, head bump, wow/flutter, noise), transformer (core saturation, LF behaviour), console/summing. |
| v0.5 | **Automated measurement suite**: stepped-level sine sweeps (THD vs level and vs frequency), compression curve and attack/release measurement, IMD, aliasing, latency. Also with a reference plugin via sidechain routing. |
| v0.6 | **Blueprint Studio**: JSON (versioned schema), PNG graphs, CSV, WAV renders, Markdown implementation and validation files, PDF. One-click `ATELIER_EXPORT/` package. |
| v0.7 | **Stereo Lab + Effects Lab** (M/S, width, crosstalk, vectorscope; reverb, delay, modulation, exciter, transient shaper). |
| v0.8 | **Validation workflow**: load a Claude-built plugin as the "reference" and score it against a saved blueprint. |
| v1.0 | Hardening, performance, full DAW validation, documentation. |

## Testing policy

- Every DSP feature gets a numeric test in `tests/` (it measures the output, not just "it runs").
- pluginval at strictness 5 runs on every CI build.
- DAW compatibility is claimed only after testing in the DAW itself (FL Studio, Cubase, Reaper).
