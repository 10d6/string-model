"""Shared helpers for the evaluation scripts in tools/ (render a scenario,
read the result, measure pitch and level). Not a script itself."""
import json
import os
import subprocess
import tempfile
import numpy as np
from scipy.io import wavfile

RENDER = os.environ.get("RENDER", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "stringmodel_render"))


def render(scenario, out=None, renderer=None):
    """Render a scenario dict; returns (sr, mono float array)."""
    fd, path = tempfile.mkstemp(suffix=".json")
    with os.fdopen(fd, "w") as f:
        json.dump(scenario, f)
    out = out or path.replace(".json", ".wav")
    subprocess.run([renderer or RENDER, path, "--out", out], capture_output=True, check=True)
    os.unlink(path)
    return read(out)


def read(path):
    sr, x = wavfile.read(path)
    x = x.astype(float)
    if np.abs(x).max() > 2 ** 24:
        x /= 2 ** 31
    elif np.abs(x).max() > 2:
        x /= 2 ** 23
    return sr, (x.mean(1) if x.ndim > 1 else x)


def mtof(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def cents(f, ref):
    return 1200 * np.log2(f / ref)


def pitch(seg, sr, want, harmonics=(1, 2, 3)):
    """Median of the spectral peaks near harmonics of `want`, divided down
    (zero-padded FFT: ~0.1 cent resolution on a 1 s segment)."""
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 1 << 18))
    f = np.fft.rfftfreq(1 << 18, 1 / sr)
    est = []
    for h in harmonics:
        m = (f > want * h * 0.97) & (f < want * h * 1.03)
        if m.any():
            est.append(f[m][np.argmax(S[m])] / h)
    return float(np.median(est))


def level_db(seg):
    return 20 * np.log10(np.sqrt(np.mean(seg ** 2)) + 1e-12)


def pitch_track(x, sr, t0, t1, want, hop=0.005, win=0.06, span=0.06):
    """Fundamental frequency every `hop` s (peak picking near `want`)."""
    h, w = int(hop * sr), int(win * sr)
    f = np.fft.rfftfreq(1 << 16, 1 / sr)
    m = (f > want * (1 - span)) & (f < want * (1 + span))
    out = []
    for i in range(int(t0 * sr), int(t1 * sr) - w, h):
        S = np.abs(np.fft.rfft(x[i:i + w] * np.hanning(w), 1 << 16))
        out.append(f[m][np.argmax(S[m])])
    return np.array(out)
