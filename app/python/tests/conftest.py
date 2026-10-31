"""Shared fixtures and helpers for the `needrename-sim` test suite.

`MainClass` is a process-wide singleton, so a process can create exactly one
`PhysicsApp`. Scenarios that need an app therefore run in a fresh interpreter;
`run_scenario` provides that and the rest of the suite asserts on their output.

The suite runs either against an installed wheel (no environment variables
needed: the package resolves its bundled runtime and data root) or against a
developer build tree, which the configuration helpers below detect. A developer
run imports the package sources from `app/python/src` and locates the compiled
extension in the build tree through `NEEDRENAME_BINDINGS_DIR`.
"""

from __future__ import annotations

import importlib.machinery
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
#: Package sources of a development run (`PYTHONPATH`); the compiled extension is
#: not staged next to them and is located separately.
DEV_SOURCE_ROOT = REPO_ROOT / "app" / "python" / "src"
#: Build-tree output directories, one per configuration. The project puts every
#: target in `bin/<config>`, so the engine DLLs and the compiled extension share
#: one directory per configuration and can never mismatch.
DEV_BUILD_CANDIDATES = tuple(
    REPO_ROOT / "build" / "msvc" / "bin" / config
    for config in ("Debug", "RelWithDebInfo", "Release")
)
#: Data roots of a developer install prefix.
DEV_DATA_CANDIDATES = (
    REPO_ROOT / "build" / "py-runtime" / "share" / "needrename",
    REPO_ROOT / "install" / "share" / "needrename",
)


def _has_extension(directory: Path) -> bool:
    """True when `directory` holds a `_physicsapp` extension for this interpreter."""
    return any(
        (directory / f"_physicsapp{suffix}").is_file()
        for suffix in importlib.machinery.EXTENSION_SUFFIXES
    )


def _find_dev_build_dir() -> Path | None:
    """Return the first built configuration directory, preferring Debug."""
    for directory in DEV_BUILD_CANDIDATES:
        if _has_extension(directory):
            return directory
    return None


def _configure_dev_environment() -> None:
    """Point the environment at a developer build tree when it is not set."""
    build_dir = _find_dev_build_dir()
    if build_dir is not None:
        # The extension is linked beside the engine DLLs, so one directory serves
        # both; NEEDRENAME_BINDINGS_DIR is only an override for odd layouts.
        os.environ.setdefault("NEEDRENAME_RUNTIME_DIR", str(build_dir))
    if not os.environ.get("NEEDRENAME_DATA_ROOT"):
        for data_root in DEV_DATA_CANDIDATES:
            if data_root.is_dir():
                os.environ.setdefault("NEEDRENAME_DATA_ROOT", str(data_root))
                break


#: True when `needrename_sim` resolves without the source tree (installed wheel).
#: Checked before the dev source tree is added to `sys.path`.
WHEEL_INSTALLED = importlib.util.find_spec("needrename_sim") is not None
#: True when the package sources and a built extension are both present.
DEV_SOURCE_USABLE = not WHEEL_INSTALLED and (DEV_SOURCE_ROOT / "needrename_sim").is_dir()
DEV_BUILD_DIR = None if WHEEL_INSTALLED else _find_dev_build_dir()
DEV_PACKAGE_USABLE = DEV_SOURCE_USABLE and DEV_BUILD_DIR is not None

# An installed wheel resolves its own bundled runtime, data root and extension by
# design; only a developer build tree needs the environment pointed at a build.
if not WHEEL_INSTALLED:
    _configure_dev_environment()
if DEV_PACKAGE_USABLE:
    sys.path.insert(0, str(DEV_SOURCE_ROOT))


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
