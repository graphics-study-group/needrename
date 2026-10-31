"""Robot-level convenience layer over a `PhysicsApp` URDF import.

`Robot` turns the name→id maps returned by `PhysicsApp.load_urdf` into the
operations a control loop performs every tick: reading joint states as numpy
arrays, setting batch target angles or joint torques by name, and restoring the
episode's initial state before the next episode.

The helper owns no state of its own: every operation, including `reset`, goes
through the public `PhysicsApp` API. The episode start is the state the app
records itself at `commit_scene`.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from typing import Any

import numpy as np

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

    def set_joint_torques(self, torques: Mapping[str, float] | Sequence[float]) -> None:
        """Apply manual joint torques by name, or in `joint_names` order.

        Only joints without a registered actuator accept manual torques.
        """
        resolved = _as_target_mapping(torques, self._joint_names, "set_joint_torques")
        for name, torque in resolved.items():
            self._app.set_joint_torque(self.joint_ids[name], torque)

    # ── Episode reset ───────────────────────────────────────────────────
    def reset(self) -> None:
        """Restore the scene to the state the app recorded at `commit_scene`.

        Delegates to `PhysicsApp.reset_to_initial`: body poses and velocities go
        back to their commit-time values (velocities as integration seeds),
        external forces and torques are zeroed, actuator targets return to their
        commit-time values and actuator internal state is cleared. No recording
        call is needed beforehand; the restored state is readable immediately and
        takes effect on the next `step`.
        """
        self._app.reset_to_initial()
