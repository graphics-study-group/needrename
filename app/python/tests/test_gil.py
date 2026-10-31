"""Blocking calls release the GIL: another Python thread runs during `step`.

The scene also carries a Python-defined actuator, so the test covers both sides
of the GIL contract: `step` releases it, and the actuator trampoline re-acquires
it around the Python callback.
"""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO = """
import json
import sys
import threading
import needrename_sim as s

class Counter(s.Actuator):
    calls = 0

    def compute_torque(self, angle, angular_velocity):
        Counter.calls += 1
        return -5.0 * angle - 0.5 * angular_velocity

def main():
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    parent = app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, -0.25), half_extents=s.Vec3(0.5, 0.5, 0.5),
                  mass=0.0, kinematic=True)
    )
    child = app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, 0.25), half_extents=s.Vec3(0.5, 0.5, 0.5))
    )
    joint = app.add_hinge_joint(
        parent, child,
        s.HingeJointParams(axis_obj1=s.Vec3(0.0, 0.0, 1.0), anchor_obj1=s.Vec3(0.0, 0.0, 0.5))
    )
    app.add_actuator(joint, Counter())
    app.commit_scene()

    # A long switch interval removes the interpreter's periodic thread switch,
    # so a plain counter thread can only make progress while `step` explicitly
    # releases the GIL.
    original_interval = sys.getswitchinterval()
    sys.setswitchinterval(0.2)

    counter = [0]
    stop = threading.Event()

    def worker():
        while not stop.is_set():
            counter[0] += 1

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    try:
        gains = []
        for _ in range(10):
            before = counter[0]
            app.step()
            gains.append(counter[0] - before)
    finally:
        stop.set()
        thread.join(timeout=5.0)
        sys.setswitchinterval(original_interval)

    print(
        "RESULT:"
        + json.dumps(
            {
                "gains": gains,
                "steps_with_progress": sum(1 for gain in gains if gain > 0),
                "steps": len(gains),
                "step_id": app.step_id,
                "actuator_calls": Counter.calls,
            }
        )
    )

main()
"""


def test_thread_progresses_while_stepping() -> None:
    result = run_scenario(SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["steps"] == 10
    assert observed["step_id"] == 10
    # The worker can only run inside `step` when the GIL is released there; the
    # occasional miss is scheduler noise, a systemic failure would be 0.
    assert observed["steps_with_progress"] >= 7, observed
    # The Python actuator callback ran on every step despite the released GIL.
    assert observed["actuator_calls"] == observed["steps"], observed
