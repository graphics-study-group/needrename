# xpbd-contact-solve (delta)

## MODIFIED Requirements

### Requirement: Contact count guard with fixed dispatch

Per-constraint compute dispatches SHALL use a fixed workgroup count calculated from `max_collision_pairs * 5`. Threads with index >= `collision_count` SHALL return immediately.

The per-contact accumulation shaders (`accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`) SHALL be sized for `max_contacts = max_pairs * 5` to accommodate up to 5 contact points per collision pair (4 perturbation + optionally 1 MPR fallback).

#### Scenario: Empty contact list dispatched correctly

- **WHEN** `collision_count` is 0
- **THEN** the dispatch still launches `(max_pairs * 5 + 63) / 64` workgroups
- **AND** all threads return immediately at the count guard

#### Scenario: Contact dispatch covers all 5 points per pair

- **WHEN** a collision pair produces 5 contact points (4 perturbation + MPR fallback)
- **AND** `max_pairs = N*(N-1)/2`
- **THEN** the dispatch workgroup count is `(max_pairs * 5 + 63) / 64`
- **AND** all 5 contact points from that pair are within dispatch range
