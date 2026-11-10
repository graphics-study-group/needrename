"""Inertia invertibility: a body with a small but valid inertia must still rotate.

Regression for a body whose inertia determinant is far below 1 but perfectly invertible at
its own scale. The Unitree A1's foot (0.06 kg, 2 cm sphere, diag(9.6e-6), determinant
8.85e-16) used to fall below the engine's absolute `det > 1e-12` test, which zeroed its
inverse inertia. A foot that cannot rotate also freezes the calf it is welded to by a fixed
joint, so the whole lower leg stopped swinging and the robot could not walk.
"""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO = """
import json
import needrename_sim as s

def main():
    observed = {}
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    result = app.load_urdf(s.UrdfImportConfig(urdf_path=r"{urdf}", position=s.Vec3(0.0, 0.0, 5.0)))
    joint = result.joint_bodies["ball_joint"].id
    app.commit_scene()

    # A constant torque about the joint axis must spin the tiny-inertia link.
    for _ in range(40):
        app.add_joint_torque(joint, 0.5)
        app.step()
    state = app.get_joint_state(joint)
    observed["angle"] = state.angle
    observed["omega"] = state.angular_velocity

    print("RESULT:" + json.dumps(observed))

main()
"""


def test_small_inertia_link_rotates(tiny_inertia_urdf) -> None:
    scenario = SCENARIO.replace("{urdf}", str(tiny_inertia_urdf).replace("\\", "\\\\"))
    result = run_scenario(scenario)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    # 0.5 N*m on diag(9.6e-6) for 0.2 s: alpha ~ 52 rad/s^2 -> ~10 rad/s, angle ~1 rad.
    assert abs(observed["omega"]) > 1.0, f"tiny-inertia link did not rotate: {observed}"
    assert abs(observed["angle"]) > 0.05, observed


ASYMMETRIC_INERTIA_SCENARIO = """
import json
import needrename_sim as s


def main():
    app = s.PhysicsApp.create(
        s.CreateInfo(mode=s.AppMode.physics_only, physics_time_step=0.005)
    )
    result = app.load_urdf(
        s.UrdfImportConfig(urdf_path=r"{urdf}", position=s.Vec3(0.0, 0.0, 5.0))
    )
    joint = result.joint_bodies["joint"].id
    app.commit_scene()
    for _ in range(20):
        app.add_joint_torque(joint, 1.0)
        app.step()
    state = app.get_joint_state(joint)
    print("RESULT:" + json.dumps({"angle": state.angle, "omega": state.angular_velocity}))


main()
"""


def test_urdf_inertia_uses_engine_coordinate_frame(asymmetric_inertia_urdf) -> None:
    """URDF +Y is engine -X, so the child's engine X inertia must be Iyy=4."""
    scenario = ASYMMETRIC_INERTIA_SCENARIO.replace(
        "{urdf}", str(asymmetric_inertia_urdf).replace("\\", "\\\\")
    )
    result = run_scenario(scenario)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert 0.020 < abs(observed["omega"]) < 0.030, observed


NO_COLLISION_INERTIA_SCENARIO = """
import json
import needrename_sim as s


def main():
    app = s.PhysicsApp.create(
        s.CreateInfo(mode=s.AppMode.physics_only, physics_time_step=0.005)
    )
    result = app.load_urdf(
        s.UrdfImportConfig(urdf_path=r"{urdf}", position=s.Vec3(0.0, 0.0, 5.0))
    )
    body = result.link_bodies["body"]
    app.commit_scene()
    for _ in range(20):
        app.set_body_value(body, s.BodyField.external_torque, s.Vec4(0.0, 0.0, 1.0, 0.0))
        app.step()
    state = app.get_body_state(body)
    print("RESULT:" + json.dumps({"omega": state.angular_velocity.z}))


main()
"""


def test_manual_inertia_does_not_require_a_collision_shape(no_collision_inertia_urdf) -> None:
    scenario = NO_COLLISION_INERTIA_SCENARIO.replace(
        "{urdf}", str(no_collision_inertia_urdf).replace("\\", "\\\\")
    )
    result = run_scenario(scenario)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert 0.09 < abs(observed["omega"]) < 0.11, observed
