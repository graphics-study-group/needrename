## Context

`PhysicsAdaptor` bridges the GO-space physics descriptor world to the COM-space `PhysicsScene`. During `Scene::Init`, components call `Allocate*Joint()` to reserve slots, then `SubmitJoint()` to buffer descriptors. During `Flush()`, buffered descriptors are converted and sent to `PhysicsScene`.

Current state:
- `PhysicsScene` stores fixed and hinge joints in separate vectors (`m_fixed_joints`, `m_hinge_joints`), each with independent 0-based indexing.
- `PhysicsAdaptor` mirrors this for allocation (separate `AllocateFixedJoint` / `AllocateHingeJoint` forwarded to `PhysicsScene`), but uses a single `m_pending_joints` map for buffering.
- This creates a key-collision: a fixed joint at index 0 and a hinge joint at index 0 share the same map entry, causing silent data loss.
- `JointSubmitData = std::variant<FixedJointSubmitData, HingeJointSubmitData>` is used as the parameter type for `SubmitJoint` and the value type for `m_pending_joints`.

## Goals / Non-Goals

**Goals:**
- Eliminate index collision between different joint types in the pending-joint buffer
- Eliminate the `JointSubmitData` variant — replace `SubmitJoint` with typed `SubmitFixedJoint` + `SubmitHingeJoint`
- Maintain the same conversion pipeline in `Flush()`

**Non-Goals:**
- Change the joint allocation scheme in `PhysicsScene` (correct as-is)
- Add new joint types or validation logic

## Decisions

### Decision 1: Split `m_pending_joints` into two type-specific maps

**Chosen:** Replace `std::unordered_map<uint32_t, JointSubmitData> m_pending_joints` with:
- `std::unordered_map<uint32_t, FixedJointSubmitData> m_pending_fixed_joints`
- `std::unordered_map<uint32_t, HingeJointSubmitData> m_pending_hinge_joints`

**Rationale:** Mirrors `PhysicsScene`'s own two-vector design. Eliminates the variant entirely — no `std::visit` needed in `SubmitJoint` or `Flush()`. Each map is type-safe by construction.

**Alternatives considered:**
- *Composite key (`pair<JointType, uint32_t>` or encoded `uint64_t`)*: Keeps a single map but adds key complexity and still requires variant dispatch. Less readable.
- *Change allocation to a global joint pool*: Would require changes to `PhysicsScene`, the GPU solver, and all joint management code — far too invasive for a bug fix.

### Decision 2: Split `SubmitJoint` into `SubmitFixedJoint` + `SubmitHingeJoint`

**Chosen:** Replace `void SubmitJoint(uint32_t joint_idx, const JointSubmitData &data)` with:
- `void SubmitFixedJoint(uint32_t joint_idx, const FixedJointSubmitData &data)`
- `void SubmitHingeJoint(uint32_t joint_idx, const HingeJointSubmitData &data)`

**Rationale:** Since allocation is already type-specific (`AllocateFixedJoint` / `AllocateHingeJoint`), submission should be too. This makes the API consistent with allocation and the underlying PhysicsScene storage. Callers (`PhysicsConstraintComponent`) already dispatch by joint type via `std::visit` over `m_joints` — at each branch they construct the concrete `FixedJointSubmitData` or `HingeJointSubmitData` and can call the matching method directly.

### Decision 3: Remove `JointSubmitData` typedef

**Chosen:** Delete `using JointSubmitData = std::variant<FixedJointSubmitData, HingeJointSubmitData>;` from `PhysicsDescriptors.h`.

**Rationale:** With both `m_pending_joints` and `SubmitJoint` gone, the variant has zero remaining consumers. Removing it keeps the codebase clean.

### Decision 4: Process pending joints in two sequential loops in `Flush()`

**Chosen:** Replace the single `for (auto &[joint_idx, data] : m_pending_joints)` with two loops — one for fixed, one for hinge — each directly accessing their typed struct members without `std::visit`.

**Rationale:** Simpler, no variant overhead, and each struct's fields are directly accessible.

## Risks / Trade-offs

- **[Risk] Future joint types must add new methods** → Acceptable. Adding a new joint type (e.g., spring joint) already requires changes in PhysicsScene, JointConverter, and PhysicsConstraintComponent. Adding one more `Submit*Joint` method and one more pending map is mechanical and consistent.
- **[Trade-off] Two maps vs. one** → Slightly more memory for the map structure overhead, but this is negligible (two empty maps instead of one; at most a few dozen entries at runtime).
- **[Trade-off] `SubmitJoint` API break** → Only one caller (`PhysicsConstraintComponent`), and the change is a trivial rename from `adaptor.SubmitJoint(idx, data)` to `adaptor.SubmitFixedJoint(idx, data)` / `adaptor.SubmitHingeJoint(idx, data)`. The caller already dispatches by type, so no refactoring of dispatch logic is needed.
