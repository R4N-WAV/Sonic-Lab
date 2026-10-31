# TODO

## v0.3 (current)
- [x] "Maison" look drawn from the logo: linen paper, ink, camel; italic serif titles, ruler axes, garment-tag blocks, stitched parallel wires, watch-dial knobs, matching menus and dialogs
- [x] Double-click any value to type it (Alt-click resets)
- [x] Spectrum at ~60 fps from a short FFT with analyser ballistics; measurements on a slower path
- [x] Harmonics readout holds the last stable reading instead of flickering
- [x] Navigation: "Bridge / location" in the header, back button in every editor, Esc
- [x] Bridge: zoom, pan, fit, tidy; drag a wire's end to re-route or detach; Shift-drag pulls a block out; insert blocks into wires; routing menus
- [x] EQ: dense frequency/dB grid with rulers, dynamic bands drawn as curves with a hatched range
- [x] 36 factory templates (hardware styles, mixing chains, effects) + save / load / delete your own
- [x] Pitch block (rotating-tap shifter: semitones, cents, grain, stereo detune, feedback)
- [x] Blueprint: stages numbered in processing order, attack/release by synchronous demodulation (63 % and 90 %), honest noise floor; Opto release is truly two-stage and its Auto release control is hidden
- [x] Installer: two pages ("Welcome." with the folder, "Installation complete.")

## v0.2: rounds 1 + 2
- [x] Bridge: node graph between Input and Output, parallel paths (summed), cycle protection, per-block mix / output gain / bypass, latency-aligned joins, lock-free graph swaps
- [x] 17 blocks: tube, tape, transformer, console, saturation, compressor (opto / FET / VCA / vari-mu / diode), transient, EQ, filter, exciter, delay, reverb, chorus, flanger, phaser, stereo, gain
- [x] Every block: SIMPLE and ADVANCED controls
- [x] EQ: 24 bands, click-to-create, 8 shapes, slopes, stereo / L / R / M / S placement, dynamic bands, zero-latency / natural / linear phase, EQ Match, dB view ±30 / 24 / 12 / 6 / 3 / 1 / auto
- [x] Saturation: 7 harmonic recipes (silky, warm, round, square, sharp, gritty, glassy) + custom harmonic designer (H2–H10)
- [x] New UI (approved design): centred spectrum with IN / OUT / difference / freeze, readout panel (LUFS-S, true peak, correlation, THD, gain reduction), inspector, full-size EQ and saturation editors
- [x] A/B with loudness matching; undo / redo
- [x] Blueprint export: JSON + schema, PDF, PNG plots, CSV, WAV renders, implementation and validation docs, revisioned folders
- [x] Tests: 37 core DSP + 54 block + 41 plugin checks, pluginval strictness 5
- [ ] DAW test by R4N (FL Studio 25, 44.1 kHz / 512)

## Known limitations (v0.2)
- (v0.3) A tiny spur at half the test frequency (~-110 dBFS) with fast feedback compressors is not yet investigated.
- Block parameters are not host-automatable yet (only the global ones: trims, A/B, bypass). The bridge and all block settings are saved with the project.
- In linear-phase mode, dynamic EQ bands act as static bands.
- EQ Match learns the reference from Atelier's own input (or sidechain), not from a loaded file.
- Delay, chorus, flanger and tape wow use modulation, so their renders are not sample-identical between runs; the Blueprint says so.
- DAW routing steps for reference analysis are not yet confirmed in a DAW. An FL Studio guide with screenshots comes later.

## Backlog
See DEVELOPMENT_PLAN.md.
