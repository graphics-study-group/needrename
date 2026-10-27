# xpbd-contact-solve

## MODIFIED Requirements

### Requirement: Lagrange multiplier lifetime

Every constraint type's lagrange multiplier buffers SHALL be cleared to zero at the start of each substep, before the position iteration loop, by that type's dedicated clear shader: the contact multiplier by `clear_int_buffer.comp` (or an equivalent float clear), and the hinge and fixed multipliers by `clear_hinge_lagrange.comp` and `clear_fixed_lagrange.comp`, each of which zeroes both of its type's buffers (`HingeAxisLagrange` and `HingeAnchorLagrange`; `FixedRotationLagrange` and `FixedPositionLagrange`). These buffers hold floats and SHALL be cleared by their own shaders rather than by the integer-clear workaround used for the integer accumulators.

Within a substep, each position-solve iteration SHALL accumulate `dlambda` into the lagrange multiplier. The velocity-solve phase SHALL read the accumulated contact lagrange multiplier to compute the friction impulse cap.

Because the multipliers are read-modify-written across iterations, a buffer that is not cleared accumulates across substeps without bound: it must not be possible for any multiplier buffer to be written by an accumulate pass without having been cleared in the same substep.

#### Scenario: Every multiplier buffer is cleared each substep

- **WHEN** a substep runs with contacts, hinge joints and fixed joints present
- **THEN** the contact, hinge-axis, hinge-anchor, fixed-rotation and fixed-position multiplier buffers each receive exactly one clear dispatch before the position iterations
- **AND** each clear covers that type's own element count

#### Scenario: No multiplier is accumulated across substeps

- **WHEN** the same constraint is solved in two consecutive substeps with identical geometry
- **THEN** the second substep's first iteration reads a multiplier of zero, not the previous substep's accumulated value

#### Scenario: A joint count of zero is harmless

- **WHEN** a substep runs with no hinge joints (or no fixed joints)
- **THEN** that type's clear dispatches cover zero elements and no out-of-bounds write occurs
- **AND** the buffers remain allocated for the substep's entry list
