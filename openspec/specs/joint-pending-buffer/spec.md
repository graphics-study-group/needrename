# joint-pending-buffer Specification

## Purpose
TBD - created by archiving change fix-joint-index-collision. Update Purpose after archive.
## Requirements
### Requirement: Joint pending buffer preserves all submissions

The joint pending buffer SHALL preserve all submitted joint data regardless of joint type.

#### Scenario: Mixed joint types do not collide

- **WHEN** a fixed joint and a hinge joint are both submitted with the same numeric index
- **THEN** both submissions SHALL be preserved and independently flushed to PhysicsScene

