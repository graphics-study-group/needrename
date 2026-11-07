"""Shared fixtures and helpers for the `needrename-sim` test suite.

`MainClass` is a process-wide singleton, so a process can create exactly one
`PhysicsApp`. Scenarios that need an app therefore run in a fresh interpreter;
`run_scenario` provides that and the rest of the suite asserts on their output.

The suite runs against an installed wheel, a staged development runtime supplied
through `PYTHONPATH`, or the source tree with explicit `NEEDRENAME_*` locations.
It does not search build or install directories; the CMake `python_test` target
is the supported build-and-run entry point for a developer checkout.
"""

from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
TESTS_DIR = Path(__file__).resolve().parent
DATA_DIR = TESTS_DIR / "data"
DEV_SOURCE_ROOT = REPO_ROOT / "app" / "python" / "src"

# A wheel or staged runtime already resolves through the caller's PYTHONPATH.
# Otherwise use the sources for a custom run with explicit runtime locations.
PACKAGE_RESOLVED = importlib.util.find_spec("needrename_sim") is not None
DEV_SOURCE_USABLE = not PACKAGE_RESOLVED and (DEV_SOURCE_ROOT / "needrename_sim").is_dir()
if DEV_SOURCE_USABLE:
    sys.path.insert(0, str(DEV_SOURCE_ROOT))


def run_scenario(
    source: str,
    *,
    timeout: int = 600,
    extra_env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run `source` in a fresh interpreter and capture its output."""
    env = dict(os.environ)
    if DEV_SOURCE_USABLE:
        existing = env.get("PYTHONPATH")
        env["PYTHONPATH"] = str(DEV_SOURCE_ROOT) + (os.pathsep + existing if existing else "")
    env.update(extra_env or {})
    with tempfile.TemporaryDirectory(prefix="needrename-scenario-") as tmp:
        script = Path(tmp) / "scenario.py"
        script.write_text(textwrap.dedent(source), encoding="utf-8")
        return subprocess.run(
            [sys.executable, str(script)],
            cwd=str(REPO_ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )


def assert_scenario_passed(result: subprocess.CompletedProcess[str]) -> None:
    """Fail with the scenario's stderr when it did not exit cleanly."""
    assert result.returncode == 0, f"scenario failed ({result.returncode}):\n{result.stdout}\n{result.stderr}"


def scenario_result(result: subprocess.CompletedProcess[str]) -> dict:
    """Parse the `RESULT:{json}` line a scenario prints."""
    for line in result.stdout.splitlines():
        if line.startswith("RESULT:"):
            return json.loads(line[len("RESULT:") :])
    raise AssertionError(f"scenario printed no RESULT line:\n{result.stdout}\n{result.stderr}")


@pytest.fixture(scope="session")
def minimal_urdf() -> Path:
    """Path to the hand-written mesh-free robot fixture."""
    path = DATA_DIR / "minimal_robot.urdf"
    assert path.is_file(), f"missing URDF fixture: {path}"
    return path


@pytest.fixture(scope="session")
def tiny_inertia_urdf() -> Path:
    """Path to the fixture whose child link has a small but invertible inertia."""
    path = DATA_DIR / "tiny_inertia_robot.urdf"
    assert path.is_file(), f"missing URDF fixture: {path}"
    return path
