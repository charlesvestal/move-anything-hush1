# SH-101 Behavior Mapping

This module maps SH-101 panel semantics into plugin parameters and control logic:

- `priority`: `Last` or `Low` note priority.
- `hold`: keeps gate and current note when keys are released.
- `transpose`: semitone offset applied before pitch conversion.
- `glide`: slews pitch from current to target note.

Arp/seq and external clock behavior are represented as control-rate state transitions. The first implementation focuses on deterministic monophonic playability; calibration vectors in `tests/fixtures/control_vectors.json` lock expected outcomes.
