# ATELIER – DAW routing for reference-plugin analysis

To compare a reference plugin's **input** with its **output**, ATELIER needs both signals:

| ATELIER input | Carries | Role in the analysis |
|---|---|---|
| Main input (1/2) | Output of the reference plugin | "processed" (y) |
| Sidechain (3/4) | The same audio *before* the reference plugin | "unprocessed" (x) |

```
source ──┬──> [Reference plugin] ──> ATELIER main input
         └──────────────────────────> ATELIER sidechain
```

In ATELIER, set **Analysis → "Reference plugin: sidechain → main input"**.
The header shows the sidechain state:

- **NOT CONNECTED**: the DAW has not enabled ATELIER's sidechain bus. Only the processed signal can be analysed. ATELIER does not show an input/output comparison in this state.
- **SILENT**: the bus is enabled but no audio is arriving.
- **ACTIVE**: both signals are present, so the comparison is valid.

Leave ATELIER's Source on **DAW Input** for reference analysis.

> **Verification status (v0.1):** the steps below follow each DAW's standard sidechain workflow. They have **not yet been confirmed with ATELIER** in each DAW. Menu names can differ between DAW versions. Please report what you see.

---

## Reaper (most flexible)

1. Track **SRC**: put the audio here, with no FX. Turn off its *Master send*.
2. Track **REF**: set *Track channels* to **4** (route button).
   - FX: `[Reference plugin]` then `ATELIER`.
3. On SRC, add **two sends to REF**:
   - Audio 1/2 → REF channels **1/2**
   - Audio 1/2 → REF channels **3/4**
4. Leave the reference plugin's pins at 1/2 in and 1/2 out. Reaper passes channels 3/4 through untouched.
5. ATELIER's sidechain input reads channels 3/4 by default. Check this in ATELIER's *Pin connector* if needed.

## Cubase

1. Duplicate the source track (**Track → Duplicate Tracks**). The duplicate (**DRY**) carries the unprocessed audio. Set DRY's output to *No Bus*, or mute its output path.
2. On the original track, insert `[Reference plugin]` then `ATELIER`.
3. In ATELIER's plug-in window, click the **Side-Chain** button to activate the sidechain bus.
4. Click **Side-Chain routing → Add Side-Chain Source** and choose **DRY**.

## FL Studio

1. Route the channel to **Insert 1** (dry, no FX).
2. Route **Insert 1 → Insert 2** (normal route). Insert 2 holds `[Reference plugin]` then `ATELIER`.
3. Also route **Insert 1 → Insert 3** (normal route). Remove Insert 3's route to Master.
4. With Insert 2 selected, right-click Insert 3's route arrow and choose **Sidechain to this track**.
5. In ATELIER's wrapper, go to **Processing** (gear icon) → input port mapping. Assign ATELIER's **sidechain input** to the sidechain signal from Insert 3.

---

## Notes on measurement validity

- Sweeps and noise give the best transfer-function estimates.
- For harmonic analysis of a reference plugin, play a single steady sine through it. ATELIER then estimates the fundamental from the spectral peak, and the result is tagged *f0 ESTIMATED*.
- **Coherence** (green strip) shows how much of the output a linear filter of the input explains:
  - Low coherence means nonlinearity, noise, modulation (e.g. wow/flutter) or misalignment.
  - In those regions the magnitude curve is less reliable.
- ATELIER estimates the bulk delay between the two paths and reports it. Uncompensated latency appears there.
- These are measurements of behaviour. They do **not** reveal a plugin's internal circuit or component values.
