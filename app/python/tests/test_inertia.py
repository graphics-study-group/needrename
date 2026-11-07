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
