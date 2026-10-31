## Context

See proposal.md for motivation. State that shapes this design:

- `app/physics/Actuator.{h,cpp}`: `Actuator` base (target angle + `const` `ComputeTorque`), `PdActuator`, `DcMotorActuator`. `PhysicsApp::Impl::m_actuators` is a `std::vector<std::unique_ptr<Actuator>>` parallel to `m_joints`; `RunActuators` runs once per `Step`, measures the joint from the previous step's CPU readback, and writes the pair of body torques into the persistent `ExternalTorque` write staging.
- The binding layer (`app/python/physics_app_bindings.cpp`) cannot hand a Python-owned instance to `std::unique_ptr`, so every Python actuator is currently hidden behind a `PythonActuator` adapter (owns an `nb::object`, acquires the GIL in `ComputeTorque`, mirrors app-side targets into the Python object through a `mutable float m_synced_target`).
- nanobind's `std::shared_ptr` caster (`third_party/nanobind/include/nanobind/stl/shared_ptr.h`) supports Python-owned instances natively: `shared_from_python` produces a `shared_ptr` whose deleter holds a Python reference, and `from_cpp` returns the existing Python wrapper (identity preserved).
- The `step` binding releases the GIL around `app.Step()` (scoped release inside the lambda so `_step_id` bookkeeping keeps the GIL).
- No time step is reachable from `PhysicsApp`: `PhysicsSystem` exposes no solver configuration and `XpbdConfig::time_step` (default 1/60) lives inside `XPBDGpuSolver`. Python users configure `dt` externally and pass it to actuator constructors.

## Goals / Non-Goals

**Goals:**
- One registration path for C++ and Python actuators: no wrapper object, no shadow state, same instance visible on both sides.
- The caller keeps its handle: live parameter tuning, identity (`get_actuator(joint) is act`), detach/replace.
- Control laws with internal state (filters, integrators, friction/backlash) become expressible.

**Non-Goals:**
- Time-step plumbing (exposing physics configuration / `GetTimeStep()`); `dt` keeps arriving as a constructor parameter.
- Episode reset (`ResetToInitial()`); it is the next change and merely consumes the new `Reset()`.
- Multiple actuators per joint, actuator serialization, changing the per-step control rate.

## Decisions

### D1: `std::shared_ptr<Actuator>` ownership, plus `GetActuator` / `RemoveActuator`
`AddActuator(JointId, std::shared_ptr<Actuator>)`; `Impl::m_actuators` becomes `std::vector<std::shared_ptr<Actuator>>`; `GetActuator` returns the registered instance (empty pointer when none — surfaced as `None` in Python). Chosen over:
- *keep `unique_ptr` + adapter*: the adapter and its `m_synced_target` shadow exist only to satisfy the ownership model — the model is wrong, not the binding;
- *borrow `Actuator&` + `keep_alive`*: zero allocation, but C++ callers carry the dangling-pointer risk and `RemoveActuator` cannot hand ownership back; `shared_ptr` also makes "app outlives the caller's variable" safe.
Cost is one refcounted allocation per actuator; actuator dispatch is once per joint per step, so the indirection is noise. `SetTargetAngle(JointId, float)` stays as joint-keyed sugar (`GetActuator(joint)->SetTargetAngle(x)`).

### D2: Registration in both phases; `RemoveActuator` clears the torque channel
Actuators are control state, not scene structure (`SetTargetAngle` is already legal in both phases), so `AddActuator`/`RemoveActuator`/`GetActuator` are Drive-legal; an actuator added after `CommitScene` drives from the next `Step`. `RemoveActuator` must zero both endpoint bodies' `ExternalTorque`: the channel is persistent (last write keeps applying), so without clearing, a removed actuator's final torque would keep acting forever and `SetJointTorque` would fight it. `RemoveActuator` on a joint without an actuator throws `std::logic_error` (same strictness as `SetTargetAngle`).

### D3: `ComputeTorque` drops `const`; new `virtual void Reset()` (no-op base)
`RunActuators` already calls through non-const references, so the engine change is trivial. `Reset()` is the extension point episode resets will call (next change) and users can call directly; a no-op default keeps stateless laws unchanged. Alternatives: `mutable` state behind a `const` interface (rejected — that is the smell the adapter already shows), or a separate `StatefulActuator` subclass (rejected — splits the hierarchy for no gain).

### D4: Delete `PythonActuator`; the trampoline acquires the GIL
With `shared_ptr`, a Python subclass instance is registered directly; virtual dispatch goes through the existing `PyActuator` trampoline. The trampoline's overrides wrap `NB_OVERRIDE_*` in `nb::gil_scoped_acquire`, so the callback is safe regardless of which thread runs `Step` and the `step` binding keeps releasing the GIL around the blocking call. Rule to document in the binding header: *every* future virtual added to `Actuator` must acquire the GIL in its trampoline override. The non-virtual `SetTargetAngle`/`GetTargetAngle` live on the C++ base; since the Python object *is* that object now, no target mirroring is needed.

### D5: Parameters as plain accessors bound with `def_prop_rw`
Each shipped type gains getter/setter pairs for every constructor parameter (`kp`, `kd`, `stall_torque`, `no_load_speed`, `cont_torque`, `gear_ratio`) and the bindings expose them as properties. Rejected alternatives: a parameter struct (grows the interface per type), constructor-only parameters (no live tuning).

### D6: Error surface unchanged
`std::invalid_argument` → `ValueError`, `std::out_of_range` → `IndexError`, `std::logic_error` → `RuntimeError`, with the existing translator in `bindings.cpp`.

## Risks / Trade-offs

- **[A `shared_ptr` built from a Python-owned instance keeps a Python reference alive from C++]** → nanobind's `py_deleter` skips deallocation after interpreter shutdown (`cleanup_guard`); app destruction releases its share while the GIL is held (`del app`) or is a no-op post-shutdown. C++-owned actuators have no Python involvement at all.
- **[GIL re-acquisition per callback]** → cost is dwarfed by the Python call itself; C++ actuators never touch the GIL. If it ever matters, batch callbacks per step are a local change to `RunActuators`.
- **[Both-phase registration allows controller swaps mid-episode]** → removal clears the torque channel (spec scenario covers this); everything else follows the documented last-write-wins semantics.
- **[Stateful `ComputeTorque` makes reproducibility the subclass's own business]** → `Reset()` contract plus unit tests for state clearing; the app does not auto-reset (that is the next change's call site).
- **[Two owners can mutate one actuator]** → the app is single-threaded inside `Step`; callers tune between steps. Documented, not enforced.

## Migration Plan

1. `app/physics/Actuator.{h,cpp}` and `PhysicsApp.{h,cpp}` first (interface + `Impl` storage + channel clearing), adapting the in-tree C++ tests (`physics_app_actuator_unit_test`, `physics_app_actuator_stand_test`, `physics_app_joint_torque_test`) to `make_shared` and non-const calls.
2. Bindings: remove `PythonActuator`, bind `add_actuator`/`get_actuator`/`remove_actuator` over `shared_ptr`, add properties, add the trampoline GIL guard.
3. Python tests (`test_actuator.py`, `test_joint_torque.py`), examples, `app/python/README.md`.

Rollback: the `AddActuator` signature break is contained — the only callers are the in-tree tests and the binding; no persisted data or asset format changes.

## Open Questions

- Whether `AddActuator` on an occupied joint should replace and return the previous instance (today: throw, and replace = `RemoveActuator` + `AddActuator`). Deferred until a use case appears; the spec allows either future choice.
