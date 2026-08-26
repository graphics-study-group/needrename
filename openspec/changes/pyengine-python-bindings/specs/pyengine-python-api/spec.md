# pyengine-python-api

## ADDED Requirements

### Requirement: Public package exposes an App wrapper in body coordinates

The `pyengine` package SHALL expose a factory `pyengine.create_app(...)` returning an `App` wrapper class that internally owns a `_native.PhysicsApp`. The `App` SHALL mirror the full method surface of `PhysicsApp` (Building-phase creation, URDF import, joints, actuators, camera, lights, commit, step, pause, render, readback, quit), with only the body-state APIs applying coordinate conversion. `BodyId` and `JointId` SHALL be plain `int`s. Descriptor structs (`CreateInfo`, `BoxDesc`, `SphereDesc`, `CylinderDesc`, `UrdfImportConfig`, joint params, light params, `JointLimits`, `JointState`, `JointBodyPair`, `UrdfImportResult`) SHALL be the re-exported `_native` classes.

#### Scenario: Factory returns a working app
- **WHEN** `pyengine.create_app(mode=pyengine.AppMode.PhysicsOnly)` is called
- **THEN** an `App` instance is returned whose `commit_scene` and `step` can drive a minimal scene

#### Scenario: Descriptor structs are re-exported native classes
- **WHEN** `pyengine.BoxDesc` is inspected
- **THEN** it is the `_native` bound struct with constructor keyword parameters (e.g. `pyengine.BoxDesc(half_extents=(0.5, 0.5, 0.5))`)

### Requirement: Body state reads are body-origin converted

`App.get_body_state(body_id)` SHALL return position, rotation, linear velocity, and angular velocity converted to body-origin coordinates: position and linear velocity SHALL use the cached body-local COM offset `c` and the readback orientation `R` / angular velocity `ω` (`pos_body = pos_com − R·c`; `v_body = v_com − ω × (R·c)`); rotation and angular velocity SHALL pass through unchanged. The return SHALL be a `BodyState`-like object with `float32` numpy fields.

`App.get_com_offset(body_id)` SHALL return the body-local COM offset as a `(3,)` `float32` numpy array; the value SHALL be constant after `commit_scene` (zero for self-built shapes, possibly non-zero for URDF links) and SHALL fail fast when called before `commit_scene`.

#### Scenario: COM-offset body reports its origin
- **WHEN** a body with a known COM offset `c` and orientation `R` is read via `get_body_state`
- **THEN** the returned position equals `pos_com − R·c` (body origin), not the COM position

#### Scenario: Offset query before commit fails fast
- **WHEN** `get_com_offset` is called before `commit_scene`
- **THEN** `RuntimeError` is raised

### Requirement: Writes are deferred and flushed by step()

`App` SHALL expose fine-grained setters `set_position`, `set_rotation`, `set_linear_velocity`, `set_angular_velocity`, `set_force`, and `set_torque`. Each SHALL only record a pending value per body; `step()` SHALL flush all pending writes to `_native` `SetBodyValue` calls once, using the final pending values:

- `R_final` = pending rotation if set, else the last readback rotation;
- `ω_final` = pending angular velocity if set, else the last readback angular velocity;
- `com_pos = pos + R_final · c`; `v_com = lin + ω_final × (R_final · c)`;
- rotation, angular velocity, force, and torque SHALL pass through unconverted.

The flush SHALL happen before the C++ `Step` call, and pending state SHALL be cleared after the step. Any ordering and any subset of setters within one step window SHALL produce the same final state.

#### Scenario: Any setter order yields the same state
- **WHEN** `set_linear_velocity` is called before `set_rotation`, and separately the same values are applied in the reverse order, and both are flushed by `step`
- **THEN** both runs produce the same body-origin linear velocity after the step (within solver tolerance)

#### Scenario: Pending writes flush at step
- **WHEN** `set_position` and `set_rotation` are called and then `step` runs
- **THEN** the body's next readback reflects the new pose, converted back to body coordinates

### Requirement: Reads return the last-stepped state and fail fast on phase errors

`App` reads (`get_body_state`, `get_body_states`, `get_com_offset`, joint state, render output) SHALL return the state as of the last `step()`, never mixing in pending writes. All setters and body-state getters SHALL raise `RuntimeError` when called before `commit_scene` (the wrapper SHALL track the phase, flipping it in `commit_scene`).

#### Scenario: Read before step shows pre-write state
- **WHEN** `set_position` is called and `get_body_state` runs before any `step`
- **THEN** the returned position is the value from the last step, not the pending value

#### Scenario: Setter before commit fails fast
- **WHEN** `set_position` is called before `commit_scene`
- **THEN** `RuntimeError` is raised immediately

### Requirement: Batch read returns BodyStatesBatch dataclass

`App.get_body_states()` SHALL return a `BodyStatesBatch` dataclass with BodyId-indexed dense `float32` arrays converted to body-origin coordinates: `body_ids` (`(N,)` int32), `positions` (`(N,3)`), `rotations` (`(N,4)` xyzw), `linear_velocities` (`(N,3)`), `angular_velocities` (`(N,3)`). It SHALL NOT include COM offsets (available via `get_com_offset`).

#### Scenario: Batch arrays index by BodyId
- **WHEN** `get_body_states` is called on a scene with bodies `0..N-1`
- **THEN** `body_ids` equals `[0..N-1]` and row `i` of every array corresponds to body id `i`

### Requirement: Render output returns a RenderFrame dataclass

`App.get_render_output()` SHALL return a `RenderFrame` dataclass with `pixels` (a `(H, W, 4)` `uint8` numpy copy), `width`, `height`, and `frame_id` integers. Calling it in `PhysicsOnly` mode SHALL propagate the C++ `RuntimeError`.

#### Scenario: Offscreen render returns pixels and frame id
- **WHEN** an `Offscreen` app with readback enabled renders a frame and `get_render_output` is called
- **THEN** a `RenderFrame` with `pixels.shape == (height, width, 4)` and a positive `frame_id` is returned

### Requirement: DLL directories resolve via environment variable

`pyengine/__init__.py` SHALL, before importing `_native`, read the `PYENGINE_DLL_DIR` environment variable; when set, it SHALL pass the path to `os.add_dll_directory` (Windows). When unset, the package SHALL fall back to adding the package's own directory. The mechanism SHALL be guarded so non-Windows platforms skip it without error. The env var is documented for users to point at `build/<cfg>/bin` when the package is not co-located with the engine DLLs.

#### Scenario: Env var supplies the DLL directory
- **WHEN** `PYENGINE_DLL_DIR` points at `build/debug/bin` and `pyengine` is imported
- **THEN** `_native` imports successfully and the engine DLLs resolve through the given directory

#### Scenario: Fallback uses the package directory
- **WHEN** `PYENGINE_DLL_DIR` is unset and the package is co-located with the engine DLLs
- **THEN** import succeeds without the variable

### Requirement: Input and output numeric conventions are uniform

All vector/quaternion parameters of `App` methods SHALL accept any `(3,)` / `(4,)` sequence (tuple, list, or numpy array, float32 or float64); all vector/quaternion outputs SHALL be `float32` numpy arrays; all quaternions SHALL use xyzw order. These conventions SHALL be stated in the package documentation.

#### Scenario: Tuple and array inputs are equivalent
- **WHEN** `set_position` is called with `(1.0, 2.0, 3.0)` and again with `np.array([1.0, 2.0, 3.0])`
- **THEN** both calls produce identical behavior
