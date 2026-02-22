from tools import tal_preset_converter as conv


def test_extract_tal_xml_chunk_from_vstpreset_blob():
    xml = (
        '<?xml version="1.0" encoding="UTF-8"?> '
        '<tal curprogram="0" version="2.0"><programs><program programname="X" '
        'sawvolume="0.5"/></programs></tal>'
    )
    blob = b"VST3\x00junk" + xml.encode("utf-8") + b"\x00tail"
    assert conv.extract_tal_xml(blob) == xml


def test_map_program_attrs_to_sh101_params():
    attrs = {
        "programname": "Test Preset",
        "sawvolume": 0.7,
        "pulsevolume": 0.3,
        "suboscvolume": 0.2,
        "suboscmode": 0.9,
        "noisevolume": 0.1,
        "whitenoiseenabled": 1.0,
        "dcopwmvalue": 1.0,
        "dcopwmmode": 0.5,
        "dcolfovalue": 0.6,
        "dcorange": 0.75,
        "filtercutoff": 0.2,
        "filterresonance": 0.4,
        "filterenvelopevalue": 0.9,
        "filterenvelopevaluefullrange": 1.0,
        "filterkeyboardvalue": 0.8,
        "adsrattack": 0.0,
        "adsrdecay": 1.0,
        "adsrsustain": 0.5,
        "adsrrelease": 0.25,
        "adsrmode": 0.95,
        "vcamode": 0.9,
        "portamentointensity": 0.5,
        "portamentomode": 0.45,
        "lforate": 0.5,
        "lfowaveform": 0.8,
        "lfotrigger": 1.0,
        "lfosync": 1.0,
        "lfoinverted": 1.0,
        "filtermodulationvalue": 0.25,
        "controlvelocityvolume": 0.75,
        "controlvelocityenvelope": 0.33,
        "volume": 0.62,
        "masterfinetune": 0.625,
        "octavetranspose": 0.5,
    }

    out = conv.map_tal_program_to_sh101(attrs)

    assert out["name"] == "Test Preset"
    assert out["saw"] == 0.7
    assert out["pulse"] == 0.3
    assert out["sub"] == 0.2
    assert out["sub_mode"] == 2
    assert out["noise"] == 0.1
    assert out["white_noise"] == 1
    assert out["pulse_width"] == 0.95
    assert out["pwm_mode"] == 1
    assert out["pwm_depth"] == 0.0
    assert out["pwm_env_depth"] == 0.0
    assert out["cutoff"] == 0.447214
    assert out["resonance"] == 0.48
    assert out["env_amt"] == 0.8
    assert out["filter_env_full_range"] == 1
    assert out["filter_env_polarity"] == 0
    assert out["key_follow"] == 0.8
    assert out["attack"] == 0.001
    assert out["decay"] == 6.0
    assert out["sustain"] == 0.5
    assert round(out["release"], 6) == round(0.001 + (0.25 ** 2.2) * (8.0 - 0.001), 6)
    assert out["f_attack"] == out["attack"]
    assert out["f_decay"] == out["decay"]
    assert out["f_sustain"] == out["sustain"]
    assert out["f_release"] == out["release"]
    assert out["glide"] == 250.0
    assert out["portamento_mode"] == 0
    assert out["lfo_rate"] == 20.01
    assert out["lfo_pitch"] == 0.6
    assert out["lfo_filter"] == 0.25
    assert out["lfo_pwm"] == 0.0
    assert out["lfo_waveform"] == 2
    assert out["lfo_trigger"] == 1
    assert out["lfo_sync"] == 1
    assert out["lfo_invert"] == 1
    assert out["velocity_sens"] == 0.75
    assert out["filter_velocity_sens"] == 0.33
    assert out["velocity_mode"] == 1
    assert out["priority"] == 0
    assert out["gate_trig_mode"] == 1
    assert out["retrigger"] == 1
    assert out["vca_mode"] == 1
    assert out["adsr_declick"] == 0.0
    assert out["filter_volume_correction"] == 0.0
    assert out["portamento_linear"] == 0
    assert out["lfo_pitch_snap"] == 0
    assert out["volume"] == 0.62
    assert out["fine_tune"] == 25.0
    assert out["transpose"] == 0
    assert out["octave_transpose"] == 1


def test_mapping_avoids_millisecond_decay_for_typical_303_values():
    attrs = {
        "programname": "303-like",
        "filtercutoff": 0.199,
        "adsrdecay": 0.212,
        "adsrrelease": 0.0,
        "adsrsustain": 0.0,
    }
    out = conv.map_tal_program_to_sh101(attrs)
    assert out["decay"] > 0.15
    assert out["cutoff"] > 0.4


def test_mapping_supports_all_adsr_trigger_modes():
    lfo = conv.map_tal_program_to_sh101({"adsrmode": 0.0})
    gate = conv.map_tal_program_to_sh101({"adsrmode": 0.333})
    gate_trig = conv.map_tal_program_to_sh101({"adsrmode": 1.0})

    assert lfo["gate_trig_mode"] == 2
    assert lfo["retrigger"] == 0
    assert gate["gate_trig_mode"] == 0
    assert gate["retrigger"] == 0
    assert gate_trig["gate_trig_mode"] == 1
    assert gate_trig["retrigger"] == 1


def test_mapping_matches_au_threshold_modes():
    # AU reverse-engineered thresholds:
    # adsrmode: [0..0.249]=LFO, [0.25..0.749]=Gate, [0.75..1]=Gate+Trigger
    # portamentomode: [0..0.249]=Auto, [0.25..0.749]=Off, [0.75..1]=On
    # lfowaveform: [0..0.166]=Tri, [0.167..0.499]=Rect, [0.5..0.833]=Random, [0.834..1]=Noise
    m_lfo = conv.map_tal_program_to_sh101({"adsrmode": 0.20, "portamentomode": 0.0, "lfowaveform": 0.10})
    m_gate = conv.map_tal_program_to_sh101({"adsrmode": 0.69, "portamentomode": 0.40, "lfowaveform": 0.20})
    m_trig = conv.map_tal_program_to_sh101({"adsrmode": 0.95, "portamentomode": 0.95, "lfowaveform": 0.60})
    m_noise = conv.map_tal_program_to_sh101({"lfowaveform": 0.95})

    assert m_lfo["gate_trig_mode"] == 2
    assert m_gate["gate_trig_mode"] == 0
    assert m_trig["gate_trig_mode"] == 1

    assert m_lfo["portamento_mode"] == 2
    assert m_gate["portamento_mode"] == 0
    assert m_trig["portamento_mode"] == 1

    assert m_lfo["lfo_waveform"] == 0
    assert m_gate["lfo_waveform"] == 1
    assert m_trig["lfo_waveform"] == 2
    assert m_noise["lfo_waveform"] == 3
