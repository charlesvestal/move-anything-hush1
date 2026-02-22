# Reference Capture Guide

1. Record dry SH-101 phrases at 44.1kHz/24-bit for:
   - saw and pulse single-note sustains
   - filter cutoff sweep at low and high resonance
   - short and long envelope settings
2. Normalize to -12 dBFS peak.
3. Export analysis summaries into JSON metrics for CI checks.

Current CI thresholds are in `tests/audio/test_reference_alignment.py` and can be tightened as fixture quality improves.
