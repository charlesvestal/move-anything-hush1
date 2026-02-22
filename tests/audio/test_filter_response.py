import numpy as np


def test_resonance_peak_increases_near_cutoff(load_sweep_fixture):
    low_res = load_sweep_fixture("cutoff_0p35_res_0p1")
    hi_res = load_sweep_fixture("cutoff_0p35_res_0p85")

    spec1 = np.abs(np.fft.rfft(low_res * np.hanning(len(low_res))))
    spec2 = np.abs(np.fft.rfft(hi_res * np.hanning(len(hi_res))))

    sample_rate = 44100
    cutoff_hz = 30.0 + 0.35 * 0.35 * 15000.0
    cutoff_bin = int(round(cutoff_hz * len(low_res) / sample_rate))

    p1 = spec1[cutoff_bin]
    p2 = spec2[cutoff_bin]

    assert p2 > p1 * 1.25
