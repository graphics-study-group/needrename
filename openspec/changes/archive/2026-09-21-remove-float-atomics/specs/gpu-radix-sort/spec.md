# gpu-radix-sort

## ADDED Requirements

### Requirement: Primary-key-only sort mode

The `RadixSort` class SHALL support a primary-key-only mode that sorts `uvec2` pairs by `.x` (primary key) ascending in 4 radix passes (one per byte of `.x`), with `.y` riding along as opaque payload that is permuted together with its pair but never compared. Each pass SHALL use the same histogram → prefix-sum → scatter sub-steps, scratch buffer, and GPU-side `PairCount` count-guard semantics as the existing 8-pass mode.

The mode SHALL be selectable per `Record` call and SHALL leave the final sorted result in the same ping buffer as the 8-pass mode. Relative order among pairs with equal `.x` SHALL be unspecified. The mode SHALL dispatch 12 compute dispatches (4 passes × 3 sub-steps) instead of 24.

#### Scenario: Primary-only mode sorts keys with payload ride-along

- **WHEN** input pairs are `[(5,10), (2,20), (2,30), (1,40)]` and the primary-only mode is recorded
- **THEN** after execution the sorted pairs are grouped by `.x` ascending: `(1,40)`, then `(2,20)` and `(2,30)` in either order, then `(5,10)`
- **AND** every original `.y` value appears exactly once in the output

#### Scenario: Result lands in the ping buffer

- **WHEN** the primary-only mode is recorded with `pairs_buf_a` as the ping buffer
- **THEN** after the 4 passes complete, the sorted result resides in `pairs_buf_a`
- **AND** 12 dispatches were recorded (not 24)

#### Scenario: Count guard still applies

- **WHEN** the primary-only mode is recorded with a GPU-side `pair_count` smaller than the capacity
- **THEN** threads with index greater than or equal to the count return immediately
- **AND** only the first `pair_count` pairs participate in the sort

#### Scenario: Key validation still applies

- **WHEN** the primary-only mode is recorded with a `max_key_value` greater than 2^20
- **THEN** a `std::runtime_error` is thrown, matching the 8-pass mode
