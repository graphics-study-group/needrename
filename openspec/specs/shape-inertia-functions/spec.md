# shape-inertia-functions

## Purpose

Extract per-shape-type inertia tensor and volume computation into dedicated, named functions used by the COM/inertia pipeline. The computation lives in `engine/Framework/world/physics/Internal/ComInertiaComputer.hpp` as pure functions invoked from `PhysicsAdaptor::Flush`, replacing the inline box-only calculations previously in `PhysicsScene::RecalculateRigidBodyState` (which no longer exists).

## Requirements

### Requirement: Per-shape volume functions

`ComInertiaComputer.hpp` SHALL provide the following functions in `Engine::detail` (inline, pure) for shape volume computation:

- `float ComputeBoxVolume(const glm::vec3 &feature)` — returns `8.0f * hx * hy * hz`
- `float ComputeSphereVolume(float radius)` — returns `(4.0f / 3.0f) * π * r³`
- `float ComputeCylinderVolume(float radius, float half_height)` — returns `π * r² * 2.0f * half_height`

#### Scenario: Box volume

- **WHEN** `ComputeBoxVolume({1.0, 2.0, 3.0})` is called
- **THEN** the returned value is `48.0f` (8 × 1 × 2 × 3)

#### Scenario: Zero volume for degenerate shapes

- **WHEN** any volume function is called with a zero dimension (e.g., `radius = 0`, `feature.z = 0`)
- **THEN** the returned value is `0.0f`

### Requirement: Per-shape inertia functions

`ComInertiaComputer.hpp` SHALL provide the following functions returning `glm::mat3`:

- `glm::mat3 ComputeBoxInertia(float mass, const glm::vec3 &feature)`
  - I_xx = `(mass / 3.0f) * (hy² + hz²)`
  - I_yy = `(mass / 3.0f) * (hx² + hz²)`
  - I_zz = `(mass / 3.0f) * (hx² + hy²)`
  - Off-diagonals = 0

- `glm::mat3 ComputeSphereInertia(float mass, float radius)`
  - All diagonals = `(2.0f / 5.0f) * mass * r²`
  - Off-diagonals = 0

- `glm::mat3 ComputeCylinderInertia(float mass, float radius, float half_height)`
  - Let `h = 2.0f * half_height`
  - I_xx = I_yy (transverse) = `(mass / 12.0f) * (3.0f * r² + h²)`
  - I_zz (axial, Z-up) = `(mass / 2.0f) * r²`
  - Off-diagonals = 0

#### Scenario: Box inertia matches existing behavior

- **WHEN** `ComputeBoxInertia(1.0f, {1.0, 1.0, 1.0})` is called
- **THEN** the returned diagonal is `{2/3, 2/3, 2/3}` (all h=1 → h²=1 → (1/3)*(1+1) = 2/3)

#### Scenario: Sphere inertia is isotropic

- **WHEN** `ComputeSphereInertia(5.0f, 1.0f)` is called
- **THEN** all diagonal entries equal `2.0` (5 × 2/5 × 1² = 2.0)

#### Scenario: Cylinder axial vs transverse inertia

- **WHEN** `ComputeCylinderInertia(1.0f, 1.0f, 1.0f)` is called (r=1, h=2)
- **THEN** I_zz = 0.5, I_xx = I_yy = 7/12 ≈ 0.5833 (axial < transverse for h > r)

### Requirement: ComInertiaComputer dispatches by shape type

`ComInertiaComputer::Compute(const RigidBodyDescriptor &rb_desc, const vector<ShapeComputationData> &shapes)` SHALL switch on each shape's type to call the appropriate volume and inertia functions, replacing the hardcoded box calculations. The dispatch SHALL handle `Box` (0), `Sphere` (1), and `Cylinder` (2). Unknown types SHALL be treated as zero-volume (skipped in center-of-mass and inertia accumulation) with an `SDL_LogWarn`.

When `rb_desc.use_manual_inertia_com` is `true`, automatic volume-weighted computation SHALL be skipped entirely. The inertia tensor SHALL be set from `rb_desc.manual_inertia` and the COM offset SHALL be set from `rb_desc.manual_center_of_mass`. Shape local poses SHALL still be recomputed relative to the manual COM.

The output `ComInertiaOutput` SHALL contain `center_offset_local` (GO-local COM offset), the COM world pose, the inertia tensor, and its inverse. `PhysicsAdaptor::Flush` SHALL cache `center_offset_local` in `m_com_offsets[rb_idx]` for later retrieval via `GetComOffsetLocal`.

#### Scenario: Mixed-shape rigid body center of mass

- **WHEN** a rigid body has a Box (hx=1, hy=1, hz=1, at origin) and a Sphere (r=1, at (3, 0, 0))
- **AND** `ComInertiaComputer::Compute` is called for the rigid body
- **THEN** the center of mass is weighted by box volume (8) and sphere volume (~4.189)
- **AND** the COM is closer to the box (larger volume dominates)

#### Scenario: Unknown shape type is skipped

- **WHEN** a shape's type has an unrecognized value (e.g., 99)
- **THEN** `SDL_LogWarn` is emitted
- **AND** the shape contributes zero volume and zero inertia (effectively skipped)

#### Scenario: Manual inertia and COM override automatic computation

- **WHEN** `use_manual_inertia_com = true` with `manual_center_of_mass = (0.5, 0, 0)` and manual inertia provided
- **THEN** the COM offset is set to `(0.5, 0, 0)` without volume-weighted computation
- **AND** the inertia tensor is set from the manual values
- **AND** shape local poses are recomputed relative to the manual COM

### Requirement: Parallel axis theorem is applied uniformly

After computing per-shape local inertia via the dedicated functions, `ComInertiaComputer::Compute` SHALL apply the parallel axis theorem identically for all shape types: rotate the local inertia by the shape's local rotation relative to the center of mass, then add `mass * (d²·I − outer(d,d))` where `d` is the shape's local position offset from the center of mass.

#### Scenario: Offset sphere gets parallel axis contribution

- **WHEN** a sphere (m=1, r=1) is offset by `d = (2, 0, 0)` from the COM
- **THEN** the total inertia I_yy and I_zz include an additional `1 * (4 - 0) = 4` from the parallel axis term (d² = 4, but outer(d,d) only has xx=4 so yy and zz get full d²)

### Requirement: Manual inertia flag renamed to m_use_manual_inertia_com

`RigidBodyComponent` SHALL use the field `m_use_manual_inertia_com` in place of the former `m_use_manual_inertia`. The value SHALL be passed through `RigidBodyDescriptor::use_manual_inertia_com` to `ComInertiaComputer`; the former PhysicsScene-side storage as `m_rigid_body_use_manual_inertia` SHALL NOT exist.

#### Scenario: Component field uses new name

- **WHEN** a `RigidBodyComponent` header is inspected
- **THEN** it declares `m_use_manual_inertia_com` and has no `m_use_manual_inertia` field

#### Scenario: Manual flag reaches ComInertiaComputer

- **WHEN** `RigidBodyComponent::Init` builds a `RigidBodyDescriptor` for a component with `m_use_manual_inertia_com = true`
- **THEN** `rb_desc.use_manual_inertia_com` is `true`
