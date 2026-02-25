# JuicySuite (JUCE AU/VST3)

A work-in-progress (WIP) suite of plugins (AU/VST3), created using the JUCE open-source cross-platform C++ application framework, and aimed at emphasising or maximising sonic "juiciness", as described in Hicks et al. (2024). The plugins are as follows:

Quite stable/usable:

Juicy Punch - an impact-shaper that pushes transient energy, perceived hit force, and front-edge density so that sounds feel more immediate and physically present without relying only on raw level increases.

Juicy Saturator - adds harmonic richness and controlled nonlinearities relating to sonic colour, and is designed to increase perceived weight and texture while preserving intelligibility through a mixture of drive shaping and output balancing.

Juicy Texture - uses physically inspired resonance models (gel, metal, wood, plastic, flesh-like) to add body, surface character, and impact-related feel to the source sound, with controllable damping and weight.

Juicy Width - widens the (perceived) stereo image and envelopment while monitoring mono compatibility to ensure that widened sounds are still cohesive in collapsed playback.

More experimental/may not yet be fully usable and/or useful:

Juicy Motion - introduces controlled variation over repeated events, adding evolving tone/transient/tail motion while managing repetition and contrastto avoid fatigue so the effect stays lively instead of static or overused.

Juicy Infer - an analysis hub that estimates juiciness with pre/post scoring and dimension tracking (based on the criteria in XXXX) so you can see whether processing is helping or creating fatigue risk.

Juicy Cohere - a context-fit processor that tries to align spectral balance and tail behaviour toward a learned mix profile so that “juiced” sounds still remain coherent and belong in the same sonic world as the rest of the production.


Each plugin exposes a `Juiciness Score` parameter to enable inferred juiciness to be monitored.
`JuicyInfer` now includes triangle analysis outputs (`Emphasis`, `Coherence`, `Synesthesia`) plus `Fatigue Risk` and `Repetition Density`.
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
