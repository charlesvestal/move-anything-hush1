from __future__ import annotations

import subprocess
from pathlib import Path

import numpy as np
import pytest


@pytest.fixture(scope="session")
def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def render_tool(repo_root: Path) -> Path:
    build_dir = repo_root / "build"
    build_dir.mkdir(exist_ok=True)
    tool = build_dir / "render_osc_fixture"
    cmd = [
        "cc",
        "-std=c11",
        "-O2",
        "-Isrc/dsp",
        "tools/render_osc_fixture.c",
        "src/dsp/sh101_osc.c",
        "src/dsp/sh101_filter.c",
        "-lm",
        "-o",
        str(tool),
    ]
    subprocess.check_call(cmd, cwd=repo_root)
    return tool


@pytest.fixture
def load_fixture(repo_root: Path, render_tool: Path):
    def _load(name: str) -> np.ndarray:
        out = repo_root / "build" / f"{name}.rawf32"
        subprocess.check_call([str(render_tool), name, str(out)], cwd=repo_root)
        return np.fromfile(out, dtype=np.float32)

    return _load


@pytest.fixture
def load_sweep_fixture(load_fixture):
    return load_fixture


@pytest.fixture
def with_metrics(repo_root: Path):
    def _metrics(path: str):
        import json

        full = repo_root / path
        with full.open("r", encoding="utf-8") as f:
            return json.load(f)

    return _metrics
