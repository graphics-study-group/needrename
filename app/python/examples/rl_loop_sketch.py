"""RL-style loop sketch: episodes, decimated control ticks, snapshot resets.

Usage:
    python examples/rl_loop_sketch.py <robot.urdf> [--episodes N] [--steps N]

The "policy" here is a trivial sinusoidal target pattern driven through the
robot helper's batch target API; the point is the loop structure an RL user
needs: one app per worker process, an episode snapshot, control ticks decoupled
from physics ticks, a per-episode reward, and `robot.reset()` between episodes.
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import numpy as np

import needrename_sim as sim


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("urdf", type=Path, help="path to the robot's URDF file")
    parser.add_argument("--episodes", type=int, default=3, help="number of episodes to run")
    parser.add_argument("--steps", type=int, default=120, help="physics steps per episode")
    parser.add_argument("--control-decimation", type=int, default=4, help="physics steps per control tick")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if not args.urdf.is_file():
        print(f"error: URDF not found: {args.urdf}", file=sys.stderr)
        return 2

    app = sim.PhysicsApp.create(sim.CreateInfo(mode=sim.AppMode.physics_only))
    result = app.load_urdf(sim.UrdfImportConfig(urdf_path=args.urdf, position=sim.Vec3(0.0, 0.0, 1.0)))
    for joint in result.joint_bodies.values():
        app.add_actuator(joint.id, sim.PdActuator(20.0, 1.0))

    robot = sim.Robot(app, result)
    robot.set_target_angles({name: 0.0 for name in robot.joint_names})
    app.commit_scene()

    # Episode start state, captured once after the scene is committed.
    robot.snapshot()
    start_height = float(np.mean([app.get_body_state(body).position.z for body in robot.link_bodies.values()]))

    for episode in range(args.episodes):
        total_reward = 0.0
        ticks = 0
        for step in range(args.steps):
            if step % args.control_decimation == 0:
                # Control tick: a smooth target pattern, applied in one batch.
                phase = 2.0 * math.pi * (step / max(1, args.steps)) + episode
                targets = {
                    name: 0.3 * math.sin(phase + index)
                    for index, name in enumerate(robot.joint_names)
                }
                robot.set_target_angles(targets)
                ticks += 1
            app.step()
            # Toy reward: keep the robot upright and near its start height.
            height = float(
                np.mean([app.get_body_state(body).position.z for body in robot.link_bodies.values()])
            )
            total_reward += 1.0 - abs(height - start_height)

        angles = robot.joint_angles()
        print(
            f"episode {episode}: steps={args.steps} ticks={ticks} "
            f"reward={total_reward:+.2f} angle_rms={float(np.sqrt(np.mean(angles**2))):.3f}"
        )
        robot.reset()

    print("done (each episode starts from the recorded snapshot)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
