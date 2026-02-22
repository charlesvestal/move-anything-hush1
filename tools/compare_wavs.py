#!/usr/bin/env python3
"""Compare TAL AU and SH-101 module WAV renders block-by-block.

Renders both WAVs, then compares:
  - Block-by-block amplitude envelope (absmean per block)
  - Pitch tracking via autocorrelation per block
  - Overall level ratio, zero-crossing rate, spectral tilt
  - Per-block correlation coefficient

Usage:
  python3 tools/compare_wavs.py <preset.vstpreset>
  python3 tools/compare_wavs.py --batch <preset_dir> [--count N] [--supported-only]
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


BLOCK = 128
SR = 44100
TOTAL_BLOCKS = 96  # 64 note-on + 32 release


def read_wav_mono(path: str) -> list[float]:
    """Read 16-bit mono WAV, return float samples in [-1,1]."""
    with open(path, "rb") as f:
        data = f.read()
    # Skip to 'data' chunk
    idx = data.find(b"data")
    if idx < 0:
        raise ValueError(f"no data chunk in {path}")
    size = struct.unpack_from("<I", data, idx + 4)[0]
    n_samples = size // 2
    samples = []
    offset = idx + 8
    for i in range(n_samples):
        s = struct.unpack_from("<h", data, offset + i * 2)[0]
        samples.append(s / 32768.0)
    return samples


def block_metrics(samples: list[float], block_idx: int) -> dict:
    """Compute metrics for a single block."""
    start = block_idx * BLOCK
    end = start + BLOCK
    blk = samples[start:end]
    if len(blk) < BLOCK:
        blk.extend([0.0] * (BLOCK - len(blk)))

    peak = max(abs(s) for s in blk)
    absmean = sum(abs(s) for s in blk) / BLOCK

    # Zero crossings
    zc = 0
    for i in range(1, BLOCK):
        if (blk[i - 1] >= 0 and blk[i] < 0) or (blk[i - 1] < 0 and blk[i] >= 0):
            zc += 1
    zc_rate = zc / (BLOCK - 1)

    # Pitch via autocorrelation (lag 20-500)
    best_corr = 0.0
    best_lag = 0
    for lag in range(20, min(501, BLOCK)):
        acc = 0.0
        e1 = 0.0
        e2 = 0.0
        for i in range(lag, BLOCK):
            x = blk[i]
            y = blk[i - lag]
            acc += x * y
            e1 += x * x
            e2 += y * y
        if e1 > 1e-12 and e2 > 1e-12:
            r = acc / math.sqrt(e1 * e2)
            if r > best_corr:
                best_corr = r
                best_lag = lag
    freq_hz = SR / best_lag if best_lag > 0 else 0.0

    return {
        "peak": peak,
        "absmean": absmean,
        "zc_rate": zc_rate,
        "autocorr": best_corr,
        "lag": best_lag,
        "freq_hz": round(freq_hz, 1),
    }


def cross_correlation(a: list[float], b: list[float], start: int, end: int) -> float:
    """Normalized cross-correlation between two signal segments."""
    seg_a = a[start:end]
    seg_b = b[start:end]
    n = min(len(seg_a), len(seg_b))
    if n == 0:
        return 0.0
    mean_a = sum(seg_a[:n]) / n
    mean_b = sum(seg_b[:n]) / n
    num = sum((seg_a[i] - mean_a) * (seg_b[i] - mean_b) for i in range(n))
    da = math.sqrt(sum((seg_a[i] - mean_a) ** 2 for i in range(n)))
    db = math.sqrt(sum((seg_b[i] - mean_b) ** 2 for i in range(n)))
    if da < 1e-10 or db < 1e-10:
        return 0.0
    return num / (da * db)


def compare_preset(preset_path: str, repo: Path, verbose: bool = False) -> dict:
    """Render and compare one preset."""
    name = Path(preset_path).stem

    with tempfile.TemporaryDirectory() as td:
        tal_wav = os.path.join(td, "tal.wav")
        mod_wav = os.path.join(td, "mod.wav")

        # Render TAL AU
        subprocess.run(
            ["swift", str(repo / "tools" / "render_tal_au_wav.swift"), preset_path, tal_wav],
            capture_output=True, timeout=30,
        )
        # Render module
        subprocess.run(
            [str(repo / "build" / "render_sh101_wav"), preset_path, mod_wav],
            capture_output=True, timeout=10,
        )

        if not os.path.exists(tal_wav) or not os.path.exists(mod_wav):
            return {"name": name, "error": "render_failed"}

        tal = read_wav_mono(tal_wav)
        mod = read_wav_mono(mod_wav)

    n_blocks = min(len(tal), len(mod)) // BLOCK

    # Per-block analysis
    blocks = []
    for b in range(n_blocks):
        tm = block_metrics(tal, b)
        mm = block_metrics(mod, b)

        # Cross-correlation for this block
        xcorr = cross_correlation(tal, mod, b * BLOCK, (b + 1) * BLOCK)

        blocks.append({
            "block": b,
            "tal_absmean": round(tm["absmean"], 6),
            "mod_absmean": round(mm["absmean"], 6),
            "tal_peak": round(tm["peak"], 6),
            "mod_peak": round(mm["peak"], 6),
            "tal_freq": tm["freq_hz"],
            "mod_freq": mm["freq_hz"],
            "tal_autocorr": round(tm["autocorr"], 4),
            "mod_autocorr": round(mm["autocorr"], 4),
            "xcorr": round(xcorr, 4),
        })

    # Summary metrics over full render
    tal_total_energy = sum(b["tal_absmean"] for b in blocks)
    mod_total_energy = sum(b["mod_absmean"] for b in blocks)
    energy_ratio = mod_total_energy / tal_total_energy if tal_total_energy > 1e-6 else float("inf")

    # Envelope shape correlation (absmean per block)
    tal_env = [b["tal_absmean"] for b in blocks]
    mod_env = [b["mod_absmean"] for b in blocks]
    env_corr = _list_correlation(tal_env, mod_env)

    # Pitch agreement: fraction of blocks where freq differs < 10%
    pitch_agree = 0
    pitch_blocks = 0
    for b in blocks:
        if b["tal_absmean"] > 0.005 and b["mod_absmean"] > 0.005:
            pitch_blocks += 1
            tf = b["tal_freq"]
            mf = b["mod_freq"]
            if tf > 0 and mf > 0:
                ratio = max(tf, mf) / min(tf, mf)
                if ratio < 1.10:
                    pitch_agree += 1
    pitch_match_pct = round(100 * pitch_agree / pitch_blocks, 1) if pitch_blocks > 0 else 0.0

    # Average cross-correlation for non-silent blocks
    active_xcorrs = [b["xcorr"] for b in blocks if b["tal_absmean"] > 0.005 or b["mod_absmean"] > 0.005]
    avg_xcorr = sum(active_xcorrs) / len(active_xcorrs) if active_xcorrs else 0.0

    # Pass/fail criteria
    env_ok = env_corr > 0.85
    pitch_ok = pitch_match_pct >= 70.0
    level_ok = 0.2 <= energy_ratio <= 5.0 if math.isfinite(energy_ratio) else False
    xcorr_ok = avg_xcorr > 0.5
    passed = env_ok and pitch_ok and level_ok

    result = {
        "name": name,
        "passed": passed,
        "energy_ratio": round(energy_ratio, 4) if math.isfinite(energy_ratio) else "inf",
        "env_correlation": round(env_corr, 4),
        "pitch_match_pct": pitch_match_pct,
        "avg_xcorr": round(avg_xcorr, 4),
        "criteria": {
            "env_ok": env_ok,
            "pitch_ok": pitch_ok,
            "level_ok": level_ok,
            "xcorr_ok": xcorr_ok,
        },
    }

    if verbose:
        result["blocks"] = blocks

    return result


def _list_correlation(a: list[float], b: list[float]) -> float:
    n = len(a)
    if n == 0:
        return 0.0
    ma = sum(a) / n
    mb = sum(b) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = math.sqrt(sum((a[i] - ma) ** 2 for i in range(n)))
    db = math.sqrt(sum((b[i] - mb) ** 2 for i in range(n)))
    if da < 1e-10 or db < 1e-10:
        return 0.0
    return num / (da * db)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("preset", nargs="?", help="Single preset to compare")
    ap.add_argument("--batch", type=Path, help="Preset directory for batch mode")
    ap.add_argument("--count", type=int, default=10)
    ap.add_argument("--supported-only", action="store_true")
    ap.add_argument("--verbose", "-v", action="store_true")
    ap.add_argument("--out-json", type=Path, default=Path("build/wav_comparison_report.json"))
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[1]

    if args.preset:
        result = compare_preset(args.preset, repo, verbose=args.verbose)
        print(json.dumps(result, indent=2))
        return

    if not args.batch:
        args.batch = Path("/tmp/tal101/TAL BassLine 101")

    unsupported_keys = {"arpenabled", "seqenabled", "fmpulse", "fmsaw", "fmsubosc", "fmnoise", "fmintensity", "polymode"}

    all_presets = sorted(args.batch.rglob("*.vstpreset"))
    selected = []
    for p in all_presets:
        if args.supported_only:
            raw = p.read_bytes()
            i = raw.find(b"<tal ")
            j = raw.find(b"</tal>", i)
            if i >= 0 and j >= 0:
                s = raw[i: j + 6].decode("utf-8", errors="ignore")
                skip = False
                for key in unsupported_keys:
                    m = re.search(rf" {key}=\"([^\"]+)\"", s)
                    if m:
                        try:
                            if float(m.group(1)) > 0.5:
                                skip = True
                                break
                        except ValueError:
                            continue
                if skip:
                    continue
        selected.append(p)
        if len(selected) >= args.count:
            break

    results = []
    for i, p in enumerate(selected):
        sys.stderr.write(f"\r[{i+1}/{len(selected)}] {p.stem}...")
        sys.stderr.flush()
        r = compare_preset(str(p), repo, verbose=args.verbose)
        results.append(r)
    sys.stderr.write("\n")

    passed = sum(1 for r in results if r.get("passed"))
    total = len(results)
    print(f"WAV comparison: {passed}/{total} pass")
    for r in results:
        if not r.get("passed"):
            c = r.get("criteria", {})
            fails = [k for k, v in c.items() if not v]
            print(f"  FAIL {r['name']}: energy={r.get('energy_ratio','?')} env_corr={r.get('env_correlation','?')} "
                  f"pitch={r.get('pitch_match_pct','?')}% xcorr={r.get('avg_xcorr','?')} [{', '.join(fails)}]")

    report = {"total": total, "passed": passed, "results": results}
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n")
    print(f"report -> {args.out_json}")


if __name__ == "__main__":
    main()
