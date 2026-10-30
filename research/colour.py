"""Colour metrics: what a saturator does to a voice, beyond THD.
y is compared with the dry x after level matching. The linear part (best LTI filter x->y, from averaged cross
spectra) and the residual (the colour actually added) are separated, then measured per band."""
import numpy as np, soundfile as sf
from scipy.signal import welch, csd, stft, istft, butter, sosfiltfilt
BANDS = [(60,120),(120,250),(250,500),(500,1000),(1000,2000),(2000,4000),(4000,8000),(8000,16000)]
NAMES = ["60","120","250","500","1k","2k","4k","8k"]

def mono(path):
    x, fs = sf.read(path, always_2d=True); return x.mean(1), fs

def align(x, y):
    n = min(len(x), len(y)); X = np.fft.rfft(x[:n], 2*n); Y = np.fft.rfft(y[:n], 2*n)
    c = np.fft.irfft(np.conj(X) * Y); lag = int(np.argmax(np.abs(c[:4096])))
    return x[:n-lag], y[lag:n], lag

def bandsum(f, p, a, b): return p[(f >= a) & (f < b)].sum()

def measure(x, y, fs):
    x, y, lag = align(x, y)
    nper = 8192
    f, Sxx = welch(x, fs, nperseg=nper); _, Syy = welch(y, fs, nperseg=nper); _, Sxy = csd(x, y, fs, nperseg=nper)
    H = Sxy / np.maximum(Sxx, 1e-30)                       # linear part
    Srr = np.maximum(Syy - np.abs(Sxy)**2 / np.maximum(Sxx, 1e-30), 0)   # residual (incoherent with x) power
    g = np.sqrt(Sxx.sum() / Syy.sum())                     # level match on power
    out = {"lag": lag}
    out["tilt"] = [10*np.log10(bandsum(f, Syy*g*g, a, b) / bandsum(f, Sxx, a, b)) for a, b in BANDS]   # output vs dry per band
    out["lin"] = [10*np.log10(bandsum(f, np.abs(H)**2*Sxx*g*g, a, b) / bandsum(f, Sxx, a, b)) for a, b in BANDS]
    out["res"] = [10*np.log10(bandsum(f, Srr, a, b) / bandsum(f, Syy, a, b) + 1e-12) for a, b in BANDS]   # colour share per band
    out["colour"] = 10*np.log10(Srr.sum() / Syy.sum() + 1e-12)
    # box: colour added in the low mids against the colour in the presence (dB); tilt hump
    t = out["tilt"]; out["box"] = np.mean(t[2:4]) - 0.5*(np.mean(t[0:2]) + np.mean(t[5:8]))
    out["silk"] = t[7]
    out["fizz"] = out["res"][7] - out["res"][5]
    # articulation: 2-6 kHz envelope (5 ms) dynamic range, output vs dry (dB); low-mid envelope too
    def envdr(s, lo, hi):
        sos = butter(4, [lo, hi], 'band', fs=fs, output='sos'); e = np.abs(sosfiltfilt(sos, s))
        k = int(fs*0.005); e = np.sqrt(np.convolve(e**2, np.ones(k)/k, 'same'))
        e = e[e > 1e-6 * e.max()]; return 20*np.log10(np.percentile(e, 99) / np.percentile(e, 50))
    out["artic"] = envdr(y, 2000, 6000) - envdr(x, 2000, 6000)
    out["lowmid_dr"] = envdr(y, 250, 1000) - envdr(x, 250, 1000)
    # glitches: high-passed residual spikes (excess kurtosis vs the dry's HF) and slow gain wobble (pumping)
    r = y*g - np.real(np.fft.irfft(np.fft.rfft(x) * np.interp(np.fft.rfftfreq(len(x), 1/fs), f, np.abs(H)) * np.exp(1j*np.interp(np.fft.rfftfreq(len(x), 1/fs), f, np.unwrap(np.angle(H)))), len(x)))*g
    sos = butter(4, 8000, 'high', fs=fs, output='sos'); rh = sosfiltfilt(sos, r)
    out["click"] = float(np.max(np.abs(rh)) / (np.sqrt(np.mean(rh**2)) + 1e-12))
    w = int(fs*0.05); ex = np.sqrt(np.convolve(x**2, np.ones(w)/w, 'valid')); ey = np.sqrt(np.convolve((y*g)**2, np.ones(w)/w, 'valid'))
    m = ex > 0.1*ex.max(); gr = 20*np.log10(ey[m]/ex[m]); out["gain_sd"] = float(np.std(gr))
    return out

def row(name, o):
    return (f"{name[:26]:26s} col {o['colour']:6.1f} box {o['box']:+5.2f} silk {o['silk']:+5.2f} fizz {o['fizz']:+6.1f} "
            f"artic {o['artic']:+5.2f} lmDR {o['lowmid_dr']:+5.2f} gainSD {o['gain_sd']:4.2f} click {o['click']:5.1f} | res "
            + " ".join(f"{v:6.1f}" for v in o["res"]))
