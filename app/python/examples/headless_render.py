"""Headless offscreen rendering: run the robot and read frames back as numpy.

Usage:
    python examples/headless_render.py <robot.urdf> [--frames N] [--out DIR]

No window is created: the app renders into an internal target and hands the
pixels to Python as a zero-copy numpy view, which this script inspects and saves
as a PPM image (no image-library dependency).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

import needrename_sim as sim


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("urdf", type=Path, help="path to the robot's URDF file")
    parser.add_argument("--frames", type=int, default=8, help="number of frames to render")
    parser.add_argument("--steps", type=int, default=4, help="physics steps per rendered frame")
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=240)
    parser.add_argument("--out", type=Path, default=Path("frames"), help="output directory for PPM frames")
    return parser.parse_args(argv)


def write_ppm(path: Path, pixels: np.ndarray) -> None:
    """Write an (H, W, 4) uint8 RGBA array as a binary PPM (RGB) image."""
    rgb = np.ascontiguousarray(pixels[:, :, :3])
    height, width, _ = rgb.shape
    with path.open("wb") as handle:
        handle.write(f"P6\n{width} {height}\n255\n".encode("ascii"))
        handle.write(rgb.tobytes())


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.urdf.is_file():
        print(f"error: URDF not found: {args.urdf}", file=sys.stderr)
        return 2

    app = sim.PhysicsApp.create(
        sim.CreateInfo(mode=sim.AppMode.offscreen, resol_x=args.width, resol_y=args.height)
    )
    app.add_box(
        sim.BoxDesc(position=sim.Vec3(0.0, 0.0, -0.05), half_extents=sim.Vec3(10.0, 10.0, 0.05),
                    mass=0.0, kinematic=True, color="white")
    )
    app.add_directional_light(sim.DirectionalLightParams(direction=sim.Vec3(0.4, 0.3, -1.0), intensity=1.6))
    app.set_camera_pose(sim.Vec3(2.2, -2.2, 1.8), sim.Vec3(0.0, 0.0, 0.4))

    result = app.load_urdf(sim.UrdfImportConfig(urdf_path=args.urdf, position=sim.Vec3(0.0, 0.0, 0.8)))
    # DC-motor actuators with the A1 joint-side parameters the C++ stand demo uses.
    for joint in result.joint_bodies.values():
        app.add_actuator(joint.id, sim.DcMotorActuator())
    app.commit_scene()
    app.set_render_readback_enabled(True)

    args.out.mkdir(parents=True, exist_ok=True)
    for frame in range(args.frames):
        for _ in range(args.steps):
            app.step()
        app.render_next_frame()
        output = app.get_render_output()
        pixels = output.pixels  # zero-copy view: (H, W, 4) uint8
        path = args.out / f"frame_{frame:04d}.ppm"
        write_ppm(path, pixels)
        print(
            f"frame {frame}: {pixels.shape[1]}x{pixels.shape[0]} "
            f"mean={pixels[:, :, :3].mean():.1f} max={int(pixels[:, :, :3].max())} -> {path}"
        )

    print(f"rendered {args.frames} frames into {args.out} (no window created)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
