## Why

Multiple physics subsystems incorrectly use GameObject (GO) coordinates where center-of-mass (COM) coordinates are required: model matrix rendering outputs COM transform instead of GO transform, joint constraint data is computed from GO positions but solved against COM positions, and manual inertia forces COM offset to zero. This causes visual misplacement of rendered meshes, incorrect constraint rest-states, and prevents users from specifying non-zero COM offsets with custom inertia tensors.

## What Changes

- **Model matrix fix**: `model_matrix.comp` reads `center_offset_local_position` buffer and outputs GO world transform (COM position minus rotated offset) instead of COM transform. **BREAKING**: vertex shader output changes for rigid bodies with non-zero COM offset.
- **Joint COM conversion**: `PhysicsConstraintComponent` passes GO-local joint parameters to `PhysicsScene`, which defers GO→COM conversion until after `RecalculateRigidBodyState` runs inside `InitializePendingRigidBodies`. GpuHingeJoint and GpuFixedJoint fields become COM-local. **BREAKING**: GPU joint buffer semantics change — shaders now receive COM-local data.
- **Manual COM offset**: `m_use_manual_inertia` renamed to `m_use_manual_inertia_com`. When enabled, user must provide both inertia tensor and `m_manual_center_of_mass` (GO-local). `RecalculateRigidBodyState` uses manual values directly instead of zeroing COM offset.
- **GetRigidBodyCenterOffsetLocal getter**: new public method on `PhysicsScene` exposing COM offset for a given rigid body index.
- **Remove dead code**: `RegisterFixedJoint` and `RegisterHingeJoint` (unused, replaced by Allocate + Update pattern).
- **Fix recovery formula**: `RecalculateRigidBodyState` line 735 corrected from `center_world - center_offset` to `center_world - rot * center_offset`.
- **Document `SetRigidBodyTransform`**: comment that it stores a temporary GO-world placeholder overwritten by `RecalculateRigidBodyState`.

## Capabilities

### New Capabilities

- `rigidbody-center-of-mass-offset`: Manual COM offset on RigidBodyComponent plus GO→COM conversion pipeline for joint constraints and model matrix rendering.

### Modified Capabilities

- `hinge-joint-constraint`: GpuHingeJoint `hinge_anchor_obj1` and `initial_rel_pos_local` are now COM-local space (converted from GO-local during `InitializePendingRigidBodies`). Non-breaking for shader internals; GPU-side math unchanged.
- `renderer-ancestor-rigidbody-attach`: Model matrix buffer now represents GO world transform instead of COM world transform. Push-constant `model` composition semantics unchanged.
- `shape-inertia-functions`: Manual inertia flag renamed from `m_use_manual_inertia` to `m_use_manual_inertia_com`. Manual path now sets COM offset from `m_manual_center_of_mass` instead of zeroing it.

## Impact

- **RigidBodyComponent** — new `m_manual_center_of_mass` field, renamed `m_use_manual_inertia_com` (serialization-breaking: asset files need migration).
- **PhysicsScene** — new public getter, internal pending joints queue, modified `RecalculateRigidBodyState` branch, removed `Register*Joint` methods.
- **PhysicsConstraintComponent** — Init simplified: passes GO-local data through without conversion.
- **model_matrix.comp** — new buffer binding for `rigid_body_center_offset_local_position`.
- **accumulate_hinge_position.comp** / **accumulate_fixed_position.comp** — no shader changes; math correct once input data is COM-local.

- **CONTEXT.md** — new glossary defining GO-local vs COM-local coordinate spaces.
