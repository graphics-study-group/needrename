"""Actuator registration from Python: identity, live tuning, and removal.

Every scenario runs in a fresh interpreter, because the engine allows exactly one
`PhysicsApp` per process.
"""

from __future__ import annotations

from conftest import assert_scenario_passed, run_scenario, scenario_result

# Shared scene builder for the scenarios below: a kinematic parent box, a dynamic
# child box, and a Z-axis hinge at the child's center.
_HELPERS = """
import json
import needrename_sim as s

def caught(fn):
    try:
        fn()
    except BaseException as exc:  # noqa: BLE001
        return type(exc).__name__
    return "none"

def add_hinge(app, origin, parent_kinematic=True):
    parent = app.add_box(
        s.BoxDesc(position=s.Vec3(origin[0], origin[1], origin[2] - 0.25),
                  half_extents=s.Vec3(0.2, 0.2, 0.2),
                  mass=0.0 if parent_kinematic else 1.0, kinematic=parent_kinematic)
    )
    child = app.add_box(
        s.BoxDesc(position=s.Vec3(origin[0], origin[1], origin[2] + 0.25),
                  half_extents=s.Vec3(0.2, 0.2, 0.2))
    )
    joint = app.add_hinge_joint(
        parent, child,
        s.HingeJointParams(axis_obj1=s.Vec3(0.0, 0.0, 1.0), anchor_obj1=s.Vec3(0.0, 0.0, 0.5))
    )
    return joint
"""

IDENTITY_SCENARIO = _HELPERS + """
class Proportional(s.Actuator):
    \"\"\"Python PD law registered without any wrapper object.\"\"\"

    calls = 0

    def __init__(self, kp, kd, target):
        super().__init__()
        self.kp = kp
        self.kd = kd
        self.set_target_angle(target)

    def compute_torque(self, angle, angular_velocity):
        Proportional.calls += 1
        return self.kp * (self.get_target_angle() - angle) - self.kd * angular_velocity

def main():
    observed = {}
    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    joint = add_hinge(app, (0.0, 0.0, 0.0))

    act = Proportional(20.0, 1.0, 0.5)
    act_id = id(act)
    app.add_actuator(joint, act)
    observed["identity"] = app.get_actuator(joint) is act

    # The app's share must keep the object (and its Python class) alive even when
    # the caller drops its own reference.
    del act
    import gc
    gc.collect()
    held = app.get_actuator(joint)
    observed["survives_dropped_reference"] = held is not None
    observed["same_object"] = id(held) == act_id
    observed["type"] = type(held).__name__

    app.commit_scene()
    for _ in range(60):
        app.step()
    observed["angle_at_target"] = app.get_joint_state(joint).angle

    # A target set through the app lands on the registered Python instance.
    app.set_target_angle(joint, -0.4)
    for _ in range(80):
        app.step()
    observed["angle_at_app_target"] = app.get_joint_state(joint).angle
    observed["calls"] = Proportional.calls

    # Removal hands the torque channel back to manual control, and the removed
    # instance stays usable from Python.
    app.remove_actuator(joint)
    observed["removed"] = app.get_actuator(joint) is None
    observed["removed_target_readable"] = held.get_target_angle()
    observed["manual_torque_after_removal"] = caught(lambda: app.set_joint_torque(joint, 2.0))
    app.step()
    observed["manual_omega"] = app.get_joint_state(joint).angular_velocity
    observed["second_removal"] = caught(lambda: app.remove_actuator(joint))

    # Registration and removal after `commit_scene` are legal and take effect on
    # the next step.
    dc = s.DcMotorActuator()
    dc.set_target_angle(-0.3)
    observed["post_commit_add"] = caught(lambda: app.add_actuator(joint, dc))
    observed["post_commit_identity"] = app.get_actuator(joint) is dc
    for _ in range(80):
        app.step()
    observed["angle_after_post_commit_add"] = app.get_joint_state(joint).angle
    observed["post_commit_remove"] = caught(lambda: app.remove_actuator(joint))
    observed["post_commit_removed"] = app.get_actuator(joint) is None

    print("RESULT:" + json.dumps(observed))

main()
"""

TUNING_SCENARIO = _HELPERS + """
def main():
    observed = {}
    defaults = s.DcMotorActuator()
    observed["defaults"] = [defaults.kp, defaults.kd]

    app = s.PhysicsApp.create(s.CreateInfo(mode=s.AppMode.physics_only))
    joint = add_hinge(app, (0.0, 0.0, 0.0))

    # Start with gains that produce no torque, so the joint cannot move.
    dc = s.DcMotorActuator(0.0, 0.0)
    dc.set_target_angle(0.6)
    app.add_actuator(joint, dc)
    observed["identity"] = app.get_actuator(joint) is dc
    app.commit_scene()

    for _ in range(40):
        app.step()
    observed["angle_weak_gain"] = app.get_joint_state(joint).angle

    # Live tuning: writing the gains changes the motion on the next steps.
    dc.kp = 20.0
    dc.kd = 1.0
    registered = app.get_actuator(joint)
    observed["tuned_kp"] = registered.kp
    observed["tuned_kd"] = registered.kd
    for _ in range(80):
        app.step()
    observed["angle_strong_gain"] = app.get_joint_state(joint).angle

    print("RESULT:" + json.dumps(observed))

main()
"""

DC_MOTOR_SCENARIO = _HELPERS + """
def main():
    observed = {}
    m = s.DcMotorActuator()
    observed["defaults"] = [m.kp, m.kd, m.stall_torque, m.no_load_speed, m.cont_torque, m.gear_ratio]

    m.kp = 1000.0
    m.kd = 0.0
    m.set_target_angle(0.1)
    observed["clipped_at_default_cont_torque"] = m.compute_torque(0.0, 0.0)

    m.cont_torque = 5.0
    observed["clipped_at_tuned_cont_torque"] = m.compute_torque(0.0, 0.0)

    m.stall_torque = 40.0
    m.no_load_speed = 30.0
    m.gear_ratio = 2.0
    observed["tuned"] = [m.stall_torque, m.no_load_speed, m.cont_torque, m.gear_ratio]

    print("RESULT:" + json.dumps(observed))

main()
"""


def test_registered_python_actuator_is_the_callers_object() -> None:
    result = run_scenario(IDENTITY_SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    # Identity, and survival of the caller dropping its reference.
    assert observed["identity"] is True
    assert observed["survives_dropped_reference"] is True
    assert observed["same_object"] is True
    assert observed["type"] == "Proportional"

    # It really drives the joint (no wrapper object in between).
    assert observed["calls"] >= 140, "the Python override must run on every step"
    assert abs(observed["angle_at_target"] - 0.5) < 0.05, observed
    assert abs(observed["angle_at_app_target"] + 0.4) < 0.05, observed

    # Removal detaches it, hands the channel back, and keeps the instance usable.
    assert observed["removed"] is True
    assert abs(observed["removed_target_readable"] + 0.4) < 1e-5
    assert observed["manual_torque_after_removal"] == "none"
    assert observed["manual_omega"] > 0.0
    assert observed["second_removal"] == "RuntimeError"

    # Post-commit registration drives from the next step, and removal works too.
    assert observed["post_commit_add"] == "none"
    assert observed["post_commit_identity"] is True
    assert abs(observed["angle_after_post_commit_add"] + 0.3) < 0.05, observed
    assert observed["post_commit_remove"] == "none"
    assert observed["post_commit_removed"] is True


def test_live_parameter_tuning_changes_the_motion() -> None:
    result = run_scenario(TUNING_SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    assert observed["defaults"] == [25.0, 0.5]
    assert observed["identity"] is True
    # Zero gains: the target is never reached ...
    assert abs(observed["angle_weak_gain"]) < 0.02, observed
    # ... the write is visible on the registered instance ...
    assert observed["tuned_kp"] == 20.0
    assert observed["tuned_kd"] == 1.0
    # ... and the next steps drive the joint to the target.
    assert abs(observed["angle_strong_gain"] - 0.6) < 0.05, observed


def test_dc_motor_parameters_are_readable_and_writable() -> None:
    result = run_scenario(DC_MOTOR_SCENARIO)
    assert_scenario_passed(result)
    observed = scenario_result(result)

    defaults = observed["defaults"]
    assert defaults[:4] == [25.0, 0.5, 33.5, 21.0]
    assert abs(defaults[4] - 13.4) < 1e-5
    assert defaults[5] == 1.0
    assert abs(observed["clipped_at_default_cont_torque"] - 13.4) < 1e-4
    assert abs(observed["clipped_at_tuned_cont_torque"] - 5.0) < 1e-4
    assert observed["tuned"] == [40.0, 30.0, 5.0, 2.0]
