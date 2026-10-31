"""Import-time DLL/data discovery.

Covers the environment-variable path, a real physics-only run through it, and a
re-import in a spawned worker process (the multiprocessing pattern the package
documentation recommends for parallel environments).
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from conftest import assert_scenario_passed, run_scenario, scenario_result

DISCOVERY_SCENARIO = """
import json
import os
import needrename_sim as s

def main():
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, 2.0), half_extents=s.Vec3(0.5, 0.5, 0.5))
    )
    app.commit_scene()
    app.step()
    print(
        "RESULT:"
        + json.dumps(
            {
                "runtime_dir": str(s._runtime_dir),
                "data_root": str(s._data_root),
                "env_runtime_dir": os.environ.get("NEEDRENAME_RUNTIME_DIR"),
                "env_data_root": os.environ.get("NEEDRENAME_DATA_ROOT"),
                "version": s.__version__,
                "step_id": app.step_id,
            }
        )
    )

main()
"""

SPAWN_SCENARIO = """
import json
import multiprocessing
import needrename_sim as s

def worker(queue):
    # A spawned worker re-imports the package; discovery must behave exactly as
    # in the parent process (no once-only state).
    import needrename_sim as worker_module

    app = worker_module.PhysicsApp.create(worker_module.CreateInfo(mode=worker_module.AppMode.physics_only))
    app.add_box(
        worker_module.BoxDesc(position=worker_module.Vec3(0.0, 0.0, 2.0),
                              half_extents=worker_module.Vec3(0.5, 0.5, 0.5))
    )
    app.commit_scene()
    app.step()
    queue.put([str(worker_module._runtime_dir), app.step_id])

def main():
    if not hasattr(s, "__file__"):
        raise SystemExit("package must be imported from a file")
    context = multiprocessing.get_context("spawn")
    queue = context.Queue()
    process = context.Process(target=worker, args=(queue,))
    process.start()
    runtime_dir, step_id = queue.get(timeout=300)
    process.join(timeout=300)
    print("RESULT:" + json.dumps({"runtime_dir": runtime_dir, "step_id": step_id,
                                  "exit_code": process.exitcode}))

if __name__ == "__main__":
    main()
"""


@pytest.fixture(scope="session")
def known_locations() -> tuple[str, str]:
    """A runtime directory and data root to feed the environment variables.

    A developer run uses the configured dev install; a wheel-only environment
    uses the package's own bundled directories. Either way the values are passed
    explicitly, so the test exercises the environment-variable path rather than
    the ambient environment.
    """
    runtime = os.environ.get("NEEDRENAME_RUNTIME_DIR")
    data_root = os.environ.get("NEEDRENAME_DATA_ROOT")
    if runtime and data_root:
        return runtime, data_root
    import needrename_sim as sim

    package_dir = Path(sim.__file__).resolve().parent
    return str(package_dir / "bin"), str(package_dir / "share" / "needrename")


def test_environment_variables_drive_discovery(known_locations: tuple[str, str]) -> None:
    runtime, data_root = known_locations
    result = run_scenario(
        DISCOVERY_SCENARIO,
        extra_env={"NEEDRENAME_RUNTIME_DIR": runtime, "NEEDRENAME_DATA_ROOT": data_root},
    )
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert os.path.normcase(observed["runtime_dir"]) == os.path.normcase(runtime)
    assert os.path.normcase(observed["data_root"]) == os.path.normcase(data_root)
    assert observed["step_id"] == 1
    assert observed["version"]


def test_spawned_worker_reimports_the_package(known_locations: tuple[str, str]) -> None:
    runtime, data_root = known_locations
    result = run_scenario(
        SPAWN_SCENARIO,
        extra_env={"NEEDRENAME_RUNTIME_DIR": runtime, "NEEDRENAME_DATA_ROOT": data_root},
    )
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["exit_code"] == 0
    assert observed["step_id"] == 1
    assert os.path.normcase(observed["runtime_dir"]) == os.path.normcase(runtime)
