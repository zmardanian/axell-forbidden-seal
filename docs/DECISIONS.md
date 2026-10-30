# Design decisions

Each entry: what was chosen, what it was measured against, and what was rejected.

## F1 - Our own circuit models, studied from the classics (1.0)
The class leaders were studied from their manuals and documented behaviour (drive into a modelled circuit, a tilt tone
and low cut before it, a high cut after it, a parallel path, tape speed and emphasis, multiband and creative styles);
every model here is our own, from circuit physics: Jiles-Atherton hysteresis (tape), Koren's triode and pentode
equations solved on a load line (tubes), a transformer core whose magnetising current rises with the flux (iron,
console), a class-A transistor stage with emitter degeneration (console, germanium), a diode clipper, a sine folder.
No code, data, captured responses or graphics of any product are used.

## F2 - Anti-aliasing: ADAA on every static curve + oversampling (1.0)
A 7.5 kHz tone 18 dB over full scale (the hardest case) at 4x: plain curves -24 to -41 dB of energy away from the
harmonics; first-order antiderivative anti-aliasing (Parker et al. 2016) on the tube, transistor and diode curves:
-56 to -66 dB, -72 to -125 dB at 8x, -94 to -140 dB at 16x. TAPE has memory (no ADAA): its solver now cuts a large
field step into steps of at most 0.05 (it went unstable at extreme drive and made sub-harmonics: -31 dB at 16x, now
-113 dB). Default Best (8x): realistic material (3 kHz at -12 dBFS, +18 dB) is below -80 dB in every style
(-97 to -152 dB measured).

## F3 - AUTO: a measured table plus a slow loudness trim (1.0)
A table (tools/SealCalibrate.cpp: every style x 5 CHARACTER x 26 DRIVE steps, BS.1770 on a voice-like signal) takes back
the drive and what the model adds. On a different voice it was off by up to -2.9 dB at extreme drive (a static table
cannot know the material), so a trim compares the long-term (3 s) K-weighted loudness of the latency-aligned input and
of the saturated signal (open loop, +-4 dB): the largest error on unseen material is now 0.57 dB, and the short-term
dynamics (the saturation's own compression) are left alone.

## F4 - Exactness (1.0)
MIX 0 % and host bypass give the input delayed by the reported latency, bit for bit, at every QUALITY (gains glide and
land exactly - a float glide otherwise stalls 3e-5 short); NaN / Inf never reach a model; block-size independent.

## F5 - BANDS (2.0)
Linkwitz-Riley 24 dB/oct crossovers (each two Butterworth sections): LP + HP of one crossover sum to a second-order
allpass, so with three bands the low band passes the high crossover's allpass, and the dry passes both - the wet bands
and the dry stay phase-coherent at every MIX. Measured linear: the crossovers re one band within 0.0005 dB and 0.001 deg
at 80 Hz - 5 kHz (DSP), 0.07 dB magnitude over 50 Hz - 16 kHz through a real host (audit). With BANDS off nothing of
this is in the path (MIX 0 bit-exact). Each band has its own oversampler and circuits (tables shared per process).

## F6 - DYNAMICS (2.0)
An envelope (5 ms / 80 ms) of the pre-saturation signal re -18 dBFS moves the drive (0.6 dB per dB, +-18 dB); AUTO takes
the level back per sample from its measured table, delayed by the oversampling latency so it lands on the same
samples. Measured: +100 adds 4 dB of 3rd harmonic on a loud passage, -100 adds 15 dB on a quiet one.

## F7 - ESS GUARD keeps esses out of the circuit (2.0)
First design: lower the drive on esses and take the level back from AUTO's table - the esses came out 5.3 dB quiet (the
table is a voice average; noise-like esses saturate differently) and only 5 dB less distorted. Chosen: during an ess,
each band fades from its saturated signal to its clean, time-aligned one, at the gain the saturation gives the material
around it (measured continuously on the unguarded samples, K-weighted, 3 s). With the real network on an independent
voice: ess distortion -59.6 dB re off, the esses' balance to the vowels as in the input (+0.15 dB), vowels 0.00 dB.
The decision needs the network's look-ahead (frame delay + hop: 8 ms at 48 kHz), so the whole signal is delayed by it
while ESS GUARD is on (the host is told through the latency handshake).

## F8 - The pluginval teardown crash is the host's (2.0)
pluginval (strictness 10) sometimes crashed AFTER all tests passed: SIGSEGV in its own VST3-host run loop
(juce::RunLoop::Impl fd handler map) or a heap-corruption abort at exit - the same signature as occasional CI crashes of
other Axell plugins. The plugin built with AddressSanitizer and loaded into pluginval with libasan preloaded: 8 runs
clean (virtual and real display) - no memory error anywhere in the process; the crash is a timing race in the host's
Linux run loop. While investigating, a real latent bug in the shared core was fixed: model weight arrays had internal
linkage but were referenced by inline objects (an ODR violation exposed by a sanitizer link).

## F9 - Measured against an open reference (3.0)
An open-source tape model (GPL; used only as a local benchmark, never its code) at each of its oversampling settings
against TAPE, at more saturation on our side (THD -25 dB at -12 dBFS vs its -36 dB at maximum drive): energy away from
the harmonics on a 7.5 kHz tone at -6 dBFS: -129 dB (Best) vs -74 dB (its 8x); on 3 kHz at -12 dBFS: -210 dB vs
-83 dB; CPU 6.5 % vs 10.6 % of a core. Decapitator, Saturn 2, HG-2 and Oxide were studied from their manuals and
documented behaviour (none of them is installed natively here); their feature ideas are credited in the README's
spirit, the models are ours.

## F10 - Live: minimum-phase oversampling (3.0)
The linear-phase half-bands of the core's oversampler cost 71 - 83 samples at 44.1/48 kHz. For recording through the
plugin, LIVE uses two polyphase IIR half-band stages (an elliptic half-band split into two all-pass branches,
Valenzuela & Constantinides 1983; designed here from the transition width and the stop band: 7 + 4 coefficients):
passband flat to 20 kHz, stop band -111 dB, images -119 dB, latency 3 - 4 samples (the delay at 300 Hz, rounded;
the dry is delayed by it). The phase is not linear - fine for monitoring; mixing stays on the linear-phase settings.

## F11 - INPUT + LEARN (3.0)
On the artist's dry vocal (-35.9 LUFS) the vocal presets barely touched the curve: the circuits are designed around a
voice at -18 dBFS RMS. LEARN measures 6 s of the take (BS.1770, gated, the channels' mean) and sets INPUT = -18 - L
(clamped +-24 dB). INPUT adds to the drive into every band; AUTO takes it back with the drive, so the output level is
unchanged; DYNAMICS' reference follows it. Tested: a take at -38.3 LUFS -> INPUT +20.3 dB (exact).

## F12 - The vocal presets, tuned on a real voice (3.0)
At the designed level the first drafts were far too heavy (UTMOS 2.04 dry -> 1.34 - 1.77, the fundamental 5 dB down
against the mids). Each vocal preset was searched over DRIVE x MIX (and the low cuts lowered to 80 Hz) for the most
colour that keeps UTMOS >= the dry take's - 0.03, the fundamental within 2 dB and the esses' level within their word no
higher (+0.3 dB): Tube Grit and Ess-Safe Grit +2 dB / 70 %, Aggressive Lead +6 dB, Split Warmth +4 dB, Loud Accents
+4 dB, Lift the Details +6 dB. Without ESS GUARD the grit preset raised the esses 1.45 dB against their words, with it
0.15 dB.

## F13 - AUTO reads its table at DRIVE, INPUT is a plain gain (3.0.1)
3.0.0 indexed AUTO's table with INPUT + DRIVE. The table is measured as "drive above the designed level"; INPUT's job
is to bring the take to that level, so the circuit sees DRIVE above it - with LEARN's +17.9 dB on the artist's take
the table assumed a circuit driven 18 dB harder and over-compensated (~5 dB too loud). The suites ran with INPUT 0 and
missed it; both now test a take 20 dB quiet with INPUT +20 dB (DSP 0.1 dB, audit 0.16 dB). The vocal presets were
retuned at the level LEARN actually sets (3.0.0 tuned them 3 dB lower: summed-channel loudness instead of the
channels' mean): Tube Grit +2 / 50 %, Ess-Safe Grit 0 / 70 %, Warm Tape +6, Console Glue +6, Aggressive Lead +4, Split
Warmth +6 / 50 %, Loud Accents +1, Lift the Details +6 - UTMOS 2.020 - 2.064 vs the dry take's 2.043.

## F14 - 4.0: decoupled saturation, measured against the reference at EQUAL SQUASH
3.0.1 failed the artist's ear on their own vocal: "boxy", harsh, glitchy. Measured on that vocal at the level LEARN sets,
against a respected commercial tape-machine emulation (rendered through the artist's own licence; its measurements guide
the design, nothing of it is used): at the same amount of saturation both squash the low mids about equally - the
difference was tone. The reference's own linear response is a gentle smile (+0.6..1.5 dB at 60 Hz, -0.3..1 dB at 500 Hz -
2 kHz, +1.1..1.8 dB at 8 - 16 kHz); several of our circuits' internal filters cut 8 - 16 kHz by 0.9 - 1.3 dB and the vocal
presets added high cuts: a dull top over a full middle - heard as a box.
- The engine: each circuit gives only its colour = output - its small-signal response (ADAA's linear response is the
  two-sample mean; the transformers have an exact linear twin); the voice never passes a circuit.
- The measures (research/colour.py; research/bussgang.py is the split, as the engine runs it): per band, the linear part vs the residual (coherence); box = the low-mid tilt hump;
  silk = 8 - 16 kHz tilt; fizz, honk, weight, bite = where the residual sits; articulation = 2 - 6 kHz envelope range;
  squash = the 250 - 1000 Hz envelope range; level wobble.
- The comparison axis matters: at equal *colour* the reference looked better, because most of its "colour" is
  compression; at equal *squash* (the low-mid dynamics) the comparison is fair.
- A first click detector reported 109 bursts on ESS GUARD - made fair (each processor's linear part removed first), it
  reversed; it was retracted. Glitches are tested by stationarity instead (F17).

## F15 - squash and harmonics apart (Bussgang), FOCUS per circuit and per drive
FOCUS first shaped the whole colour - including the circuit's squash, which then squashed the presence and the lows but
not the mids: a new box (+1.3 dB). Bussgang (1952): for a memoryless circuit the colour = a gain of the input (one for all
frequencies) + uncorrelated harmonics. The engine estimates the gain as the local least-squares projection of the
colour on the band's signal (two one-pole stages, 2.5 ms each), applies 30 % of it evenly (kFocusSquash), and shapes only
the harmonics. The shapes (Source/FocusShapes.h) were optimised per circuit on the artist's vocal against the reference at
both speeds at equal squash - box, fizz, honk below the better speed's, weight and bite above, air between the duller
speed's and +2.5 dB, no mud (weight at most 2 dB past the reference) - with the weight voiced per DRIVE (the reference's
low end moves with its input in its own way). Lessons: optimise on the shipping engine's own colour, with the engine's
exact estimator and the exact squash of the full output (a proxy squash biased every comparison by 0.1 - 0.3 dB).

## F16 - presets
Vocal presets carry no filters (LOW CUT shapes what drives the circuit and thins the voice's own lows in 4.0; HIGH CUT
dulls it); each one's DRIVE is solved on the artist's vocal (INPUT at LEARN's +17.9 dB) for its squash target. The effect
presets (ad-libs, 808, drums, buses, master, lo-fi, Forbidden) keep the classic circuit (FOCUS 0, VOICING off).

## F17 - ESS GUARD 4.0, and glitches by stationarity
The colour fades over an ess (1 ms in, 40 ms out, after FOCUS: none of its filters' ring-out either); the audio waits 4 ms
more than the network's decision so the guard is complete before the ess arrives (latency 12 ms while on); a guarded ess
gets the vowels' loudness change (the table's estimate) so the balance holds with AUTO on or off. Guarded esses: 50 - 54 dB
less distortion than the input. Stationarity tests: a periodic vowel must give a periodic output (frames of whole
periods: 0.0006 dB), every circuit, AUTO, DYNAMICS, ESS GUARD, LIVE; forced guard switching every 50 ms adds no highs.

## F18 - the interface (4.0)
The artist asked for no knobs, a smaller face (660 x 350 at 100 %), better composition, the Japanese and seal elements
kept, "a touch of ancient". DRIVE = a katana (the hero control); every other continuous control = a brush ensō that
closes with its value; switches = hanko seals (vermilion when on); the sections named after Musashi's five rings with a
traditional palette; his painting seal 二天 (Niten) stamped over 禁. All drawn in code (Source/MusashiArt.h,
Source/SealControls.h); the brush font subset gained 二天一流地水火風空輪 (the 209 existing glyphs byte-identical).

## F19 - the final gate: perceived quality, then character
Optimising placement alone pushed the harmonics' level up (to 3x) to out-do the reference's weight and bite - and the
predicted listener score (UTMOS, which matched the artist's ear before) then fell 0.07 - 0.37 below the reference's at
equal squash: more grit, heard as worse. 4.0 ships with each circuit's harmonics at the level where its UTMOS meets the
reference's at equal squash (src: harm_scale per circuit, 0.37 - 0.7 of the optimum's), checked on the real plugin at
DRIVE 8 and 12 for every circuit (12 of 16 within 0.02, all within 0.03; DNSMOS 13 of 16). VOICING stays on: off gains
UTMOS 0.03 but loses DNSMOS (7 of 16) and the box and air advantage. The cost, stated: at equal squash 4.0 is cleaner than
the reference (weight above it at 2 of 64 points, bite at 28). Also found here: UTMOS does not hear a box or a dull top
(3.0.1's Warm Tape scores above the dry take) - it guards against harshness, it does not judge tone.
AUTO 4.0: the table's estimate times a correction measured on the unguarded parts only (a guarded ess never steers the
vowels' level); a guarded ess gets the vowels' net change exactly. Presets: DRIVE solved per preset for its squash on the
artist's vocal; PENTODE's squash peaks at -1.37 dB (DRIVE 16) - Aggressive Lead sits there.
