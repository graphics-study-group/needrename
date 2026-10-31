"""Robot-level convenience layer over a `PhysicsApp` URDF import.

`Robot` turns the name→id maps returned by `PhysicsApp.load_urdf` into the
operations a control loop performs every tick: reading joint states as numpy
arrays, setting batch target angles or joint torques by name, and restoring the
episode's initial snapshot before the next episode.

The helper owns no state of its own beyond the snapshot and the targets it has
applied; every operation goes through the public `PhysicsApp` API.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

import numpy as np

from . import _physicsapp as _ext

__all__ = ["Robot"]


def _as_target_mapping(
    values: Mapping[str, float] | Sequence[float],
    names: Sequence[str],
    what: str,
) -> dict[str, float]:
    """Accept either a name→value mapping or a sequence aligned with `names`."""
    if isinstance(values, Mapping):
        unknown = set(values) - set(names)
        if unknown:
            raise KeyError(f"{what}: unknown joint name(s) {sorted(unknown)}")
        return {name: float(value) for name, value in values.items()}
    values = list(values)
    if len(values) != len(names):
        raise ValueError(f"{what}: expected {len(names)} values, got {len(values)}")
    return {name: float(value) for name, value in zip(names, values)}


class Robot:
    """A URDF robot imported into a `PhysicsApp`, addressed by name.

    Parameters
    ----------
    app:
        The app the robot was imported into. It must be in the drive phase for
        state reads and reset to work.
    import_result:
        The `UrdfImportResult` returned by `PhysicsApp.load_urdf`.
    """

    def __init__(self, app: Any, import_result: Any) -> None:
        self._app = app
        self.link_bodies: dict[str, int] = dict(import_result.link_bodies)
        self.joint_ids: dict[str, int] = {
            name: pair.id for name, pair in import_result.joint_bodies.items()
        }
        self._link_names: list[str] = list(self.link_bodies)
        self._link_ids: list[int] = [self.link_bodies[name] for name in self._link_names]
        self._joint_names: list[str] = list(self.joint_ids)
        self._snapshot: _Snapshot | None = None
        # Targets this helper applied, so reset can restore them. Targets set
        # directly through the app are outside the helper's bookkeeping.
        self._targets: dict[str, float] = {}

    # ── Name lookups ────────────────────────────────────────────────────
    @property
    def link_names(self) -> list[str]:
        """Physical link names in import order."""
        return list(self._link_names)

    @property
    def joint_names(self) -> list[str]:
        """Physical joint names in import order."""
        return list(self._joint_names)

    def link_id(self, name: str) -> int:
        """Resolve a link name to its `BodyId`."""
        try:
            return self.link_bodies[name]
        except KeyError:
            raise KeyError(f"unknown link {name!r}; known links: {self._link_names}") from None

    def joint_id(self, joint: str | int) -> int:
        """Resolve a joint name (or pass a `JointId` through unchanged)."""
        if isinstance(joint, str):
            try:
                return self.joint_ids[joint]
            except KeyError:
                raise KeyError(
                    f"unknown joint {joint!r}; known joints: {self._joint_names}"
                ) from None
        return int(joint)

    # ── Joint state readback ────────────────────────────────────────────
    def joint_states(self) -> np.ndarray:
        """Return an `(n_joints, 2)` float64 array of `[angle, angular_velocity]`."""
        states = [self._app.get_joint_state(self.joint_ids[name]) for name in self._joint_names]
        return np.asarray([[state.angle, state.angular_velocity] for state in states], dtype=np.float64)

    def joint_angles(self) -> np.ndarray:
        """Return an `(n_joints,)` float64 array of joint angles [rad]."""
        return self.joint_states()[:, 0]

    def joint_velocities(self) -> np.ndarray:
        """Return an `(n_joints,)` float64 array of joint angular velocities [rad/s]."""
        return self.joint_states()[:, 1]

    def joint_limits(self) -> dict[str, Any]:
        """Return the recorded URDF limits per joint (data only, never enforced)."""
        return {name: self._app.get_joint_limits(joint_id) for name, joint_id in self.joint_ids.items()}

    # ── Batch control ───────────────────────────────────────────────────
    def set_target_angles(self, targets: Mapping[str, float] | Sequence[float]) -> None:
        """Set actuator target angles by name, or in `joint_names` order."""
        resolved = _as_target_mapping(targets, self._joint_names, "set_target_angles")
        for name, target in resolved.items():
            self._app.set_target_angle(self.joint_ids[name], target)
            self._targets[name] = target

    def set_joint_torques(self, torques: Mapping[str, float] | Sequence[float]) -> None:
        """Apply manual joint torques by name, or in `joint_names` order.

        Only joints without a registered actuator accept manual torques.
        """
        resolved = _as_target_mapping(torques, self._joint_names, "set_joint_torques")
        for name, torque in resolved.items():
            self._app.set_joint_torque(self.joint_ids[name], torque)

    # ── Episode snapshot / reset ────────────────────────────────────────
    def snapshot(self) -> None:
        """Record the current body state and applied targets as the episode start.

        Call this once, after `commit_scene` and before the first simulation
        step; `reset` restores exactly this state.
        """
        positions = np.empty((len(self._link_ids), 3), dtype=np.float32)
        rotations = np.empty((len(self._link_ids), 4), dtype=np.float32)
        linear_velocities = np.empty((len(self._link_ids), 3), dtype=np.float32)
        angular_velocities = np.empty((len(self._link_ids), 3), dtype=np.float32)
        for index, body in enumerate(self._link_ids):
            state = self._app.get_body_state(body)
            positions[index] = (state.position.x, state.position.y, state.position.z)
            rotations[index] = (state.rotation.x, state.rotation.y, state.rotation.z, state.rotation.w)
            linear_velocities[index] = (
                state.linear_velocity.x,
                state.linear_velocity.y,
                state.linear_velocity.z,
            )
            angular_velocities[index] = (
                state.angular_velocity.x,
                state.angular_velocity.y,
                state.angular_velocity.z,
            )
        self._snapshot = _Snapshot(
            positions=positions,
            rotations=rotations,
            linear_velocities=linear_velocities,
            angular_velocities=angular_velocities,
            targets=dict(self._targets),
        )

    @property
    def has_snapshot(self) -> bool:
        """True once `snapshot` has recorded an episode start."""
        return self._snapshot is not None

    def reset(self) -> None:
        """Restore the snapshot: body states, zeroed force/torque, applied targets."""
        if self._snapshot is None:
            raise RuntimeError("Robot.reset() called before Robot.snapshot()")
        snapshot = self._snapshot
        zero = _ext.Vec4(0.0, 0.0, 0.0, 0.0)
        for index, body in enumerate(self._link_ids):
            p = snapshot.positions[index]
            r = snapshot.rotations[index]
            lv = snapshot.linear_velocities[index]
            av = snapshot.angular_velocities[index]
            self._app.set_body_value(body, _ext.BodyField.position, _ext.Vec4(float(p[0]), float(p[1]), float(p[2]), 0.0))
            self._app.set_body_value(
                body, _ext.BodyField.rotation, _ext.Vec4(float(r[0]), float(r[1]), float(r[2]), float(r[3]))
            )
            self._app.set_body_value(
                body, _ext.BodyField.linear_velocity, _ext.Vec4(float(lv[0]), float(lv[1]), float(lv[2]), 0.0)
            )
            self._app.set_body_value(
                body, _ext.BodyField.angular_velocity, _ext.Vec4(float(av[0]), float(av[1]), float(av[2]), 0.0)
            )
            # External force/torque are caller-managed channels: zero both so a
            # policy's torque/force writes never leak into the next episode.
            self._app.set_body_value(body, _ext.BodyField.external_force, zero)
            self._app.set_body_value(body, _ext.BodyField.external_torque, zero)

        self._targets = dict(snapshot.targets)
        for name, target in self._targets.items():
            self._app.set_target_angle(self.joint_ids[name], target)


class _Snapshot:
    """Recorded episode-start state: per-body arrays plus applied target angles."""

    __slots__ = ("positions", "rotations", "linear_velocities", "angular_velocities", "targets")

    def __init__(
        self,
        positions: np.ndarray,
        rotations: np.ndarray,
        linear_velocities: np.ndarray,
        angular_velocities: np.ndarray,
        targets: dict[str, float],
    ) -> None:
        self.positions = positions
        self.rotations = rotations
        self.linear_velocities = linear_velocities
        self.angular_velocities = angular_velocities
        self.targets = targets
