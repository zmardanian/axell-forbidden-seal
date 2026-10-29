"""measure.py: harmonic profile, THD, aliasing and small-signal gain of every style (SealRender --sine)."""
import os, sys, subprocess, tempfile, numpy as np, soundfile as sf
R = os.environ.get("SEAL_RENDER", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build-tools", "SealRender"))
STYLES = ["TAPE", "TRIODE", "PENTODE", "CONSOLE", "GERMANIUM", "IRON", "DIODE", "FOLD"]
FS = 48000
def sine(freq, dbfs, keys, secs=1.0, fs=FS):
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "o.wav")
        subprocess.run([R, "--sine", out, str(freq), str(dbfs), str(secs), str(fs)] + [f"{k}={v}" for k, v in keys.items()],
                       check=True, stderr=subprocess.DEVNULL)
        return sf.read(out)[0][:, 0]
def bh7(n):
    """7-term Blackman-Harris window (sidelobes < -180 dB: no leakage counted as aliasing)"""
    a = [0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803, 0.01081174209837, 0.00077658482522, 0.00001388721735]
    k = np.arange(n) / (n - 1)
    return sum(((-1) ** j) * a[j] * np.cos(2 * np.pi * j * k) for j in range(7))
def spectrum(y, fs=FS):
    y = y[len(y) // 4:]                     # settled
    w = bh7(len(y)); Y = np.abs(np.fft.rfft(y * w)) / (w.sum() / 2)
    return np.fft.rfftfreq(len(y), 1 / fs), Y
def harmonics(y, f0, n=9, fs=FS):
    f, Y = spectrum(y, fs)
    out = []
    for k in range(1, n + 1):
        if k * f0 >= fs / 2: break
        i = np.argmin(np.abs(f - k * f0)); out.append(Y[max(0, i - 3):i + 4].max())
    return np.array(out)
def alias_db(y, f0, fs=FS):
    """energy away from the harmonics (+-3 bins) and DC, re the fundamental, in the audio band 20 Hz - 20 kHz"""
    f, Y = spectrum(y, fs)
    P = Y ** 2; mask = (f > 20) & (f < 20000)
    hb = np.zeros_like(mask)
    for k in range(1, int(fs / 2 / f0) + 1):
        hb |= np.abs(f - k * f0) < 10 * (f[1] - f[0])
    fund = P[np.abs(f - f0) < 10 * (f[1] - f[0])].sum()
    return 10 * np.log10(P[mask & ~hb].sum() / fund + 1e-30)
if __name__ == "__main__":
    q = int(os.environ.get("Q", "1"))
    print(f"quality {q} ({2 ** (q + 1)}x)")
    for s, name in enumerate(STYLES):
        keys = dict(style=s, auto=0, quality=q, character=50)
        g = harmonics(sine(1000, -40, dict(keys, drive=0)), 1000)[0] / 10 ** (-40 / 20)
        rows = []
        for drive in (6, 18, 30):
            h = harmonics(sine(1000, -18, dict(keys, drive=drive)), 1000)
            thd = 20 * np.log10(np.sqrt((h[1:] ** 2).sum()) / h[0])
            even = 20 * np.log10(np.sqrt((h[1::2] ** 2).sum()) / (np.sqrt((h[2::2] ** 2).sum()) + 1e-12))
            rows.append(f"d{drive}: THD {thd:6.1f} dB, even-odd {even:+5.1f} dB, H2..H5 " + " ".join(f"{20*np.log10(x/h[0]+1e-12):6.1f}" for x in h[1:5]))
        al = alias_db(sine(7500, -6, dict(keys, drive=24)), 7500)
        print(f"{name:10s} small-signal {20*np.log10(g):+.2f} dB | alias (7.5 kHz, -6 dBFS, +24 dB) {al:6.1f} dB")
        for r in rows: print("            " + r)
