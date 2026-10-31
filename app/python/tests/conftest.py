"""Shared fixtures and helpers for the `needrename-sim` test suite.

`MainClass` is a process-wide singleton, so a process can create exactly one
`PhysicsApp`. Scenarios that need an app therefore run in a fresh interpreter;
`run_scenario` provides that and the rest of the suite asserts on their output.

The suite runs either against an installed wheel (no environment variables
needed: the package resolves its bundled runtime and data root) or against a
developer build tree, which the configuration helpers below detect.
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
#: Developer install prefixes probed when the environment is not configured.
DEV_PREFIX_CANDIDATES = (REPO_ROOT / "build" / "py-runtime", REPO_ROOT / "install")
#: Build-tree package produced by the CMake build (compiled extension + sources).
DEV_PACKAGE_ROOT = REPO_ROOT / "build" / "msvc" / "python"


def _configure_dev_environment() -> None:
    """Resolve a developer install prefix when the environment is not set."""
    if os.environ.get("NEEDRENAME_RUNTIME_DIR") and os.environ.get("NEEDRENAME_DATA_ROOT"):
        return
    for prefix in DEV_PREFIX_CANDIDATES:
        runtime_dir = prefix / "bin"
        data_root = prefix / "share" / "needrename"
        if runtime_dir.is_dir() and data_root.is_dir():
            os.environ.setdefault("NEEDRENAME_RUNTIME_DIR", str(runtime_dir))
            os.environ.setdefault("NEEDRENAME_DATA_ROOT", str(data_root))
            return


#: True when `needrename_sim` resolves without the build-tree path (installed
#: wheel). Checked before the build tree is added to `sys.path`.
WHEEL_INSTALLED = importlib.util.find_spec("needrename_sim") is not None
#: True when scenarios must be told where the build-tree package lives.
DEV_PACKAGE_USABLE = not WHEEL_INSTALLED and (DEV_PACKAGE_ROOT / "needrename_sim").is_dir()

# An installed wheel resolves its own bundled runtime and data root by design;
# only a developer build tree needs the environment pointed at a dev install.
if not WHEEL_INSTALLED:
    _configure_dev_environment()
if DEV_PACKAGE_USABLE:
    sys.path.insert(0, str(DEV_PACKAGE_ROOT))


def run_scenario(
    source: str,
    *,
    timeout: int = 600,
    extra_env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    """Run `source` in a fresh interpreter and capture its output."""
    env = dict(os.environ)
    if DEV_PACKAGE_USABLE:
        existing = env.get("PYTHONPATH")
        env["PYTHONPATH"] = str(DEV_PACKAGE_ROOT) + (os.pathsep + existing if existing else "")
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
