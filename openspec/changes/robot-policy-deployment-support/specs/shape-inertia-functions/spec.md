# shape-inertia-functions Delta

## ADDED Requirements

### Requirement: Inertia invertibility gate

`ComInertiaComputer` SHALL invert an inertia tensor only when it is
well-conditioned at its own scale. Invertibility SHALL be tested with a
scale-relative criterion — `|det(I)|` vs machine-epsilon times the Frobenius
norm cubed of `I`, with the determinant and norm computed in double precision —
not an absolute determinant epsilon, because an inertia's magnitude scales with
`mass * length^2` and small-but-perfectly-invertible bodies (e.g. a 0.06 kg,
2 cm foot with `diag(9.6e-6)`) must not be treated as singular.

A tensor failing the invertibility test (numerically singular, e.g. a zero
tensor) SHALL get a zero inverse inertia, as before. A tensor that is
invertible but not positive definite (invalid as an inertia) SHALL be reported
as an error rather than silently inverted.

The gate SHALL apply to both the automatic shape-computed path and the manual
inertia path, so both share one singularity policy.

#### Scenario: Small but valid inertia is inverted

- **WHEN** a body's inertia is `diag(9.6e-6)` (determinant 8.85e-16, far below
  any absolute epsilon of 1e-12)
- **THEN** the inverse inertia is computed and the body can rotate

#### Scenario: Singular inertia gets zero inverse

- **WHEN** a body's inertia tensor is numerically singular at its own scale
  (e.g. the zero tensor)
- **THEN** its inverse inertia is zero and no error is raised

#### Scenario: Scale invariance

- **WHEN** the same inertia tensor is scaled by a large factor and by a small
  factor and both are tested
- **THEN** both are treated identically (both inverted or both singular) —
  the decision depends on conditioning, not magnitude

#### Scenario: Invalid (non-positive-definite) inertia is rejected

- **WHEN** an inertia tensor is invertible but has a non-positive determinant
  or otherwise fails the positive-definiteness check
- **THEN** an error is reported instead of inverting garbage dynamics
