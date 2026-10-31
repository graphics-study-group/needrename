"""needrename-sim: Python bindings for the PhysicsApp GPU physics simulator.

Importing this package resolves where the engine runtime lives, registers the
DLL search directory (Windows loads extension-module dependencies from
registered ``os.add_dll_directory`` entries only, not from ``PATH``), pins the
runtime data root, and then imports the ``_physicsapp`` extension module.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

__version__ = "0.1.0"

#: Environment variable naming the directory that holds the engine DLLs.
RUNTIME_DIR_ENV_VAR = "NEEDRENAME_RUNTIME_DIR"
#: Environment variable naming the runtime data root (builtin_assets/, projects/).
DATA_ROOT_ENV_VAR = "NEEDRENAME_DATA_ROOT"

_PACKAGE_DIR = Path(__file__).resolve().parent
# Wheel layout: the shared libraries and the data root travel inside the package.
_BUNDLED_RUNTIME_DIR = _PACKAGE_DIR / "bin"
_BUNDLED_DATA_ROOT = _PACKAGE_DIR / "share" / "needrename"


def _resolve_location(env_var: str, bundled_location: Path) -> Path | None:
    """Resolve one location: the environment variable first, then the wheel copy."""
    configured = os.environ.get(env_var)
    if configured:
        return Path(configured).expanduser()
    if bundled_location.is_dir():
        return bundled_location
    return None


def _raise_missing(runtime_dir: Path | None, data_root: Path | None) -> None:
    problems: list[str] = []
    if runtime_dir is None:
        problems.append(f"{RUNTIME_DIR_ENV_VAR} is not set and no bundled runtime directory exists")
    elif not runtime_dir.is_dir():
        problems.append(f"{RUNTIME_DIR_ENV_VAR}={runtime_dir} is not a directory")
    if data_root is None:
        problems.append(f"{DATA_ROOT_ENV_VAR} is not set and no bundled data root exists")
    elif not data_root.is_dir():
        problems.append(f"{DATA_ROOT_ENV_VAR}={data_root} is not a directory")

    raise ImportError(
        "needrename_sim cannot locate the engine runtime.\n"
        f"  Set {RUNTIME_DIR_ENV_VAR} to the directory holding PhysicsApp.dll and the "
        "engine DLLs (for example an engine build's bin/Debug).\n"
        f"  Set {DATA_ROOT_ENV_VAR} to the runtime data root holding builtin_assets/ "
        "and projects/ (for example an engine install's share/needrename).\n"
        f"  Both variables name absolute directories; the wheel provides both when "
        f"installed. Problems: {'; '.join(problems)}."
    )


_runtime_dir = _resolve_location(RUNTIME_DIR_ENV_VAR, _BUNDLED_RUNTIME_DIR)
_data_root = _resolve_location(DATA_ROOT_ENV_VAR, _BUNDLED_DATA_ROOT)
if _runtime_dir is None or _data_root is None or not _runtime_dir.is_dir() or not _data_root.is_dir():
    _raise_missing(_runtime_dir, _data_root)

# DLL search directories must be registered before the extension is imported.
# The handle stays referenced for the lifetime of the interpreter; dropping it
# would unregister the directory.
_dll_directory_handles = []
if sys.platform == "win32":
    _dll_directory_handles.append(os.add_dll_directory(str(_runtime_dir)))

# The engine reads the data root from the environment on first use, so a
# wheel-bundled data root must be exported before anything loads assets. An
# explicit environment variable keeps precedence.
os.environ.setdefault(DATA_ROOT_ENV_VAR, str(_data_root))

# No once-only guards: spawned multiprocessing workers re-import this module
# cleanly and inherit the environment variables.
from . import _physicsapp  # noqa: E402  (must follow the DLL directory registration)
from ._physicsapp import *  # noqa: E402,F401,F403

from . import robot  # noqa: E402
from .robot import Robot  # noqa: E402

__all__ = [
    "__version__",
    "RUNTIME_DIR_ENV_VAR",
    "DATA_ROOT_ENV_VAR",
    "Robot",
    "robot",
    *[name for name in dir(_physicsapp) if not name.startswith("_")],
]
