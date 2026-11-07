"""Drive the Unitree A1 into its standing pose using only Python control code.

Usage:
    python examples/a1_stand.py [--frames N]

The default run opens a window until it is closed. `--frames 600` runs a finite
three-second smoke scenario and checks that the trunk stays upright and the
joints settle near their standing targets.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import needrename_sim as sim

REPO_ROOT = Path(__file__).resolve().parents[3]
A1_URDF = REPO_ROOT / "assets" / "a1_description" / "urdf" / "a1.urdf"
STANDING_TARGETS = {
    "FR_hip_joint": -0.1,
    "FR_thigh_joint": 0.8,
    "FR_calf_joint": -1.5,
    "FL_hip_joint": 0.1,
    "FL_thigh_joint": 0.8,
    "FL_calf_joint": -1.5,
    "RR_hip_joint": -0.1,
    "RR_thigh_joint": 1.0,
    "RR_calf_joint": -1.5,
    "RL_hip_joint": 0.1,
    "RL_thigh_joint": 1.0,
    "RL_calf_joint": -1.5,
}


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=0, help="stop after N frames (0 = until window close)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    return parser.parse_args(argv)


def build_scene(app: sim.PhysicsApp):
    app.add_box(
        sim.BoxDesc(
            position=sim.Vec3(0.0, 0.0, -0.5),
            half_extents=sim.Vec3(100.0, 100.0, 0.5),
            mass=0.0,
            kinematic=True,
            color="white",
        )
    )
    result = app.load_urdf(
        sim.UrdfImportConfig(
            urdf_path=A1_URDF,
            position=sim.Vec3(0.0, 0.0, 0.85),
            static_friction=0.5,
            dynamic_friction=0.5,
            restitution=0.0,
        )
    )

    missing = set(STANDING_TARGETS) - set(result.joint_bodies)
    if missing:
        raise RuntimeError(f"A1 stand: missing joints: {sorted(missing)}")
    for name, target in STANDING_TARGETS.items():
        joint_id = result.joint_bodies[name].id
        app.add_actuator(joint_id, sim.DcMotorActuator())
        app.set_target_angle(joint_id, target)

    app.add_directional_light(
        sim.DirectionalLightParams(direction=sim.Vec3(0.0, 0.0, -1.0), intensity=1.5, cast_shadow=True)
    )
    app.set_camera_pose(sim.Vec3(2.5, -2.5, 2.0), sim.Vec3(0.0, 0.0, 0.5))
    return result


def standing_metrics(app: sim.PhysicsApp, result) -> tuple[float, float, float, float]:
    trunk = app.get_body_state(result.link_bodies["trunk"])
    q = trunk.rotation
    up_z = 1.0 - 2.0 * (q.x * q.x + q.y * q.y)
    states = [app.get_joint_state(result.joint_bodies[name].id) for name in STANDING_TARGETS]
    max_error = max(
        abs(state.angle - target)
        for state, target in zip(states, STANDING_TARGETS.values())
    )
    peak_speed = max(abs(state.angular_velocity) for state in states)
    return trunk.position.z, up_z, max_error, peak_speed


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.frames < 0:
        print("error: --frames must be non-negative", file=sys.stderr)
        return 2

    app = sim.PhysicsApp.create(
        sim.CreateInfo(
            mode=sim.AppMode.windowed,
            resol_x=args.width,
            resol_y=args.height,
            window_title="needrename-sim - A1 stand",
            physics_time_step=0.005,
        )
    )
    result = build_scene(app)
    app.commit_scene()
    app.resume()

    print("window open: close it to exit (SPACE pauses the simulation)")
    frames = 0
    while not app.should_quit:
        app.step()
        app.render_next_frame()
        frames += 1
        if frames % 60 == 0 or frames == args.frames:
            height, up_z, max_error, peak_speed = standing_metrics(app, result)
            print(
                f"frame {frames}: trunk_z={height:.3f} up_z={up_z:+.3f} "
                f"max_joint_error={max_error:.3f} peak_speed={peak_speed:.3f}"
            )
        if args.frames and frames >= args.frames:
            break

    if args.frames >= 600:
        _, up_z, max_error, peak_speed = standing_metrics(app, result)
        if up_z < 0.0 or max_error > 0.5 or peak_speed > 2.0:
            print("error: A1 did not stay upright and settle at its standing targets", file=sys.stderr)
            return 1

    print(f"closed after {frames} frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
