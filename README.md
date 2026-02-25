# JuicySuite (JUCE AU/VST3)

Plugin suite derived from the "juiciness" report, implemented as four JUCE plugins:

- `JuicyInfer`: pass-through analyzer that infers a 0-100 juiciness score.
- `JuicyPunch`: transient/sustain shaper for punch and density.
- `JuicySaturator`: soft saturation tuned for harmonic richness.
- `JuicyWidth`: stereo widening with Haas delay and mono-safety behavior.

Each plugin exposes a `Juiciness Score` parameter so you can monitor inferred juiciness while processing.
Each plugin now also uses a custom UI meter panel showing live overall score and feature bars (Punch, Richness, Clarity, Width, Mono Safety).

## Inference Model (report-derived)

The score blends five dimensions emphasized in the report:

- `Punch`: transient-weighted envelope contrast (short vs long envelope).
- `Richness`: crest/rms proxy for harmonic density.
- `Clarity`: low-mid mud and high-band harshness penalties.
- `Width`: side-vs-mid energy contribution.
- `Mono Safety`: correlation penalty to avoid "wide but collapsing" results.

Final score:

- weighted sum of punch/richness/clarity/width
- multiplied by mono safety factor
- clamped to `0..100`

## Build

You need JUCE installed and discoverable by CMake (`JUCE_DIR` or installed package config).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

Built formats:

- AU
- VST3
- Standalone (for quick auditioning/debug)

## Notes

- The processors and analyzer are intentionally lightweight and real-time safe.
- Parameter ranges are "musical starting points" mapped from the report, not strict standards.
- Use loudness-matched A/B testing when tuning for actual production decisions.
