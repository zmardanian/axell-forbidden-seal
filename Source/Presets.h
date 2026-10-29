#pragma once
// Axell Forbidden Seal - factory presets (no plugin, product or trademark names). The "Vocal:" presets were tuned on
// the artist's dry vocal at the designed level (-18 dBFS RMS: press LEARN first): the most colour that keeps the
// predicted listener score (UTMOS) at the dry take's, the fundamental within 2 dB of it and the esses no harsher
#include "Engine.h"

#include <array>
#include <vector>

namespace afs
{
struct Preset
{
    const char* name;
    const char* description;
    Params p;
};

namespace preset_detail
{
inline Params make (int style, float drive, float character, float mix = 100.0f)
{
    Params p;
    p.style = style;
    p.driveDb = drive;
    p.character = character;
    p.mix = mix;
    return p;
}
inline Params filters (Params p, float lowCut, float highCut, bool steep = false, bool thump = false, float tone = 0.0f, float air = 0.0f)
{
    p.lowCutHz = lowCut;
    p.highCutHz = highCut;
    p.steep = steep;
    p.thump = thump;
    p.tone = tone;
    p.airDb = air;
    return p;
}
inline Params punish (Params p) { p.punish = true; return p; }
inline Params guard (Params p, float g) { p.essGuard = g; return p; }
inline Params dyn (Params p, float d) { p.dynamics = d; return p; }
// bands: count, crossovers, then per band: style (-1 = main), drive trim, mix
inline Params split (Params p, int n, float xl, float xh, std::array<int, 3> st, std::array<float, 3> dr, std::array<float, 3> mx)
{
    p.bands = n;
    p.xLow = xl;
    p.xHigh = xh;
    p.bandStyle = st;
    p.bandDrive = dr;
    p.bandMix = mx;
    return p;
}
} // namespace preset_detail

inline const std::vector<Preset>& factoryPresets()
{
    using namespace preset_detail;
    static const std::vector<Preset> list {
        { "Init", "A triode stage, gently driven.", Params {} },
        { "Vocal: Warm Tape", "Tape at a slow speed: thicker lows, rounded highs, esses tucked in.",
          filters (make (kTape, 10.0f, 30.0f), 70.0f, 18000.0f) },
        { "Vocal: Tube Grit", "A cold triode pushed for grit and presence; the lows kept clean.",
          filters (make (kTriode, 2.0f, 25.0f, 70.0f), 80.0f, 13000.0f, false, false, 15.0f) },
        { "Vocal: Console Glue", "A console channel driven into its transformer: weight and density.",
          filters (make (kConsole, 9.0f, 45.0f), 60.0f, 20000.0f) },
        { "Vocal: Aggressive Lead", "Push-pull pentode: odd harmonics that cut through a dense beat.",
          filters (make (kPentode, 6.0f, 40.0f, 100.0f), 80.0f, 11000.0f, false, false, 10.0f) },
        { "Ad-libs: Telephone Crunch", "A starved germanium stage through a narrow band.",
          filters (make (kGermanium, 22.0f, 55.0f), 450.0f, 3800.0f, true, true) },
        { "Ad-libs: Folded FX", "A wavefolder blended in for a metallic, ring-like edge.",
          filters (make (kFold, 14.0f, 65.0f, 40.0f), 150.0f, 14000.0f) },
        { "808: Weight", "A small iron core: the sub saturates into audible harmonics on small speakers.",
          make (kIron, 18.0f, 70.0f) },
        { "808: Distorted", "An asymmetric diode clipper for a gritty, present 808.",
          filters (make (kDiode, 22.0f, 65.0f), 20.0f, 9000.0f) },
        { "Drums: Smash", "Punished pentode in parallel: transients kept, the room exploded.",
          filters (punish (make (kPentode, 6.0f, 60.0f, 35.0f)), 40.0f, 14000.0f) },
        { "Drums: Tape Bus", "Fast tape on the drum bus: glue and a softer top.",
          make (kTape, 12.0f, 75.0f) },
        { "Snare: Crack", "Germanium for a cracking, forward snare.",
          filters (make (kGermanium, 16.0f, 30.0f, 70.0f), 150.0f, 16000.0f) },
        { "Beat Bus: Analog Glue", "A console stage barely pushed, mostly for density.",
          filters (make (kConsole, 6.0f, 40.0f, 80.0f), 20.0f, 20000.0f) },
        { "Master: Tape Kiss", "Fast tape, lightly driven: polish without a change in tone.",
          make (kTape, 4.0f, 80.0f) },
        { "Master: Tube Sheen", "A hot-biased triode in parallel with a touch of air.",
          filters (make (kTriode, 4.0f, 75.0f, 60.0f), 20.0f, 20000.0f, false, false, 0.0f, 1.5f) },
        { "Bass: Germanium Fuzz", "A starving germanium stage: fuzz that sputters on the tails.",
          filters (make (kGermanium, 26.0f, 85.0f), 40.0f, 7000.0f) },
        { "Lo-Fi: AM Radio", "A diode clipper through a narrow, steep band.",
          filters (make (kDiode, 18.0f, 50.0f), 400.0f, 3000.0f, true, false, 20.0f) },
        // 2.0
        { "Vocal: Ess-Safe Grit", "A pushed triode with ESS GUARD: all the grit, none of the fizz on the esses.",
          guard (filters (make (kTriode, 2.0f, 30.0f, 70.0f), 80.0f, 16000.0f), 100.0f) },
        { "Vocal: Split Warmth", "Three bands: tape on the lows, a triode on the mids, the highs barely touched; esses guarded.",
          guard (split (make (kTriode, 4.0f, 40.0f), 3, 250.0f, 4000.0f, { kTape, -1, kConsole }, { 2.0f, 0.0f, -8.0f }, { 100.0f, 100.0f, 40.0f }), 80.0f) },
        { "Vocal: Loud Accents", "DYNAMICS on: the loud words bite harder, the soft ones stay smooth.",
          dyn (filters (make (kPentode, 4.0f, 45.0f, 100.0f), 80.0f, 14000.0f), 70.0f) },
        { "Vocal: Lift the Details", "DYNAMICS reversed: the quiet parts are thickened, the peaks spared.",
          dyn (make (kConsole, 6.0f, 50.0f), -70.0f) },
        { "808 + Top: Two-Band Grit", "The sub stays round (iron), everything above it gets the diode.",
          split (make (kDiode, 16.0f, 60.0f), 2, 120.0f, 3000.0f, { kIron, -1, -1 }, { -6.0f, 4.0f, 0.0f }, { 100.0f, 100.0f, 100.0f }) },
        { "Mix Bus: 3-Band Glue", "Gentle tape on the lows, console on the mids, tape kiss on the top.",
          split (make (kConsole, 5.0f, 45.0f, 80.0f), 3, 180.0f, 5000.0f, { kTape, -1, kTape }, { 1.0f, 0.0f, -3.0f }, { 100.0f, 100.0f, 70.0f }) },
        { "Forbidden", "The seal broken: a punished wavefolder. Use with care.",
          filters (punish (make (kFold, 10.0f, 70.0f, 70.0f)), 80.0f, 12000.0f) },
    };
    return list;
}
} // namespace afs
