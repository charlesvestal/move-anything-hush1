import json
from pathlib import Path


def collect_param_keys(level_def):
    keys = []
    for param in level_def.get("params", []):
        if isinstance(param, dict) and "key" in param:
            keys.append(param["key"])
    return keys


def test_ui_hierarchy_exposes_all_sh101_params():
    module = json.loads(Path("src/module.json").read_text())
    levels = module["capabilities"]["ui_hierarchy"]["levels"]

    assert "root" in levels
    assert len(levels) > 1

    all_keys = set()
    root_nav_levels = []
    for level_name, level_def in levels.items():
        all_keys.update(collect_param_keys(level_def))
        if level_name == "root":
            for param in level_def.get("params", []):
                if isinstance(param, dict) and "level" in param:
                    root_nav_levels.append(param["level"])

    expected = {
        "preset",
        "saw",
        "pulse",
        "sub",
        "noise",
        "sub_mode",
        "white_noise",
        "pulse_width",
        "pwm_mode",
        "pwm_depth",
        "pwm_env_depth",
        "cutoff",
        "resonance",
        "env_amt",
        "key_follow",
        "filter_volume_correction",
        "filter_env_full_range",
        "filter_env_polarity",
        "attack",
        "decay",
        "sustain",
        "release",
        "f_attack",
        "f_decay",
        "f_sustain",
        "f_release",
        "lfo_rate",
        "lfo_pitch",
        "lfo_filter",
        "lfo_pwm",
        "lfo_waveform",
        "lfo_trigger",
        "lfo_sync",
        "lfo_invert",
        "lfo_pitch_snap",
        "glide",
        "portamento_mode",
        "portamento_linear",
        "velocity_sens",
        "filter_velocity_sens",
        "velocity_mode",
        "gate_trig_mode",
        "vca_mode",
        "adsr_declick",
        "same_note_quirk",
        "retrigger",
        "hold",
        "priority",
        "transpose",
        "octave_transpose",
        "fine_tune",
        "bend_range",
        "volume",
    }

    missing = expected - all_keys
    assert not missing, f"Missing SH-101 params in ui_hierarchy: {sorted(missing)}"

    # Ensure root actually uses submenu navigation for hierarchy UX.
    assert len(root_nav_levels) >= 4
    for nav in root_nav_levels:
        assert nav in levels
