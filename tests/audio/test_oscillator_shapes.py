import numpy as np


def test_saw_has_strong_even_and_odd_harmonics(load_fixture):
    x = load_fixture("saw_A3")
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))

    sample_rate = 44100
    f0 = 220.0

    def bin_for(freq: float) -> int:
        return int(round(freq * len(x) / sample_rate))

    h1 = spec[bin_for(f0)]
    h2 = spec[bin_for(2 * f0)]
    h3 = spec[bin_for(3 * f0)]

    assert h2 / h1 > 0.25
    assert h3 / h1 > 0.15
