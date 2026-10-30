"""Split a circuit's colour c (aligned with the input x, at the voice's level) into the part that is a gain of the
voice (Bussgang: the local least-squares projection on x - the squash) and the new harmonics (the rest).
EXACTLY as the engine: two cascaded one-pole averages (2.5 ms each) of c*x and x*x."""
import numpy as np
from scipy.signal import lfilter
def split(c, x, fs, tau=0.0025):
    a = np.exp(-1.0 / (tau * fs))
    def lp2(v):
        v = lfilter([1 - a], [1, -a], v, axis=0); return lfilter([1 - a], [1, -a], v, axis=0)
    num = lp2(c * x); den = lp2(x * x)
    G = np.where(den > 1e-14, num / np.maximum(den, 1e-14), 0.0)
    coh = G * x
    return coh, c - coh, G
