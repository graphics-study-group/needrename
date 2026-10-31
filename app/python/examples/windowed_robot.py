"""Windowed robot demo: load a URDF, drive it with PD actuators, render a window.

Usage:
    python examples/windowed_robot.py <robot.urdf> [--frames N]

`--frames N` stops after N frames instead of waiting for the window to close,
which is what the test-suite smoke run does.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import needrename_sim as sim


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("urdf", type=Path, help="path to the robot's URDF file")
    parser.add_argument("--frames", type=int, default=0, help="stop after N frames (0 = until window close)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    return parser.parse_args(argv)


def build_scene(app: sim.PhysicsApp, urdf: Path) -> sim.Robot:
    # A large flat ground plane (the world is Z-up) and a sun.
    app.add_box(
        sim.BoxDesc(position=sim.Vec3(0.0, 0.0, -0.05), half_extents=sim.Vec3(10.0, 10.0, 0.05),
                    mass=0.0, kinematic=True, color="white")
    )
    app.add_directional_light(
        sim.DirectionalLightParams(direction=sim.Vec3(0.4, 0.3, -1.0), intensity=1.6)
    )

    app.set_camera_pose(sim.Vec3(2.2, -2.2, 1.8), sim.Vec3(0.0, 0.0, 0.4))
    result = app.load_urdf(sim.UrdfImportConfig(urdf_path=urdf, position=sim.Vec3(0.0, 0.0, 0.8)))

    # One DC-motor actuator per joint, holding the imported pose: the same A1
    # joint-side parameters the C++ actuator stand demo uses (its constructor
    # defaults). Registration shares ownership with the app, so this dict stays
    # usable for live tuning while the actuator is registered
    # (`app.get_actuator(id)` returns the same object).
    actuators: dict[int, sim.DcMotorActuator] = {}
    for joint_id in result.joint_bodies.values():
        actuator = sim.DcMotorActuator()
        app.add_actuator(joint_id.id, actuator)
        app.set_target_angle(joint_id.id, 0.0)
        actuators[joint_id.id] = actuator

    robot = sim.Robot(app, result)
    print(f"loaded {urdf.name}: {len(robot.link_names)} links, {len(robot.joint_names)} joints")
    return robot


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.urdf.is_file():
        print(f"error: URDF not found: {args.urdf}", file=sys.stderr)
        return 2

    app = sim.PhysicsApp.create(
        sim.CreateInfo(mode=sim.AppMode.windowed, resol_x=args.width, resol_y=args.height,
                       window_title=f"needrename-sim - {args.urdf.stem}")
    )
    robot = build_scene(app, args.urdf)
    app.commit_scene()
    app.resume()

    print("window open: close it to exit (SPACE pauses the simulation)")
    frames = 0
    while not app.should_quit:
        if not app.is_paused:
            app.step()
        app.render_next_frame()
        frames += 1
        if args.frames and frames >= args.frames:
            break
        if frames % 60 == 0:
            angles = ", ".join(f"{value:+.3f}" for value in robot.joint_angles())
            print(f"frame {frames}: joint angles [{angles}]")

    print(f"closed after {frames} frames")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
