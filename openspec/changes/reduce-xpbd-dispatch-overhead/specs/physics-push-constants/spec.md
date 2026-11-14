## ADDED Requirements

### Requirement: Shared joint color indexing adds no constant fields

The single-workgroup joint sweep SHALL retain its existing 32-byte constant block containing gravity/substep time, fixed color and descriptor counts, and hinge color and descriptor counts. It SHALL derive the hinge offsets base as `fixed_color_count + 1` and the hinge IDs base as `JointColorOffsets[fixed_color_count]`; no additional base fields SHALL be added to the block.

Per-color joint dispatches SHALL retain their existing constant-block layout. Fixed color offsets SHALL remain local to the beginning of the shared IDs table, while hinge color offsets SHALL include the alive fixed-ID count derived from the final fixed range value. All offsets carried in constants and derived inside shaders SHALL be element indices, not byte offsets or descriptor subrange offsets.

#### Scenario: Sweep constant size remains unchanged

- **WHEN** the joint sweep constant block is declared for shared color tables
- **THEN** its reflected and C++ sizes remain 32 bytes
- **AND** no hinge-base field is appended

#### Scenario: Hinge sweep derives its table bases

- **WHEN** a sweep processes a hinge color
- **THEN** it reads that color's local range from `JointColorOffsets[fixed_color_count + 1 + color]`
- **AND** it resolves joint IDs relative to `JointColorOffsets[fixed_color_count]`

#### Scenario: Per-color hinge offset uses the shared table

- **WHEN** the solver records a per-color hinge dispatch
- **THEN** its constant color offset is the hinge range value plus the final fixed range value
- **AND** the constant-block size and other fields remain unchanged
