"""`SetJointTorque` behaviour from Python, mirroring the C++ test."""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO = """
import json
import math
import needrename_sim as s

TORQUE = 2.0

def caught(fn):
    try:
        fn()
    except BaseException as exc:  # noqa: BLE001
        return type(exc).__name__
    return "none"

def add_assembly(app, origin, parent_kinematic):
    parent = app.add_box(
        s.BoxDesc(position=s.Vec3(origin.x, origin.y, origin.z - 0.25),
                  half_extents=s.Vec3(0.2, 0.2, 0.2),
                  mass=0.0 if parent_kinematic else 1.0, kinematic=parent_kinematic)
    )
    child = app.add_box(
        s.BoxDesc(position=s.Vec3(origin.x, origin.y, origin.z + 0.25),
                  half_extents=s.Vec3(0.2, 0.2, 0.2))
    )
    joint = app.add_hinge_joint(
        parent, child,
        s.HingeJointParams(axis_obj1=s.Vec3(0.0, 0.0, 1.0), anchor_obj1=s.Vec3(0.0, 0.0, 0.5))
    )
    return parent, child, joint

def main():
    observed = {}
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    manual = add_assembly(app, s.Vec3(0.0, 0.0, 0.0), False)
    actuated = add_assembly(app, s.Vec3(20.0, 0.0, 0.0), True)
    app.add_actuator(actuated[2], s.PdActuator(20.0, 1.0))
    app.set_target_angle(actuated[2], 0.5)

    observed["before_commit"] = caught(lambda: app.set_joint_torque(manual[2], TORQUE))
    app.commit_scene()
    observed["invalid_id"] = caught(lambda: app.set_joint_torque(12345, TORQUE))
    observed["actuated_joint"] = caught(lambda: app.set_joint_torque(actuated[2], TORQUE))

    # The actuator path keeps working while no manual torque has been set.
    for _ in range(60):
        app.step()
    observed["actuator_angle"] = app.get_joint_state(actuated[2]).angle
    observed["untouched_manual_omega"] = app.get_joint_state(manual[2]).angular_velocity

    # Positive torque accelerates about +axis and persists across steps.
    app.set_joint_torque(manual[2], TORQUE)
    app.step()
    omega_1 = app.get_joint_state(manual[2]).angular_velocity
    app.step()
    app.step()
    omega_3 = app.get_joint_state(manual[2]).angular_velocity
    observed["omega_1"] = omega_1
    observed["omega_3"] = omega_3

    child_omega = app.get_body_state(manual[1]).angular_velocity.z
    parent_omega = app.get_body_state(manual[0]).angular_velocity.z
    observed["child_omega"] = child_omega
    observed["parent_omega"] = parent_omega

    # Zeroing stops the acceleration.
    app.set_joint_torque(manual[2], 0.0)
    app.step()
    observed["omega_after_zero"] = app.get_joint_state(manual[2]).angular_velocity

    # A later SetBodyValue(ExternalTorque) wins for that body only.
    app.set_joint_torque(manual[2], TORQUE)
    app.set_body_value(manual[1], s.BodyField.external_torque, s.Vec4(0.0, 0.0, 0.0, 0.0))
    child_before = app.get_body_state(manual[1]).angular_velocity.z
    parent_before = app.get_body_state(manual[0]).angular_velocity.z
    app.step()
    app.step()
    observed["child_before"] = child_before
    observed["child_delta"] = app.get_body_state(manual[1]).angular_velocity.z - child_before
    observed["parent_delta"] = app.get_body_state(manual[0]).angular_velocity.z - parent_before

    print("RESULT:" + json.dumps(observed))

main()
"""


def test_set_joint_torque() -> None:
    result = run_scenario(SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["before_commit"] == "RuntimeError"
    assert observed["invalid_id"] == "IndexError"
    assert observed["actuated_joint"] == "RuntimeError"

    # The actuated joint still reaches its target, untouched by manual torque.
    assert observed["actuator_angle"] > 0.1
    assert abs(observed["untouched_manual_omega"]) < 1e-3

    # Torque accelerates about +axis and keeps applying on every step.
    assert observed["omega_1"] > 0.0
    assert observed["omega_3"] > 2.0 * observed["omega_1"]

    # Equal-and-opposite body torques on identical bodies counter-rotate them.
    assert observed["child_omega"] > 0.0
    assert observed["parent_omega"] < 0.0
    assert abs(observed["child_omega"] + observed["parent_omega"]) < 0.05 * abs(observed["child_omega"])

    # Zeroing the torque stops the acceleration.
    assert abs(observed["omega_after_zero"] - observed["omega_3"]) < 0.05 * abs(observed["omega_3"])

    # The later SetBodyValue write wins for the child while the parent keeps its
    # component of the joint torque.
    assert abs(observed["child_delta"]) < 0.05 * abs(observed["child_before"])
    assert observed["parent_delta"] < 0.0
