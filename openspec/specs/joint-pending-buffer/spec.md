# joint-pending-buffer Specification

## Purpose

Defines how the physics scene accumulates joint creation requests: the joint pending buffer preserves every submission regardless of joint type, so submissions that share a numeric index are all flushed to the physics scene rather than overwriting one another.

## Requirements
### Requirement: Joint pending buffer preserves all submissions

The joint pending buffer SHALL preserve all submitted joint data regardless of joint type.

#### Scenario: Mixed joint types do not collide

- **WHEN** a fixed joint and a hinge joint are both submitted with the same numeric index
- **THEN** both submissions SHALL be preserved and independently flushed to PhysicsScene

