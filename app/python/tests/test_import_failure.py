"""Import discovery failure tests for `needrename_sim`.

These tests run in a subprocess so the environment can be controlled without
mutating the test process's own import state.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

PACKAGE_SOURCE_DIR = Path(__file__).resolve().parents[1] / "src"
RUNTIME_ENV_VAR = "NEEDRENAME_RUNTIME_DIR"
DATA_ROOT_ENV_VAR = "NEEDRENAME_DATA_ROOT"


def _clean_environment(**overrides: str) -> dict[str, str]:
    env = dict(os.environ)
    for name in (RUNTIME_ENV_VAR, DATA_ROOT_ENV_VAR):
        env.pop(name, None)
    env["PYTHONPATH"] = str(PACKAGE_SOURCE_DIR)
    env.update(overrides)
    return env


def _import_in_subprocess(env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-c", "import needrename_sim"],
        env=env,
        capture_output=True,
        text=True,
    )


def test_missing_locations_raise_actionable_error() -> None:
    """Neither environment variable nor a bundled location resolves."""
    result = _import_in_subprocess(_clean_environment())

    assert result.returncode != 0, "import must fail without a resolved runtime"
    assert "ImportError" in result.stderr
    assert RUNTIME_ENV_VAR in result.stderr
    assert DATA_ROOT_ENV_VAR in result.stderr


def test_non_directory_location_is_reported(tmp_path: Path) -> None:
    """A variable pointing at a non-directory is named in the failure message."""
    missing_runtime = tmp_path / "no-such-runtime-dir"
    result = _import_in_subprocess(
        _clean_environment(**{RUNTIME_ENV_VAR: str(missing_runtime), DATA_ROOT_ENV_VAR: str(tmp_path)})
    )

    assert result.returncode != 0
    assert "ImportError" in result.stderr
    assert str(missing_runtime) in result.stderr
