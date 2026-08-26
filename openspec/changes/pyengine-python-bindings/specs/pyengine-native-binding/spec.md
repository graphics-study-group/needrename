# pyengine-native-binding

## ADDED Requirements

### Requirement: _native mirrors the complete PhysicsApp C++ surface

The `_native` nanobind module SHALL bind every public declaration of the `AppPhysics` namespace in `app/physics/PhysicsApp.h` and `Actuator.h`, with no coordinate conversion applied:

- enums: `AppMode`, `BodyField`;
- structs: `CreateInfo`, `BodyState`, `JointState`, `JointLimits`, `BodyStatesView`, `RenderOutput`, `UrdfImportConfig`, `UrdfImportResult`, `JointBodyPair`, `BoxDesc`, `SphereDesc`, `CylinderDesc`, `FixedJointParams`, `HingeJointParams`, `DirectionalLightParams`;
- class `PhysicsApp`: the `Create` factory, all Building-phase methods, `CommitScene`, all Drive-phase methods, readback methods, and both-phase methods;
- actuator classes: `Actuator`, `PdActuator`, `DcMotorActuator`.

`_native` SHALL be an internal module: it SHALL be importable (`pyengine._native`) but SHALL not be part of the documented public API, and the public `pyengine` package SHALL own all user-facing behavior.

#### Scenario: Every C++ method is reachable
- **WHEN** a Python test enumerates the members of `pyengine._native.PhysicsApp`
- **THEN** every public method of the C++ class is present (including `AddBox`, `LoadUrdf`, `AddActuator`, `Step`, `GetBodyState`, `GetBodyStates`, `SetBodyValue`, `GetRenderOutput`, `ShouldQuit`)

#### Scenario: Native readback is COM-space
- **WHEN** `_native.PhysicsApp.GetBodyState` is called on a body with a non-zero COM offset
- **THEN** the returned position is the center-of-mass position (no conversion)

### Requirement: C++ exceptions translate to Python exceptions

Calls into `_native` SHALL translate C++ exceptions to Python exceptions: `std::logic_error` → `RuntimeError`, `std::out_of_range` → `IndexError`, `std::invalid_argument` → `ValueError`, `std::runtime_error` → `RuntimeError`. Translation SHALL happen automatically at the binding boundary (nanobind's built-in mapping) and SHALL NOT be re-wrapped by the public package.

#### Scenario: Invalid body id raises IndexError
- **WHEN** `GetBodyState` is called with a BodyId outside the registry
- **THEN** `IndexError` is raised

#### Scenario: Wrong-phase call raises RuntimeError
- **WHEN** `Step` is called before `CommitScene`
- **THEN** `RuntimeError` is raised

### Requirement: glm and standard-library types are cast with defined conventions

The module SHALL bind the following conventions:
- `glm::vec3` / `glm::quat` parameters accept any `(3,)` / `(4,)` sequence; `glm::vec4` parameters accept any `(4,)` sequence;
- quaternions use xyzw order everywhere;
- `std::optional<JointLimits>` maps to `None` or the `JointLimits` object;
- `std::unordered_map<std::string, BodyId>` and `std::unordered_map<std::string, JointBodyPair>` map to `dict[str, int]` / `dict[str, JointBodyPair]`;
- `std::filesystem::path` accepts and produces `str`;
- `BodyId` / `JointId` are plain `int`.

#### Scenario: URDF result is a dict of ints
- **WHEN** `LoadUrdf` returns and its `link_bodies` member is inspected
- **THEN** it is a `dict` mapping link-name `str` to body-id `int`

#### Scenario: Optional limits are None-able
- **WHEN** `GetJointLimits` is called on a joint with no recorded limits
- **THEN** `None` is returned

### Requirement: Batch and render readback cross as copies

`BodyStatesView` SHALL be exposed so each span becomes a `float32` numpy array (copies, not views): slot indices as `int32`, positions/rotations/velocities as `(N, 4)` or `(N, 3)` as appropriate. `RenderOutput.pixels` SHALL be exposed as a copied `(H, W, 4)` uint8 buffer, with `width`, `height`, and `frame_id` as `int`. No API SHALL expose raw pointers into staging buffers.

#### Scenario: Readback arrays are stable after next step
- **WHEN** `GetBodyStates` returns arrays and `Step` is subsequently called
- **THEN** the previously returned numpy arrays still hold the values from before the step (copy semantics)

#### Scenario: Render pixels are a typed copy
- **WHEN** `GetRenderOutput` returns
- **THEN** `pixels` is a `numpy.ndarray` of dtype `uint8` with shape `(height, width, 4)`, and `frame_id` matches the C++ value

### Requirement: Actuators support Python subclassing with safe ownership

The binding SHALL expose `Actuator` as a base class supporting Python subclassing (nanobind trampoline): a Python subclass overriding `compute_torque(angle, angular_velocity)` SHALL be invoked from C++ when `PhysicsApp.Step` calls `ComputeTorque`. `PdActuator` and `DcMotorActuator` SHALL be bound as concrete classes with their constructor parameters.

The binding SHALL pass actuators to `AddActuator` as `std::shared_ptr<Actuator>` so that C++ ownership and Python object lifetime coexist without dangling. Both C++-created actuators (`PdActuator`, `DcMotorActuator`) and Python-subclassed instances SHALL work identically.

#### Scenario: Python actuator subclass drives a joint
- **WHEN** a Python class subclasses `Actuator`, overrides `compute_torque`, is registered via `add_actuator`, and `Step` runs
- **THEN** the Python override is called with the joint's measured state and its returned torque is applied

#### Scenario: Python actuator outlives app deletion safely
- **WHEN** a Python-subclassed actuator object is kept referenced after the `App` is deleted
- **THEN** accessing the object does not crash (shared ownership)

### Requirement: Module output lands inside the package directory

The `_native` module target SHALL emit its `.pyd` into the `pyengine` package directory inside the build output (`build/<cfg>/bin/pyengine/`), co-located with the copied package Python sources and with the runtime DLLs of the engine (`PhysicsApp.dll`, `Engine*.dll`, third-party DLLs) which all resolve from `build/<cfg>/bin`.

#### Scenario: pyd is importable from the package
- **WHEN** the build completes and the `pyengine` package directory is inspected
- **THEN** `_native.pyd` exists inside `pyengine/` and `from pyengine import _native` succeeds with `build/<cfg>/bin` on the DLL search path
