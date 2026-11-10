"""URDF import in physics-only mode: name maps, joint states, limit data."""

from __future__ import annotations

from pathlib import Path

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO_TEMPLATE = """
import json
import needrename_sim as s

URDF = "{urdf}"

def caught(fn):
    try:
        fn()
    except BaseException as exc:  # noqa: BLE001
        return type(exc).__name__
    return "none"

def main():
    observed = {{}}
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    observed["missing_file"] = caught(
        lambda: app.load_urdf(s.UrdfImportConfig(urdf_path=URDF + ".missing"))
    )

    result = app.load_urdf(s.UrdfImportConfig(urdf_path=URDF, position=s.Vec3(0.0, 0.0, 1.0)))
    observed["link_names"] = sorted(result.link_bodies)
    observed["joint_names"] = sorted(result.joint_bodies)

    joint1 = result.joint_bodies["joint1"]
    observed["joint1_parent"] = result.link_bodies["base_link"] == joint1.parent
    observed["joint1_child"] = result.link_bodies["link1"] == joint1.child

    limits = app.get_joint_limits(joint1.id)
    observed["joint1_limits"] = [limits.lower, limits.upper, limits.effort, limits.velocity]

    app.commit_scene()
    state = app.get_joint_state(joint1.id)
    observed["angle_at_commit"] = state.angle
    observed["velocity_at_commit"] = state.angular_velocity

    robot = s.Robot(app, result)
    observed["robot_joint_names"] = sorted(robot.joint_names)
    observed["robot_link_names"] = sorted(robot.link_names)
    observed["robot_joint_states_shape"] = list(robot.joint_states().shape)
    observed["robot_joint_angles"] = robot.joint_angles().tolist()
    observed["robot_link_id_roundtrip"] = robot.link_id("link2") == result.link_bodies["link2"]
    observed["robot_unknown_joint"] = caught(lambda: robot.joint_id("no_such_joint"))

    print("RESULT:" + json.dumps(observed))

main()
"""


def test_urdf_import_physics_only(minimal_urdf: Path) -> None:
    result = run_scenario(SCENARIO_TEMPLATE.format(urdf=minimal_urdf.as_posix()))
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["missing_file"] == "RuntimeError"
    assert observed["link_names"] == ["base_link", "link1", "link2"]
    assert observed["joint_names"] == ["joint1", "joint2"]
    assert observed["joint1_parent"] is True
    assert observed["joint1_child"] is True

    lower, upper, effort, velocity = observed["joint1_limits"]
    assert lower == -1.5 and upper == 1.5
    assert effort == 10.0 and velocity == 5.0

    assert abs(observed["angle_at_commit"]) < 1e-6
    assert abs(observed["velocity_at_commit"]) < 1e-6

    assert observed["robot_joint_names"] == ["joint1", "joint2"]
    assert observed["robot_link_names"] == ["base_link", "link1", "link2"]
    assert observed["robot_joint_states_shape"] == [2, 2]
    assert observed["robot_joint_angles"] == [0.0, 0.0]
    assert observed["robot_link_id_roundtrip"] is True
    assert observed["robot_unknown_joint"] == "KeyError"
