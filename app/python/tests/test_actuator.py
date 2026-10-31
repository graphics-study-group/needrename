"""Python-defined actuators: a subclass drives a hinge joint during `step`."""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

SCENARIO = """
import json
import needrename_sim as s

class Proportional(s.Actuator):
    \"\"\"Python PD law, registered on the joint and driven by `step`.\"\"\"

    calls = 0
    last_angle = None
    last_angular_velocity = None

    def __init__(self, kp, kd, target):
        super().__init__()
        self.kp = kp
        self.kd = kd
        self.set_target_angle(target)

    def compute_torque(self, angle, angular_velocity):
        Proportional.calls += 1
        Proportional.last_angle = angle
        Proportional.last_angular_velocity = angular_velocity
        return self.kp * (self.get_target_angle() - angle) - self.kd * angular_velocity

def main():
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    parent = app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, -0.25), half_extents=s.Vec3(0.2, 0.2, 0.2),
                  mass=0.0, kinematic=True)
    )
    child = app.add_box(
        s.BoxDesc(position=s.Vec3(0.0, 0.0, 0.25), half_extents=s.Vec3(0.2, 0.2, 0.2))
    )
    joint = app.add_hinge_joint(
        parent, child, s.HingeJointParams(axis_obj1=s.Vec3(0.0, 0.0, 1.0),
                                          anchor_obj1=s.Vec3(0.0, 0.0, 0.5))
    )
    app.add_actuator(joint, Proportional(20.0, 1.0, 0.5))
    app.commit_scene()

    for _ in range(60):
        app.step()
    angle_at_python_target = app.get_joint_state(joint).angle

    # Target set through the app reaches the Python override.
    app.set_target_angle(joint, -0.4)
    for _ in range(80):
        app.step()
    angle_at_app_target = app.get_joint_state(joint).angle

    print(
        "RESULT:"
        + json.dumps(
            {
                "calls": Proportional.calls,
                "angle_at_python_target": angle_at_python_target,
                "angle_at_app_target": angle_at_app_target,
                "saw_angular_velocity": Proportional.last_angular_velocity is not None,
            }
        )
    )

main()
"""


def test_python_actuator_drives_the_joint() -> None:
    result = run_scenario(SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["calls"] >= 140, "the callback must run on every step"
    assert observed["saw_angular_velocity"] is True
    # The joint is driven to the target the Python object set itself ...
    assert abs(observed["angle_at_python_target"] - 0.5) < 0.05, observed
    # ... and to a target set through `PhysicsApp.set_target_angle`.
    assert abs(observed["angle_at_app_target"] + 0.4) < 0.05, observed
