# HUSH ONE for Move Everything

Monophonic subtractive synthesizer emulating the Roland SH-101 for Move Everything.

## Features

- Monophonic note stack with selectable priority (last/lowest)
- Glide (portamento)
- Oscillator mixer: saw, pulse (PWM), sub, noise
- 4-pole lowpass filter with resonance and nonlinear feedback drive
- Separate amp and filter ADSR envelopes
- LFO modulation for pitch, PWM, and filter
- Hold and transpose controls
- State save/restore for session persistence
- Supports [TAL-BassLine-101](https://tal-software.com/products/tal-bassline-101) format `.vstpreset` files. Copy your own presets into the module's `presets/` directory for auto-discovery. The following TAL features are **not supported**:
  - Polyphony (`polymode`) — module is strictly monophonic
  - Step sequencer (`seqenabled`)
  - Arpeggiator (`arpenabled`)
  - FM synthesis (`fmpulse`, `fmsaw`, `fmsubosc`, `fmnoise`, `fmintensity`)

## Build

```bash
./scripts/build.sh
```

Output:
- `dist/hush1/` module folder
- `dist/hush1-module.tar.gz` release tarball

## Install On Move

```bash
./scripts/install.sh
```

This copies module files to:
`/data/UserData/move-anything/modules/sound_generators/hush1/`

## Test

```bash
bash tests/smoke/test_module_artifacts.sh
python3 -m pytest tests/ -q
```
