"""Wrong-phase and invalid-handle error translation.

Every case runs in one fresh process (one `PhysicsApp` per process) and reports
the Python exception type the binding raised.
"""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO = """
import json
import needrename_sim as s

def caught(fn):
    try:
        fn()
    except BaseException as exc:  # noqa: BLE001 - the exception type is the assertion
        return type(exc).__name__
    return "none"

def main():
    observed = {}
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    body = app.add_box(s.BoxDesc(position=s.Vec3(0.0, 0.0, 1.0), half_extents=s.Vec3(0.5, 0.5, 0.5)))

    # Building phase: drive-phase calls are phase errors.
    observed["step_before_commit"] = caught(app.step)
    observed["render_before_commit"] = caught(app.render_next_frame)
    observed["body_state_before_commit"] = caught(lambda: app.get_body_state(body))
    observed["body_states_before_commit"] = caught(app.get_body_states)
    observed["set_body_before_commit"] = caught(
        lambda: app.set_body_value(body, s.BodyField.position, s.Vec4(0.0, 0.0, 0.0, 0.0))
    )
    observed["joint_state_before_commit"] = caught(lambda: app.get_joint_state(0))
    observed["joint_torque_before_commit"] = caught(lambda: app.set_joint_torque(0, 1.0))

    app.commit_scene()

    # Drive phase: scene changes are rejected and invalid handles are IndexError.
    observed["commit_twice"] = caught(app.commit_scene)
    observed["add_box_after_commit"] = caught(lambda: app.add_box(s.BoxDesc()))
    observed["add_hinge_after_commit"] = caught(lambda: app.add_hinge_joint(body, body))
    observed["body_state_invalid_id"] = caught(lambda: app.get_body_state(9999))
    observed["set_body_invalid_id"] = caught(
        lambda: app.set_body_value(9999, s.BodyField.position, s.Vec4(0.0, 0.0, 0.0, 0.0))
    )
    observed["joint_state_invalid_id"] = caught(lambda: app.get_joint_state(9999))
    observed["joint_limits_invalid_id"] = caught(lambda: app.get_joint_limits(9999))
    observed["joint_torque_invalid_id"] = caught(lambda: app.set_joint_torque(9999, 1.0))
    observed["target_angle_invalid_id"] = caught(lambda: app.set_target_angle(9999, 0.0))

    print("RESULT:" + json.dumps(observed))

main()
"""


def test_phase_and_handle_errors_are_translated() -> None:
    result = run_scenario(SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    # std::logic_error -> RuntimeError
    for name in (
        "step_before_commit",
        "render_before_commit",
        "body_state_before_commit",
        "body_states_before_commit",
        "set_body_before_commit",
        "joint_state_before_commit",
        "joint_torque_before_commit",
        "commit_twice",
        "add_box_after_commit",
        "add_hinge_after_commit",
    ):
        assert observed[name] == "RuntimeError", f"{name}: {observed[name]}"

    # std::out_of_range -> IndexError
    for name in (
        "body_state_invalid_id",
        "set_body_invalid_id",
        "joint_state_invalid_id",
        "joint_limits_invalid_id",
        "joint_torque_invalid_id",
        "target_angle_invalid_id",
    ):
        assert observed[name] == "IndexError", f"{name}: {observed[name]}"
