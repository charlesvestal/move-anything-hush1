#!/usr/bin/env python3
"""
Convert TAL BassLine-101 .vstpreset files into Move SH-101 parameter snapshots.

The mapping is intentionally approximate. It targets fast A/B sound-matching
workflows, not bit-identical emulation.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile


def extract_tal_xml(blob: bytes) -> str:
    start = blob.find(b'<?xml version="1.0" encoding="UTF-8"?> <tal ')
    if start < 0:
        start = blob.find(b'<?xml version="1.0" encoding="UTF-8"?><tal ')
    if start < 0:
        raise ValueError("TAL XML chunk not found in vstpreset blob")

    end = blob.find(b"</tal>", start)
    if end < 0:
        raise ValueError("TAL XML closing tag not found in vstpreset blob")

    return blob[start : end + len(b"</tal>")].decode("utf-8", errors="ignore")


def parse_program_attrs(tal_xml: str) -> dict[str, float | str]:
    root = ET.fromstring(tal_xml)
    program = root.find("./programs/program")
    if program is None:
        raise ValueError("Missing <program> node in TAL XML")

    attrs: dict[str, float | str] = {}
    for key, raw in program.attrib.items():
        try:
            attrs[key] = float(raw)
        except ValueError:
            attrs[key] = raw
    return attrs


def _clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def _f(attrs: dict[str, float | str], key: str, default: float = 0.0) -> float:
    value = attrs.get(key, default)
    if isinstance(value, str):
        try:
            value = float(value)
        except ValueError:
            return float(default)
    return float(value)


def _time_from_norm(value: float, lo: float, hi: float) -> float:
    n = _clamp(value, 0.0, 1.0)
    if n <= 0.0:
        return lo
    if n >= 1.0:
        return hi
    return lo * ((hi / lo) ** n)


def _time_from_norm_power(value: float, lo: float, hi: float, exponent: float) -> float:
    n = _clamp(value, 0.0, 1.0)
    return lo + (n**exponent) * (hi - lo)


def _round(v: float) -> float:
    return round(float(v), 6)


def _tal_adsr_mode_to_gate_mode(value: float) -> int:
    n = _clamp(value, 0.0, 1.0)
    if n < 0.25:
        return 2  # LFO
    if n < 0.75:
        return 0  # Gate
    return 1  # Gate + Trigger


def _tal_portamento_mode(value: float) -> int:
    n = _clamp(value, 0.0, 1.0)
    if n < 0.25:
        return 2  # Auto
    if n < 0.75:
        return 0  # Off
    return 1  # On


def _tal_lfo_waveform(value: float) -> int:
    n = _clamp(value, 0.0, 1.0)
    if n < (1.0 / 6.0):
        return 0  # Triangle
    if n < 0.5:
        return 1  # Rectangle
    if n < (5.0 / 6.0):
        return 2  # Random (sample-and-hold)
    return 3  # Noise


def _tal_three_state(value: float) -> int:
    n = _clamp(value, 0.0, 1.0)
    if n < 0.25:
        return 0
    if n < 0.75:
        return 1
    return 2


def _tal_dco_range_to_octaves(value: float) -> int:
    n = _clamp(value, 0.0, 1.0)
    if n < (1.0 / 6.0):
        return -1  # 16'
    if n < 0.5:
        return 0  # 8'
    if n < (5.0 / 6.0):
        return 1  # 4'
    return 2  # 2'


def map_tal_program_to_sh101(attrs: dict[str, float | str]) -> dict[str, float | int | str]:
    dco_lfo = _clamp(_f(attrs, "dcolfovalue"), 0.0, 1.0)
    pwm_mode = _tal_three_state(_f(attrs, "dcopwmmode"))
    pwm_value = _clamp(_f(attrs, "dcopwmvalue"), 0.0, 1.0)
    pwm_depth = pwm_value if pwm_mode == 2 else 0.0
    pwm_env_depth = pwm_value if pwm_mode == 0 else 0.0
    sub_mode = _tal_three_state(_f(attrs, "suboscmode"))
    white_noise = 1 if _clamp(_f(attrs, "whitenoiseenabled"), 0.0, 1.0) >= 0.5 else 0

    attack = _time_from_norm(_f(attrs, "adsrattack"), 0.001, 4.0)
    decay = _time_from_norm_power(_f(attrs, "adsrdecay"), 0.001, 6.0, 2.2)
    sustain = _clamp(_f(attrs, "adsrsustain"), 0.0, 1.0)
    release = _time_from_norm_power(_f(attrs, "adsrrelease"), 0.001, 8.0, 2.2)
    env_raw = _clamp(_f(attrs, "filterenvelopevalue"), 0.0, 1.0)
    env_full_range = 1 if _clamp(_f(attrs, "filterenvelopevaluefullrange"), 0.0, 1.0) >= 0.5 else 0
    env_amt = env_raw
    env_polarity = 0
    if env_full_range:
        env_centered = (env_raw - 0.5) * 2.0
        env_amt = abs(env_centered)
        env_polarity = 1 if env_centered < 0.0 else 0

    velocity_sens = _clamp(_f(attrs, "controlvelocityvolume"), 0.0, 1.0)
    filter_velocity_sens = _clamp(_f(attrs, "controlvelocityenvelope"), 0.0, 1.0)
    velocity_mode = 1 if max(velocity_sens, filter_velocity_sens) > 0.01 else 0

    portamento_mode = _tal_portamento_mode(_f(attrs, "portamentomode"))

    tal_oct = int(round((_clamp(_f(attrs, "octavetranspose"), 0.0, 1.0) - 0.5) * 4.0))
    tal_range = _tal_dco_range_to_octaves(_f(attrs, "dcorange"))
    total_oct = tal_oct + tal_range
    octave_transpose = int(max(-2, min(2, total_oct)))
    transpose = int(max(-24, min(24, (total_oct - octave_transpose) * 12)))

    lfo_waveform = _tal_lfo_waveform(_f(attrs, "lfowaveform"))
    lfo_trigger = 1 if _clamp(_f(attrs, "lfotrigger"), 0.0, 1.0) >= 0.5 else 0
    lfo_sync = 1 if _clamp(_f(attrs, "lfosync"), 0.0, 1.0) >= 0.5 else 0
    lfo_invert = 1 if _clamp(_f(attrs, "lfoinverted"), 0.0, 1.0) >= 0.5 else 0
    fine_tune = _clamp((_clamp(_f(attrs, "masterfinetune"), 0.0, 1.0) - 0.5) * 200.0, -100.0, 100.0)
    gate_trig_mode = _tal_adsr_mode_to_gate_mode(_f(attrs, "adsrmode"))
    vca_mode = 1 if _clamp(_f(attrs, "vcamode", 1.0), 0.0, 1.0) >= 0.5 else 0

    return {
        "name": str(attrs.get("programname", "Unnamed TAL Preset")),
        "saw": _round(_clamp(_f(attrs, "sawvolume"), 0.0, 1.0)),
        "pulse": _round(_clamp(_f(attrs, "pulsevolume"), 0.0, 1.0)),
        "sub": _round(_clamp(_f(attrs, "suboscvolume"), 0.0, 1.0)),
        "sub_mode": sub_mode,
        "noise": _round(_clamp(_f(attrs, "noisevolume"), 0.0, 1.0)),
        "white_noise": white_noise,
        "pulse_width": _round(_clamp(pwm_value, 0.05, 0.95)),
        "pwm_mode": pwm_mode,
        "pwm_depth": _round(pwm_depth),
        "pwm_env_depth": _round(pwm_env_depth),
        "cutoff": _round(_clamp(_f(attrs, "filtercutoff"), 0.0, 1.0) ** 0.5),
        "resonance": _round(_clamp(_f(attrs, "filterresonance") * 1.2, 0.0, 1.2)),
        "env_amt": _round(_clamp(env_amt, 0.0, 1.0)),
        "filter_env_full_range": env_full_range,
        "filter_env_polarity": env_polarity,
        "filter_volume_correction": _round(_clamp(_f(attrs, "filtervolumecorrection"), 0.0, 1.0)),
        "key_follow": _round(_clamp(_f(attrs, "filterkeyboardvalue"), 0.0, 1.0)),
        "attack": _round(attack),
        "decay": _round(decay),
        "sustain": _round(sustain),
        "release": _round(release),
        "f_attack": _round(attack),
        "f_decay": _round(decay),
        "f_sustain": _round(sustain),
        "f_release": _round(release),
        "glide": _round(_clamp(_f(attrs, "portamentointensity"), 0.0, 1.0) * 500.0),
        "portamento_mode": portamento_mode,
        "lfo_rate": _round(0.02 + _clamp(_f(attrs, "lforate"), 0.0, 1.0) * (40.0 - 0.02)),
        "lfo_waveform": lfo_waveform,
        "lfo_trigger": lfo_trigger,
        "lfo_sync": lfo_sync,
        "lfo_invert": lfo_invert,
        "lfo_pitch_snap": 1 if _clamp(_f(attrs, "dcolfovaluesnap"), 0.0, 1.0) >= 0.5 else 0,
        "lfo_pitch": _round(dco_lfo),
        "lfo_filter": _round(_clamp(_f(attrs, "filtermodulationvalue"), 0.0, 1.0)),
        "lfo_pwm": _round(pwm_depth),
        "velocity_sens": _round(velocity_sens),
        "filter_velocity_sens": _round(filter_velocity_sens),
        "velocity_mode": velocity_mode,
        "adsr_declick": _round(_clamp(_f(attrs, "adsrdecklick"), 0.0, 1.0)),
        "vca_mode": vca_mode,
        "portamento_linear": 1 if _clamp(_f(attrs, "portamentolinear"), 0.0, 1.0) >= 0.5 else 0,
        "priority": 0,
        "gate_trig_mode": gate_trig_mode,
        "retrigger": 1 if gate_trig_mode == 1 else 0,
        "volume": _round(_clamp(_f(attrs, "volume"), 0.0, 1.0)),
        "transpose": transpose,
        "octave_transpose": octave_transpose,
        "fine_tune": _round(fine_tune),
        "source_path": str(attrs.get("path", "")),
        "source_category": str(attrs.get("category", "")),
    }


def convert_vstpreset_blob(blob: bytes) -> dict[str, float | int | str]:
    tal_xml = extract_tal_xml(blob)
    attrs = parse_program_attrs(tal_xml)
    return map_tal_program_to_sh101(attrs)


def _iter_zip_presets(zip_path: Path):
    with zipfile.ZipFile(zip_path, "r") as zf:
        for info in sorted(zf.infolist(), key=lambda i: i.filename):
            name = info.filename
            if not name.endswith(".vstpreset"):
                continue
            if name.startswith("__MACOSX/"):
                continue
            yield name, zf.read(info)


def convert_zip(zip_path: Path) -> list[dict[str, float | int | str]]:
    out = []
    for name, blob in _iter_zip_presets(zip_path):
        mapped = convert_vstpreset_blob(blob)
        mapped["archive_path"] = name
        out.append(mapped)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", required=True, type=Path, help="Path to TAL BassLine-101 VST3 preset zip")
    parser.add_argument(
        "--out-json",
        type=Path,
        default=Path("build/tal_bassline_101_converted_sh101.json"),
        help="Output path for converted preset JSON",
    )
    args = parser.parse_args()

    presets = convert_zip(args.zip)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(presets, indent=2) + "\n", encoding="utf-8")
    print(f"Converted {len(presets)} presets -> {args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
