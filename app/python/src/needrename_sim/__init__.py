"""needrename-sim: Python bindings for the PhysicsApp GPU physics simulator.

Importing this package resolves where the engine runtime lives, registers the
DLL search directory (Windows loads extension-module dependencies from
registered ``os.add_dll_directory`` entries only, not from ``PATH``), pins the
runtime data root, and then loads the ``_physicsapp`` extension module.

An installed wheel bundles everything inside the package and needs no
configuration. A development checkout imports this package straight from
``app/python/src`` (via ``PYTHONPATH``), where the compiled extension does not
live; it is then found next to the engine DLLs in the build tree's
per-configuration output directory (``build/msvc/bin/<config>``). Set
``NEEDRENAME_BINDINGS_DIR`` only for layouts that put it somewhere else.
"""

from __future__ import annotations

import importlib.machinery
import importlib.util
import os
import sys
from pathlib import Path

__version__ = "0.1.0"

#: Environment variable naming the directory that holds the engine DLLs.
RUNTIME_DIR_ENV_VAR = "NEEDRENAME_RUNTIME_DIR"
#: Environment variable naming the runtime data root (builtin_assets/, projects/).
DATA_ROOT_ENV_VAR = "NEEDRENAME_DATA_ROOT"
#: Environment variable overriding where the compiled extension is looked up.
#: Needed only when the extension is not beside the engine DLLs or in an
#: installed prefix; an installed wheel bundles it inside the package.
BINDINGS_DIR_ENV_VAR = "NEEDRENAME_BINDINGS_DIR"

#: Basename of the compiled extension module (`_physicsapp*.pyd` / `.so`).
_EXTENSION_NAME = "_physicsapp"

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


def _find_extension(directory: Path) -> Path | None:
    """Return the compiled extension inside `directory`, or None when it is absent."""
    for suffix in importlib.machinery.EXTENSION_SUFFIXES:
        candidate = directory / f"{_EXTENSION_NAME}{suffix}"
        if candidate.is_file():
            return candidate
    return None


def _resolve_extension(runtime_dir: Path, data_root: Path) -> tuple[Path | None, Path | None]:
    """Locate the compiled extension, returning (directory, module file).

    The explicit override wins; otherwise the search covers, in order, the
    package itself (wheel), the engine DLL directory (build tree, where the
    extension is linked beside the DLLs and both are per-configuration), and an
    installed prefix's `python/` directory.
    """
    configured = os.environ.get(BINDINGS_DIR_ENV_VAR)
    if configured:
        directory = Path(configured).expanduser()
        return directory, _find_extension(directory)

    for directory in (_PACKAGE_DIR, runtime_dir, data_root / "python"):
        module_path = _find_extension(directory)
        if module_path is not None:
            return directory, module_path
    return None, None


def _raise_missing_extension(bindings_dir: Path | None) -> None:
    if bindings_dir is None:
        problems = (
            f"{BINDINGS_DIR_ENV_VAR} is not set and no {_EXTENSION_NAME} extension was found "
            "in the package, beside the engine DLLs, or under <data root>/python"
        )
    else:
        problems = f"{BINDINGS_DIR_ENV_VAR}={bindings_dir} holds no {_EXTENSION_NAME} extension"

    raise ImportError(
        "needrename_sim cannot locate the compiled _physicsapp extension.\n"
        f"  Set {BINDINGS_DIR_ENV_VAR} to the directory holding _physicsapp*.pyd "
        "(a build tree's bin/Debug contains it beside the engine DLLs).\n"
        "  An installed wheel bundles the extension inside the package and needs no "
        f"variable. Problems: {problems}."
    )


def _load_extension(bindings_dir: Path, module_path: Path):
    """Import `_physicsapp` from the package directory or from an explicit file."""
    if bindings_dir == _PACKAGE_DIR:
        from . import _physicsapp

        return _physicsapp

    # One extension instance per process: a second load would register every
    # nanobind type twice.
    existing = sys.modules.get(_EXTENSION_NAME)
    if existing is not None:
        return existing

    spec = importlib.util.spec_from_file_location(_EXTENSION_NAME, module_path)
    if spec is None or spec.loader is None:
        _raise_missing_extension(bindings_dir)
    module = importlib.util.module_from_spec(spec)
    sys.modules[_EXTENSION_NAME] = module
    try:
        spec.loader.exec_module(module)
    except BaseException:
        sys.modules.pop(_EXTENSION_NAME, None)
        raise
    return module


_runtime_dir = _resolve_location(RUNTIME_DIR_ENV_VAR, _BUNDLED_RUNTIME_DIR)
_data_root = _resolve_location(DATA_ROOT_ENV_VAR, _BUNDLED_DATA_ROOT)
if _runtime_dir is None or _data_root is None or not _runtime_dir.is_dir() or not _data_root.is_dir():
    _raise_missing(_runtime_dir, _data_root)

_bindings_dir, _extension_path = _resolve_extension(_runtime_dir, _data_root)
if _extension_path is None:
    _raise_missing_extension(_bindings_dir)

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
_physicsapp = _load_extension(_bindings_dir, _extension_path)
# Register the extension as a package submodule so `from . import _physicsapp`
# and `from ._physicsapp import ...` behave exactly like a wheel install, no
# matter which directory it was loaded from.
sys.modules[f"{__name__}.{_EXTENSION_NAME}"] = _physicsapp

from ._physicsapp import *  # noqa: E402,F401,F403

from . import robot  # noqa: E402
from .robot import Robot  # noqa: E402

__all__ = [
    "__version__",
    "RUNTIME_DIR_ENV_VAR",
    "DATA_ROOT_ENV_VAR",
    "BINDINGS_DIR_ENV_VAR",
    "Robot",
    "robot",
    *[name for name in dir(_physicsapp) if not name.startswith("_")],
]
