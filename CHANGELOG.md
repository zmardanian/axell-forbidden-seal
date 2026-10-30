# Changelog

## 4.0.0 - 2026-10-30
A new engine and a new face, after the artist heard 3.0.1 on their own vocal and judged it boxy, harsh and glitchy -
and measurement confirmed why.

**Decoupled saturation (a new engine).** Every circuit now gives only what it *adds* (its output minus its own
small-signal response); the voice itself never passes a circuit, so its highs and mids stay exactly as recorded.
- What it adds is split, in real time, into **squash** (the part that is a gain of the voice - Bussgang: for a memoryless
  circuit one gain for every frequency) and **new harmonics**. The squash is applied evenly over the voice (its tone is
  kept); the harmonics are placed on purpose.
- **FOCUS** (new): where the harmonics land - out of the vowels' 1 - 1.5 kHz (the box) and softened above 7 kHz (the
  fizz), the weight voiced per circuit and per drive, a measured share of the squash. FOCUS 0 = the classic circuit.
- **VOICING** (new): the seal's tone - a touch of weight, the honk range eased, a breath of air. Off: the voice exactly as it
  came in.
- Every FOCUS shape was found on the artist's own vocal against a respected commercial tape-machine emulation at both its
  speeds, compared at **equal squash**: box below the reference's at 61 of 64 points, air within its window at 61, fizz below at 56, honk below at 56, no mud at 63; the predicted listener score (UTMOS) matches the reference's at every tested point (within 0.03). The harmonics' level is set exactly there: at equal squash 4.0 adds less low and bite grit than the reference (weight above it at 2 of 64 points, bite at 28) - more grit is a DRIVE or FOCUS turn away.
- Against 3.0.1 at equal squash, on the artist's vocal: less box, more air, less fizz, less honk at every circuit;
  less grit per squash (cleaner), and the level moves a little more (the squash now spans the whole voice evenly).
- **Saved 3.x sessions sound different on purpose** (the new engine; FOCUS and VOICING take their defaults): FOCUS 0 with
  VOICING off is the closest to the classic circuit.
- The circuits' internal filters (gap loss, output filters) shaped the voice in 3.x and dulled the top by up to 1.3 dB:
  they now shape only the colour.

**ESS GUARD.** The colour fades out over each ess instead of a 1 ms swap between two different signals; the guard
starts 4 ms before the ess (latency 12 ms while on); a guarded ess keeps exactly the vowels' loudness change. Guarded esses
carry 50 - 55 dB less distortion than the input with forced decisions (3.x: 26 - 42); with the real network on an
independent voice, -137 dB of residue; AUTO now measures the vowels alone (they keep the input's loudness within 0.08 dB).

**Glitches.** New tests: a steady vowel gives a steady output (every circuit, AUTO, DYNAMICS, ESS GUARD, LIVE:
0.0006 dB in frames of whole periods); ESS GUARD switching every 50 ms adds nothing to the highs.

**Presets.** The vocal presets are pure (no filters on the voice), each one's DRIVE solved on the artist's vocal for its
amount of squash; the effect presets keep the classic circuit.

**Interface.** Smaller (660 x 350 at 100 %) and new: DRIVE is a katana drawn out of its scabbard - the blade heats with
the drive and its temper line ripples with HEAT, DYNAMICS moves on its back; every other control is a brush ensō that
closes with its value; switches are hanko seals; Miyamoto Musashi's painting seal 二天 (Niten) stamped over 禁, glowing with
HEAT; the sections named after his five rings (地 水 火 風 空); washi paper, ink mountains. All drawn in code.

- Verified: DSP 220, plugin 32, ASan + UBSan clean, independent audit 63/63, core 143, pluginval strictness 10,
  VST3 validator 47/47.

## 3.0.1 - 2026-10-29
- **Fixed: AUTO with INPUT.** 3.0.0 read AUTO's table at INPUT + DRIVE; the table means "drive above the designed
  level", and INPUT puts the take at that level - so with LEARN's INPUT the output came out several dB too loud
  (about 5 dB on a quiet take). Now the table is read at DRIVE and INPUT is taken back as a plain gain: a take 20 dB
  quiet with INPUT +20 dB keeps its own loudness within 0.2 dB in every style (new tests, DSP and audit).
- **The vocal presets retuned at the level LEARN sets** (3.0.0's were tuned 3 dB lower), all eight: on the artist's
  dry vocal every one keeps the predicted listener score at the dry take's (2.020 - 2.064 vs 2.043), the fundamental
  within 2 dB, the esses no harsher.
- Verified: DSP 184, plugin 32, ASan + UBSan clean, independent audit 55/55, pluginval strictness 10, VST3 validator 47/47.

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
