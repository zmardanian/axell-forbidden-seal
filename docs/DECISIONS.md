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
