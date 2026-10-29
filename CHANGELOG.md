# Changelog

## 3.0.0 - 2026-10-29 - first public release
- **INPUT + LEARN**: LEARN plays your take for 6 seconds and sets INPUT so it sits at the level every style and
  preset is designed for (a voice at -18 dBFS RMS) - a saturator reacts to level like the hardware, and a quiet take
  barely touched the curve. AUTO keeps the output level; DYNAMICS follows INPUT.
- **Live (4x)** quality for recording through it: polyphase IIR half-band filters (elliptic, stop band -111 dB,
  images -119 dB), 3 - 4 samples of latency instead of 82.
- **Vocal presets tuned on a real voice** at the designed level: the most colour that keeps the predicted listener
  score at the dry take's, the fundamental within 2 dB of it and the esses no harsher (the first drafts were far too
  heavy: up to -0.7 UTMOS).
- Measured against an open-source tape model at its 8x setting, at more saturation: aliasing -129 vs -74 dB (brutal)
  and -210 vs -83 dB (realistic), CPU 6.5 vs 10.6 %.
- Verified: DSP 181, plugin 32, ASan + UBSan clean, independent audit 54/54, pluginval strictness 10 (also with the plugin built with AddressSanitizer: clean), Steinberg's VST3 validator 47/47.

## 2.0.0 - 2026-10-29 (local milestone)
- **BANDS**: one, two or three bands (Linkwitz-Riley crossovers; the bands and the dry stay phase-coherent: the
  crossovers' magnitude re one band within 0.07 dB, phase within 0.001 deg), each with its own STYLE (or MAIN), drive
  trim and MIX - tape on the lows, a tube on the mids, the highs left clean.
- **DYNAMICS**: the drive follows the level - LOUD: the accents bite harder (+4 dB of 3rd harmonic on the loud part),
  QUIET: the soft parts are thickened (+15 dB on the quiet part), the level kept by AUTO.
- **ESS GUARD**: the suite's sibilance network (Axell Velvet 3.0's LIVE network) finds the esses and keeps them out of
  the saturation; each guarded ess gets the gain the saturation gives the rest of the material (measured on the
  unguarded parts), so the esses keep the input's balance to the vowels (+0.15 dB) - 59.6 dB less distortion on the
  esses, vowels unchanged; +8 ms of latency while on (reported to the host).
- Six new presets (Ess-Safe Grit, Split Warmth, Loud Accents, Lift the Details, Two-Band Grit, 3-Band Glue).
- Shared model tables (built once per process): lower CPU (2.9 - 6.5 % per style at Best, three bands + ESS GUARD 13 %).
- Verified: DSP 172, plugin 31, ASan + UBSan clean, independent audit 47/47, pluginval strictness 10 (also with the
  plugin built with AddressSanitizer: clean), VST3 validator 47/47.

## 1.0.0 - 2026-10-29 (local milestone)
- Eight saturation styles from circuit physics: TAPE (Jiles-Atherton hysteresis between record emphasis and playback
  de-emphasis, speed 7.5-30 ips), TRIODE (12AX7-type stage, grid conduction and bias shift, nine bias curves),
  PENTODE (push-pull, odd harmonics, class A to B, supply sag), CONSOLE (transformer into class A), GERMANIUM
  (starving bias), IRON (transformer core), DIODE (asymmetric clipper), FOLD (wavefolder).
- DRIVE + PUNISH (+20 dB), CHARACTER (per style), LOW CUT + THUMP and TONE before the saturation, HIGH CUT (+ STEEP)
  and AIR after it, MIX, OUTPUT, AUTO (a measured loudness table plus a slow integrated-loudness trim).
- Anti-aliasing: antiderivative anti-aliasing on every static curve plus 2x-16x oversampling (QUALITY, default Best 8x;
  latency changes go through the host handshake).
- A live view of the transfer curve (tape draws its loop) and the harmonics H2-H9.
- 18 factory presets, A/B, the preset name saved with the project.
- Verified: DSP 132, plugin 29, ASan + UBSan clean, independent audit 39/39, pluginval strictness 10, VST3 validator 47/47.
