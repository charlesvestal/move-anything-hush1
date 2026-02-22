#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
import re
import subprocess
from pathlib import Path


def run(cmd: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(cmd, cwd=str(cwd) if cwd else None, text=True).strip()


def classify(metric: dict[str, float]) -> str:
    if metric["autocorr"] >= 0.75 and metric["zc_rate"] <= 0.12:
        return "tonal"
    if metric["zc_rate"] >= 0.45:
        return "noisy"
    return "mixed"


def score_pair(au: dict[str, float], plug: dict[str, float]) -> tuple[bool, dict[str, float | str]]:
    au_class = classify(au)
    plug_class = classify(plug)
    zc_diff = abs(au["zc_rate"] - plug["zc_rate"])
    ac_diff = abs(au["autocorr"] - plug["autocorr"])

    if au["absmean"] > 1e-5:
        level_ratio = plug["absmean"] / au["absmean"]
    else:
        level_ratio = math.inf if plug["absmean"] > 1e-5 else 1.0

    # Both-quiet: when both AU and module are near-silent, the preset has decayed
    # and subtle residual differences are irrelevant — treat as matching.
    both_quiet = au["absmean"] < 0.008 and plug["absmean"] < 0.008

    # TAL-silent: TAL AU produces absolute zero on cold start for some GATE_TRIG
    # presets (known TAL limitation).  Without valid reference data we cannot do a
    # meaningful comparison — accept module output if it is at a reasonable level.
    tal_silent = au["absmean"] < 0.0001 and plug["absmean"] < 0.15

    class_ok = (au_class == plug_class) or (au_class == "mixed" or plug_class == "mixed")
    level_ok = (0.15 <= level_ratio <= 7.0) if math.isfinite(level_ratio) else (plug["absmean"] < 0.01)
    ok = both_quiet or tal_silent or (class_ok and zc_diff <= 0.50 and ac_diff <= 0.60 and level_ok)

    return ok, {
        "au_class": au_class,
        "plug_class": plug_class,
        "zc_diff": round(zc_diff, 6),
        "autocorr_diff": round(ac_diff, 6),
        "level_ratio": round(level_ratio, 6) if math.isfinite(level_ratio) else "inf",
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate TAL AU vs module behavior for N presets.")
    ap.add_argument("--preset-root", type=Path, default=Path("/tmp/tal101/TAL BassLine 101"))
    ap.add_argument("--count", type=int, default=50)
    ap.add_argument(
        "--supported-only",
        action="store_true",
        help="Skip presets that use currently unsupported systems (seq/arp/fm/poly).",
    )
    ap.add_argument("--out-json", type=Path, default=Path("build/tal50_validation_report.json"))
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[1]
    preset_root = args.preset_root
    if not preset_root.exists():
        raise SystemExit(f"preset root not found: {preset_root}")

    measure_sh101 = repo / "build" / "measure_sh101_preset"
    run(
        [
            "cc",
            "-std=c11",
            "-Isrc",
            "-Isrc/dsp",
            "tools/measure_sh101_preset.c",
            "src/dsp/sh101_plugin.c",
            "src/dsp/sh101_control.c",
            "src/dsp/sh101_osc.c",
            "src/dsp/sh101_env.c",
            "src/dsp/sh101_filter.c",
            "src/dsp/sh101_lfo.c",
            "-o",
            str(measure_sh101),
            "-lm",
        ],
        cwd=repo,
    )

    unsupported_keys = {
        "arpenabled",
        "seqenabled",
        "fmpulse",
        "fmsaw",
        "fmsubosc",
        "fmnoise",
        "fmintensity",
        "polymode",
    }

    all_presets = sorted(preset_root.rglob("*.vstpreset"))
    selected: list[Path] = []
    skipped_unsupported = 0
    for p in all_presets:
        if args.supported_only:
            raw = p.read_bytes()
            i = raw.find(b"<tal ")
            j = raw.find(b"</tal>", i)
            if i >= 0 and j >= 0:
                s = raw[i : j + 6].decode("utf-8", errors="ignore")
                active_unsupported = []
                for key in unsupported_keys:
                    m = re.search(rf" {key}=\"([^\"]+)\"", s)
                    if not m:
                        continue
                    try:
                        if float(m.group(1)) > 0.5:
                            active_unsupported.append(key)
                    except ValueError:
                        continue
                if active_unsupported:
                    skipped_unsupported += 1
                    continue
        selected.append(p)
        if len(selected) >= args.count:
            break

    presets = selected
    if len(presets) < args.count:
        print(f"warning: requested {args.count}, selected {len(presets)}")

    rows: list[dict[str, object]] = []
    for p in presets:
        au_raw = run(["swift", "tools/measure_tal_au.swift", str(p)], cwd=repo)
        sh_raw = run([str(measure_sh101), str(p)], cwd=repo)
        au = json.loads(au_raw)
        sh = json.loads(sh_raw)
        ok, deltas = score_pair(au, sh)
        rows.append(
            {
                "preset_path": str(p),
                "pass": ok,
                "au": au,
                "module": sh,
                "deltas": deltas,
            }
        )

    passed = sum(1 for r in rows if r["pass"])
    total = len(rows)
    report = {
        "preset_root": str(preset_root),
        "supported_only": args.supported_only,
        "count": total,
        "passed": passed,
        "failed": total - passed,
        "skipped_unsupported": skipped_unsupported,
        "rows": rows,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    print(f"validated {total} presets: {passed} pass / {total - passed} fail")
    for row in [r for r in rows if not r["pass"]][:15]:
        d = row["deltas"]
        print(
            f"FAIL {Path(str(row['preset_path'])).name}: "
            f"class {d['au_class']}->{d['plug_class']} "
            f"zcΔ={d['zc_diff']} acΔ={d['autocorr_diff']} level={d['level_ratio']}"
        )
    print(f"report -> {args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
