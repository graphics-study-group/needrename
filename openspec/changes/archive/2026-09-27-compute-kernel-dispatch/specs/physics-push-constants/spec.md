# physics-push-constants

## ADDED Requirements

### Requirement: Physics declares no rotation state

No physics component (`XPBDGpuSolver`, `DummySolver`, `RadixSort`, `ParallelScan`, `CompactUnique`, `SpatialHashBroadDetector`, `ConvexCollisionDetector`) SHALL declare a rotation depth, select a rotation slot, or maintain a frame counter. Every physics dispatch SHALL be recorded through the compute kernel dispatch surface, which takes no slot parameter. `engine/Physics/` SHALL contain no literal `3` rotation depth and no `% 3` frame-counter expression.

#### Scenario: No rotation state remains in physics

- **WHEN** searching `engine/Physics/` for `AllocateResourceBinding`, `BindComputeResource` and `m_frame_counter`
- **THEN** no matches are found
- **AND** no standalone identifier denoting a descriptor-slot rotation depth or a rotation slot is declared by any physics component: a whole-identifier search for `slot_count` — matching that token alone, not the unrelated pre-existing `rigid_body_slot_count` and `shape_slot_count` — finds none

#### Scenario: XPBD dispatch paths are unified

- **WHEN** `XPBDGpuSolver::GPUStep` records a dispatch
- **THEN** every dispatch goes through the kernel dispatch surface
- **AND** no alternate descriptor-set-based dispatch helper remains

## REMOVED Requirements

### Requirement: Physics bindings use a single rotation slot

**Reason**: The requirement's normative content was entirely expressed through API that no longer exists: `AllocateResourceBinding` with its `slot_count` default, and `Rhi::BindComputeResource(cb, stage, binding, 0)`. With the compute kernel taking no slot parameter at all, there is no rotation to keep to a single slot, and a requirement phrased as "use one slot" would describe a concept the code no longer has. The protective intent — that no physics component reintroduces a multi-buffered rotation or a frame counter — is preserved as the *Physics declares no rotation state* requirement added above, which covers the same components and the same grep surface.

**Migration**: Delete the `slot_count`/slot arguments at every physics dispatch site; the kernel dispatch takes none. Keep the prohibition: no physics component may add a rotation depth or frame counter to work around descriptor reuse. Verify with the *No rotation state remains in physics* scenario's search terms, which are a superset of the removed requirement's (`AllocateResourceBinding(3)` and `m_frame_counter`) plus `BindComputeResource` and the standalone identifier `slot_count`. The search is scoped to the rotation concept on purpose: the pre-existing `rigid_body_slot_count` / `shape_slot_count` identifiers count bodies and shapes, are named as such by `detector-configure-detect`, and are not rotation state.
