"""Independent black-box audit of the built Axell Forbidden Seal VST3 through a real VST3 host (SealAuditHost, JUCE's VST3
hosting code), with test signals and analysis written here from textbook definitions (numpy/scipy) - nothing uses the
plugin's own code. Exit code = number of failures. Environment: AXELL_AUDIT_HOST, AXELL_VST3, AXELL_AUDIT_WORK."""
import os, sys, time
import numpy as np
from scipy import signal as sg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
os.environ.setdefault("AXELL_AUDIT_HOST", os.path.join(ROOT, "build/SealAuditHost_artefacts/Release/SealAuditHost"))
os.environ.setdefault("AXELL_VST3", os.path.join(ROOT, "build/AxellForbiddenSeal_artefacts/Release/VST3/Axell Forbidden Seal.vst3"))
os.environ.setdefault("AXELL_AUDIT_WORK", os.path.expanduser("~/.cache/axell-seal-audit"))
_vendored = os.path.join(ROOT, "external", "axell-core")
CORE = os.environ.get("AXELL_CORE", _vendored if os.path.isdir(_vendored) else os.path.join(os.path.dirname(ROOT), "axell-core"))
sys.path.insert(0, os.path.join(CORE, "tools"))
import lib  # noqa: E402
from lib import Run  # noqa: E402

START = time.time()
os.makedirs(os.environ["AXELL_AUDIT_WORK"], exist_ok=True)
STYLES = ["Tape", "Triode", "Pentode", "Console", "Germanium", "Iron", "Diode", "Fold"]
QUAL = ["Eco (2x)", "Good (4x)", "Best (8x)", "Extreme (16x)", "Live (4x)"]

def run_all(runs, inputs, **kw):
    return lib.run_all(runs, {k: np.asarray(v, np.float32) for k, v in inputs.items()}, **kw)

def f32(x):
    return np.asarray(x, np.float32).astype(np.float64)

results = []
def report(name, ok, detail):
    results.append((name, ok, detail))
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}", flush=True)

def db(a):
    return 20 * np.log10(max(float(a), 1e-30))

def aligned(y, lat, n):
    """the host output with the plugin's reported latency removed"""
    return y[lat:lat + n]

def sine(fs, f, dbfs, secs):
    t = np.arange(int(secs * fs)) / fs
    x = 10 ** (dbfs / 20) * np.sin(2 * np.pi * f * t)
    return f32(np.stack([x, x], 1))

def bh7(n):
    a = [0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803, 0.01081174209837, 0.00077658482522, 0.00001388721735]
    k = np.arange(n) / (n - 1)
    return sum(((-1) ** j) * a[j] * np.cos(2 * np.pi * j * k) for j in range(7))

def spectrum(y, fs):
    y = y[len(y) // 3:]
    w = bh7(len(y))
    return np.fft.rfftfreq(len(y), 1 / fs), np.abs(np.fft.rfft(y * w)) / (w.sum() / 2)

def harm(y, fs, f0, k):
    f, Y = spectrum(y, fs)
    i = int(np.argmin(np.abs(f - k * f0)))
    return Y[max(0, i - 4):i + 5].max()

def stray_db(y, fs, f0):
    f, Y = spectrum(y, fs)
    P = Y ** 2; bw = f[1] - f[0]
    near = np.abs(f / f0 - np.round(f / f0)) * f0
    fund = P[np.abs(f - f0) < 10 * bw].sum()
    mask = (f > 20) & (f < 20000) & (near > 10 * bw)
    return 10 * np.log10(P[mask].sum() / fund + 1e-30)

def voice(fs, secs, seed=11):
    """independent voice-like signal: glottal pulses (f0 110-170 Hz) through three formants, with sibilant noise
    bursts and pauses, -18 dBFS RMS"""
    rng = np.random.default_rng(seed)
    n = int(secs * fs); t = np.arange(n) / fs
    ph = np.cumsum((140 + 30 * np.sin(2 * np.pi * 0.37 * t)) / fs)
    src = sg.lfilter([1], [1, -0.95], (np.diff(np.floor(ph), prepend=0) > 0).astype(float))
    v = np.zeros(n)
    for fc, bw, g in ((700, 120, 1.0), (1200, 150, 0.6), (2600, 250, 0.25)):
        r = np.exp(-np.pi * bw / fs); w = 2 * np.pi * fc / fs
        v += g * sg.lfilter([1 - r], [1, -2 * r * np.cos(w), r * r], src)
    seg = (t % 0.45)
    env = np.clip(np.minimum(seg, 0.3 - seg) / 0.02, 0, 1) * (seg < 0.3)
    r = np.exp(-np.pi * 1500 / fs); w = 2 * np.pi * 7000 / fs
    s = sg.lfilter([1 - r], [1, -2 * r * np.cos(w), r * r], rng.standard_normal(n))
    senv = np.sin(np.pi * np.clip((seg - 0.32) / 0.1, 0, 1)) * ((seg > 0.32) & (seg < 0.42))
    x = v * env + 3 * s * senv
    x *= 10 ** (-18 / 20) / np.sqrt(np.mean(x ** 2))
    return f32(np.stack([x, x], 1))

def kweighted_db(x, fs):
    """ITU-R BS.1770 K-weighting (the two filters from the standard's analog prototypes), mean square in dB"""
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / fs); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416; a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]; a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / fs); a0 = 1 + K / Q + K * K
    b2 = [1, -2, 1]; a2 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    y = sg.lfilter(b2, a2, sg.lfilter(b1, a1, x))
    return 10 * np.log10(np.mean(y[int(0.25 * fs):] ** 2) + 1e-30)

# ---------------------------------------------------------------- 1. silence, MIX 0, bypass, latency per QUALITY
for fs in (44100, 48000, 96000):
    x = voice(fs, 2.0)
    runs = [Run(f"sil{s}{fs}", fs, sets={"Style": st, "Drive": "24", "Punish": "On"}) for s, st in enumerate(STYLES)]
    ins = {r.name: np.zeros((int(fs), 2)) for r in runs}
    runs += [Run(f"m0{q}{fs}", fs, sets={"Mix": "0", "Drive": "20", "Quality": QUAL[q]}) for q in range(5)]
    runs += [Run(f"byp{fs}", fs, sets={"Bypass": "On", "Drive": "20"})]
    for q in range(5):
        ins[f"m0{q}{fs}"] = x
    ins[f"byp{fs}"] = x
    out = run_all(runs, ins, jobname=f"basic{fs}")
    loud = max(np.abs(out[f"sil{s}{fs}"]).max() for s in range(len(STYLES)))
    report(f"silence in -> silence out, every style at +44 dB drive @ {fs}", loud == 0.0, f"peak {loud:.3g}")
    lats = []
    for q in range(5):
        r = next(r for r in runs if r.name == f"m0{q}{fs}")
        lats.append(r.latency)
        y = out[r.name]
        e = np.abs(y[r.latency:] - x[:len(y) - r.latency]).max()
        report(f"MIX 0 %, {QUAL[q]}: output = input delayed by the reported latency ({r.latency}) @ {fs}", e == 0.0, f"max error {e:.3g}")
    report(f"latency grows with QUALITY (Eco .. Extreme) @ {fs}", lats[:4] == sorted(lats[:4]) and lats[0] > 0, f"{lats[:4]} samples")
    report(f"LIVE: latency of a few samples @ {fs}", 0 < lats[4] <= 8, f"{lats[4]} samples")
    rb = next(r for r in runs if r.name == f"byp{fs}")
    e = np.abs(out[rb.name][rb.latency:] - x[:len(x) - rb.latency]).max()
    report(f"host bypass: output = input delayed by the latency @ {fs}", e == 0.0, f"max error {e:.3g}")

fs = 48000
x = voice(fs, 4.0)
P = {"Style": "Pentode", "Drive": "16", "Low Cut": "120", "Thump": "On", "Tone": "30", "High Cut": "9000", "Steep": "On", "Air": "3", "Mix": "70"}
runs = [Run("b512", fs, sets=P), Run("brand", fs, sets=P, blocks="random 1 4096 7"), Run("mono", fs, ch=1, sets=P, blocks="700")]
out = run_all(runs, {"b512": x, "brand": x, "mono": x[:, :1]}, jobname="blocks")
e = np.abs(out["b512"] - out["brand"]).max()
report("blocks of 512 vs random 1-4096 samples: the same output (every filter on)", e < 1e-6, f"max difference {e:.3g}")
report("mono track (1 in / 1 out), 700-sample blocks: finite, the effect present", np.isfinite(out["mono"]).all() and np.abs(out["mono"]).max() > 1e-3,
       f"peak {np.abs(out['mono']).max():.3f}")

# ---------------------------------------------------------------- 2. small-signal gain, harmonic signatures
for q in range(5):
    runs = [Run(f"lin{s}", fs, sets={"Style": st, "Drive": "0", "Auto Gain": "Off", "Quality": QUAL[q]}) for s, st in enumerate(STYLES)]
    x = sine(fs, 1000, -50, 0.6)
    out = run_all(runs, {r.name: x for r in runs}, jobname=f"lin{q}")
    worst = max(abs(db(harm(aligned(out[r.name], r.latency, len(x))[:, 0], fs, 1000, 1) / 10 ** (-50 / 20))) for r in runs)
    report(f"every style, {QUAL[q]}: small-signal gain at 1 kHz (largest deviation)", worst < 0.2, f"{worst:.3f} dB")
x = sine(fs, 1000, -18, 0.6)
runs = [Run(f"h{s}", fs, sets={"Style": st, "Drive": "24", "Auto Gain": "Off"}) for s, st in enumerate(STYLES)]
runs += [Run(f"e{s}", fs, sets={"Style": st, "Drive": "6", "Auto Gain": "Off"}) for s, st in enumerate(STYLES)]
out = run_all(runs, {r.name: x for r in runs}, jobname="harm")
for s in (0, 2, 5, 7):
    r = runs[s]; y = aligned(out[r.name], r.latency, len(x))[:, 0]
    h2 = db(harm(y, fs, 1000, 2) / harm(y, fs, 1000, 1))
    report(f"{STYLES[s]} (symmetric circuit): 2nd harmonic at +24 dB re the fundamental", h2 < -80, f"{h2:.1f} dB")
for s in (1, 3, 4):
    r = runs[8 + s]; y = aligned(out[r.name], r.latency, len(x))[:, 0]
    d = db(harm(y, fs, 1000, 2) / harm(y, fs, 1000, 3))
    report(f"{STYLES[s]} (single-ended): 2nd re 3rd harmonic at +6 dB (even-rich)", d > 3, f"{d:+.1f} dB")

# ---------------------------------------------------------------- 3. aliasing at Best and Extreme
x3 = sine(fs, 3000, -12, 0.8); x7 = sine(fs, 7500, -6, 0.8)
runs = [Run(f"a{s}", fs, sets={"Style": st, "Drive": "18", "Auto Gain": "Off", "Quality": "Best (8x)"}) for s, st in enumerate(STYLES)]
runs += [Run(f"x{s}", fs, sets={"Style": st, "Drive": "24", "Auto Gain": "Off", "Quality": "Extreme (16x)"}) for s, st in enumerate(STYLES)]
out = run_all(runs, {r.name: (x3 if r.name[0] == "a" else x7) for r in runs}, jobname="alias")
worst = max(stray_db(aligned(out[r.name], r.latency, len(x3))[:, 0], fs, 3000) for r in runs[:8])
report("every style, Best (8x): energy away from the harmonics (aliasing), 3 kHz at -12 dBFS +18 dB (worst style)", worst < -80, f"{worst:.1f} dB")
worst = max(stray_db(aligned(out[r.name], r.latency, len(x7))[:, 0], fs, 7500) for r in runs[8:])
report("every style, Extreme (16x): the same, 7.5 kHz at -6 dBFS +24 dB (worst style)", worst < -90, f"{worst:.1f} dB")

# ---------------------------------------------------------------- 4. AUTO keeps the loudness
x = voice(fs, 4.0); l0 = kweighted_db(x[:, 0], fs)
runs = [Run(f"g{s}d{d}", fs, sets={"Style": st, "Drive": str(d), "Auto Gain": "On"}) for s, st in enumerate(STYLES) for d in (0, 15, 30)]
out = run_all(runs, {r.name: x for r in runs}, jobname="auto")
dev = [(kweighted_db(aligned(out[r.name], r.latency, len(x))[:, 0], fs) - l0, r.name) for r in runs]
w = max(dev, key=lambda t: abs(t[0]))
report("AUTO: loudness (BS.1770 K-weighted) re the input, every style, DRIVE 0 / 15 / 30 dB (largest)", abs(w[0]) < 0.75, f"{w[0]:+.2f} dB ({w[1]})")
# a quiet take with INPUT raised (what LEARN does): AUTO keeps the take's own loudness
xq = x * 0.1; lq = kweighted_db(xq[:, 0], fs)
runs = [Run(f"q{s}", fs, sets={"Style": st, "Drive": "6", "Auto Gain": "On", "Input": "20"}) for s, st in enumerate(STYLES)]
out = run_all(runs, {r.name: xq for r in runs}, jobname="autoinput")
dev = [(kweighted_db(aligned(out[r.name], r.latency, len(xq))[:, 0], fs) - lq, r.name) for r in runs]
w = max(dev, key=lambda t: abs(t[0]))
report("AUTO with INPUT +20 dB on a take 20 dB quieter: loudness re the take, every style (largest)", abs(w[0]) < 0.75, f"{w[0]:+.2f} dB ({w[1]})")

# ---------------------------------------------------------------- 5. every factory preset: finite, bounded
names = ["Init", "Vocal: Warm Tape", "Vocal: Tube Grit", "Vocal: Console Glue", "Vocal: Aggressive Lead", "Ad-libs: Telephone Crunch",
         "Ad-libs: Folded FX", "808: Weight", "808: Distorted", "Drums: Smash", "Drums: Tape Bus", "Snare: Crack", "Beat Bus: Analog Glue",
         "Master: Tape Kiss", "Master: Tube Sheen", "Bass: Germanium Fuzz", "Lo-Fi: AM Radio", "Vocal: Ess-Safe Grit", "Vocal: Split Warmth",
         "Vocal: Loud Accents", "Vocal: Lift the Details", "808 + Top: Two-Band Grit", "Mix Bus: 3-Band Glue", "Forbidden"]
x = voice(fs, 3.0)
runs = [Run(f"p{i}", fs, sets={"Program": nm}) for i, nm in enumerate(names)]
out = run_all(runs, {r.name: x for r in runs}, jobname="presets")
bad = [nm for r, nm in zip(runs, names) if not np.isfinite(out[r.name]).all() or np.abs(out[r.name]).max() > np.abs(x).max() * 4.0]
report(f"all {len(names)} factory presets: finite, peak at most +12 dB over the input's", not bad, ", ".join(bad) if bad else "ok")
same = [nm for r, nm in zip(runs[1:], names[1:]) if np.abs(out[r.name] - out["p0"]).max() < 1e-4]
report("every preset sounds different from Init", not same, ", ".join(same) if same else "ok")

# ---------------------------------------------------------------- 6. 2.0: BANDS, ESS GUARD (the real network), DYNAMICS
fs = 48000
rng = np.random.default_rng(3)
noise = f32(np.stack([rng.standard_normal(int(3 * fs)) * 10 ** (-60 / 20)] * 2, 1))   # tiny: every circuit linear
runs = [Run(f"bands{n}", fs, sets={"Bands": nm, "Drive": "0", "Auto Gain": "Off", "Crossover Low": "200", "Crossover High": "3000"})
        for n, nm in ((1, "One"), (2, "Two"), (3, "Three"))]
out = run_all(runs, {r.name: noise for r in runs}, jobname="bands")
def mag(y, x):
    f, pxy = sg.csd(x, y, fs=fs, nperseg=8192); _, pxx = sg.welch(x, fs=fs, nperseg=8192)
    return f, np.abs(pxy / pxx)
ref = aligned(out["bands1"], runs[0].latency, len(noise))[:, 0]
for k, n in ((1, 2), (2, 3)):
    f, h = mag(aligned(out[f"bands{n}"], runs[k].latency, len(noise))[:, 0], noise[:, 0])
    _, h1 = mag(ref, noise[:, 0])
    band = (f > 50) & (f < 16000)
    dev = np.max(np.abs(20 * np.log10(h[band] / h1[band])))
    report(f"{n} bands, linear: magnitude response re one band, 50 Hz - 16 kHz (largest deviation)", dev < 0.1, f"{dev:.3f} dB")

def ess_voice(fs, secs, seed=11):
    """vowels (glottal pulses through three formants) alternating with sibilants (noise through a 7 kHz or 3.5 kHz
    resonance), 120 ms raised-sine envelopes, pauses; returns the signal and the vowel / sibilant centre ranges"""
    rng = np.random.default_rng(seed)
    n = int(secs * fs); t = np.arange(n) / fs
    ph = np.cumsum((140 + 30 * np.sin(2 * np.pi * 0.37 * t)) / fs)
    src = sg.lfilter([1], [1, -0.95], (np.diff(np.floor(ph), prepend=0) > 0).astype(float))
    vow = np.zeros(n)
    for fc, bw, g in ((700, 120, 1.0), (1200, 150, 0.6), (2600, 250, 0.25)):
        r = np.exp(-np.pi * bw / fs); w = 2 * np.pi * fc / fs
        vow += g * sg.lfilter([1 - r], [1, -2 * r * np.cos(w), r * r], src)
    nz = rng.standard_normal(n)
    def res(fc, bw):
        r = np.exp(-np.pi * bw / fs); w = 2 * np.pi * fc / fs
        return sg.lfilter([1 - r], [1, -2 * r * np.cos(w), r * r], nz)
    s_hi, s_lo = res(7000, 1500), res(3500, 1200)
    x = np.zeros(n); vowels, esses = [], []
    for k in range(int(secs / 0.45)):
        a = int(k * 0.45 * fs)
        if k % 5 == 4:
            continue
        v0, v1 = a, a + int(0.30 * fs)
        env = np.minimum(1, np.minimum(np.arange(v1 - v0), np.arange(v1 - v0)[::-1]) / (0.02 * fs))
        x[v0:v1] += vow[v0:v1] * env
        s0, s1 = v1, v1 + int(0.12 * fs)
        if s1 > n:
            break
        e = np.sin(np.pi * np.arange(s1 - s0) / (s1 - s0))
        x[s0:s1] += (s_hi if k % 2 == 0 else s_lo)[s0:s1] * e * (6.0 if k % 2 == 0 else 4.0)
        vowels.append((v0 + int(0.06 * fs), v1 - int(0.04 * fs))); esses.append((s0 + int(0.03 * fs), s1 - int(0.03 * fs)))
    x = x / np.max(np.abs(x)) * 10 ** (-12 / 20)
    return f32(np.stack([x, x], 1)), vowels, esses
x, vowels, esses = ess_voice(fs, 9.0)
base = {"Style": "Triode", "Drive": "20", "Auto Gain": "On"}
runs = [Run("g0", fs, sets=base), Run("g1", fs, sets={**base, "Ess Guard": "100"})]
out = run_all(runs, {"g0": x, "g1": x}, jobname="guard")
report("ESS GUARD on: latency + the ess look-ahead", runs[1].latency - runs[0].latency > 300, f"{runs[0].latency} -> {runs[1].latency} samples")
y0 = aligned(out["g0"], runs[0].latency, len(x))[:, 0]; y1 = aligned(out["g1"], runs[1].latency, len(x))[:, 0]
skip = int(1.5 * fs)
def residual(y, segs):
    num = den = cross = 0.0
    for a, b in segs:
        if a < skip: continue
        cross += np.dot(y[a:b], x[a:b, 0]); den += np.dot(x[a:b, 0], x[a:b, 0])
    gm = cross / den
    for a, b in segs:
        if a < skip: continue
        d = y[a:b] - gm * x[a:b, 0]; num += np.dot(d, d)
    return 10 * np.log10(num / den / gm ** 2)
d = residual(y1, esses) - residual(y0, esses)
report("ESS GUARD 100 % (the real network): the esses' distortion re off", d < -10, f"{d:+.1f} dB")
def seg_db(y, segs):
    return 10 * np.log10(sum(np.dot(y[a:b], y[a:b]) for a, b in segs if a >= skip))
dv = seg_db(y1, vowels) - seg_db(y0, vowels)
report("ESS GUARD: the vowels (re off)", abs(dv) < 0.3, f"{dv:+.2f} dB")
bal = (seg_db(y1, esses) - seg_db(y1, vowels)) - (seg_db(x[:, 0], esses) - seg_db(x[:, 0], vowels))
report("ESS GUARD 100 %: esses re vowels as in the input", abs(bal) < 1.5, f"{bal:+.2f} dB")
# DYNAMICS: a tone stepping between -30 and -6 dBFS
t = np.arange(int(2 * fs)) / fs
step = f32(np.stack([np.where(t % 1.0 < 0.5, 10 ** (-30 / 20), 10 ** (-6 / 20)) * np.sin(2 * np.pi * 500 * t)] * 2, 1))
runs = [Run(f"dy{v}", fs, sets={"Style": "Tape", "Drive": "12", "Auto Gain": "Off", "Dynamics": str(v)}) for v in (-100, 0, 100)]
out = run_all(runs, {r.name: step for r in runs}, jobname="dyn")
def h3(r, t0):
    y = aligned(out[r.name], r.latency, len(step))[int(t0 * fs):int((t0 + 0.4) * fs), 0]
    return db(harm(y, fs, 500, 3) / harm(y, fs, 500, 1))
report("DYNAMICS +100: the loud part distorts more (3rd harmonic re 0)", h3(runs[2], 1.55) - h3(runs[1], 1.55) > 3, f"{h3(runs[2], 1.55) - h3(runs[1], 1.55):+.1f} dB")
report("DYNAMICS -100: the quiet part distorts more (3rd harmonic re 0)", h3(runs[0], 1.05) - h3(runs[1], 1.05) > 3, f"{h3(runs[0], 1.05) - h3(runs[1], 1.05):+.1f} dB")

fails = sum(1 for _, ok, _ in results if not ok)
print(f"\n{'ALL PASS' if fails == 0 else 'FAILURES'}: {len(results) - fails} of {len(results)} independent checks passed ({time.time() - START:.0f} s)")
sys.exit(fails)
