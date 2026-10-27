# gpu-sum-by-key

## MODIFIED Requirements

### Requirement: Record dispatches the recursive level chain

`Record` SHALL accept the sorted key array, the payload-index array, the packed value buffer, the record buffer, the output buffer, and an entry-count buffer, and SHALL dispatch exactly `k` compute passes (one per level) with a full compute barrier between consecutive levels:

- Level 0 SHALL read the caller's sorted key array and payload-index array, take each entry's key from `keys_in[e]`, gather each of its `num_channels` values from `values[c * capacity + payload_in[e]]`, and SHALL write boundary records into record region 1.
- Levels `1 .. k-2` SHALL read record region `i` and SHALL write record region `i+1`. At these levels the key of record `e` SHALL be `keys_in[e]` and channel `c` SHALL be `values_in[c * R_i + e]`.
- Level `k-1` (a single workgroup covering `R_{k-1} <= 256` records) SHALL write all remaining per-key sums directly to the output buffer.

**Two different lengths bound a level, and they SHALL NOT be conflated.**

The **level element count** is the element count of the array a level reads: the call's `capacity` at level 0 (`R_0 = capacity`), and `R_i` at level `i >= 1`. It alone SHALL determine:

- the element-index bound (`e < level element count`), which decides whether an element exists at all;
- the per-block real-extent used for run classification, and therefore which boundary records a block emits;
- the per-level workgroup count — level 0 SHALL always dispatch `ceil(capacity / 256)` workgroups so that every record slot of region 1 is rewritten on every call;
- the record-region partition (`R_{i+1} = 2 * ceil(R_i / 256)` records, one pair of slots per block);
- the level-0 channel stride of the value buffer.

The **entry count** is the number of leading elements that carry real input data. It SHALL bound **data extent only**: at level 0 no value SHALL be read for an element at or beyond it, and it SHALL have no effect on the element-index bound, on run classification, on the workgroup count, or on the record-region layout. It SHALL be read from the caller's entry-count buffer inside the shader at execution time, because the producing pass runs on the GPU and the host does not know the value at record time. A call whose entry count equals the call's capacity SHALL behave exactly as if the bound were absent.

Because the level element count and not the entry count sizes level 0, every block of level 0 always writes its portion of region 1, and the record chain stays dense: each of the `2 * ceil(capacity / 256)` slots of region 1 is written on every call. Sizing level 0's workgroup count by the entry count would leave record slots from an earlier call in place, which the next level reads as phantom partials.

Elements at or beyond the entry count SHALL NOT have their **values** read. This includes the caller's key and payload-index arrays: the values of an entry whose index is at or beyond the entry count SHALL NOT be gathered from the value buffer, and neither SHALL the values of an entry whose payload index is at or beyond it. Such an entry SHALL still contribute its key to the level's run structure and SHALL contribute 0.0 to every channel. Callers therefore only need valid values below the entry count, and only need the key and payload-index arrays themselves to be writable up to their bound (the entry passes' full-capacity write contract covers that).

An entry whose payload index is outside `[0, capacity)` SHALL contribute 0.0 to every channel and SHALL NOT be read from the value buffer, so a caller that emits a bad payload index loses that entry instead of triggering a wild read. Its key SHALL be used unchanged: rewriting the key would violate the ascending-order requirement below and could split a real key's run, losing one of its partial sums.

`num_channels` SHALL be in `[1, 8]` and `capacity` SHALL be greater than zero; a call violating either, or a `max_key_value` of zero, SHALL throw `std::invalid_argument`. `Record` SHALL additionally reject a capacity whose implied buffer sizes exceed the buffers bound for that call.

The input mode SHALL travel in the push-constant block as a `gather_payload` flag that `Record` sets for level 0 and clears for every deeper level. In gather mode the level's input SHALL always be the caller's key array plus its payload-index array, and the entry-count bound SHALL apply to both. In non-gather mode the level's input SHALL always be the `KeysIn` / `ValuesIn` key and value arrays, and no entry-count bound SHALL apply, because a deeper-level input is produced by the previous level and is entirely valid. The entry-count buffer SHALL be bound at every level (its contents are read only in gather mode), so no level needs a placeholder for it.

All other per-level parameters (region offsets, level element count, workgroup count, channel count, key bound) SHALL also travel via the shader's push-constant block. The push-constant block SHALL NOT carry the entry count, which is GPU-produced. The caller SHALL insert the outer barriers around the whole `Record`.

The shader's storage-buffer bindings SHALL be `KeysIn`, `PayloadIn`, `ValuesIn`, `RecKeys`, `RecValues`, `OutValues` and the entry-count buffer — seven buffers, with one pass per level for all channels. There SHALL be no `uvec2` pair binding: level 0's key and payload index come from the same two scalar arrays every other level uses.

#### Scenario: Full recursion for a large array

- **WHEN** `Record` is called with `capacity = 200000` (`k = 3`) and an entry count equal to the capacity
- **THEN** three compute dispatches are recorded (782, 7, and 1 workgroups respectively)
- **AND** a barrier is recorded between consecutive dispatches
- **AND** only the first dispatch reads `PayloadIn` (`gather_payload = 1`)

#### Scenario: Level 0 dispatch is sized by the capacity, not by the entry count

- **WHEN** `Record` is called with a `capacity` far larger than its entry count
- **THEN** level 0 still dispatches `ceil(capacity / 256)` workgroups
- **AND** every record slot of region 1 is rewritten by that call
- **AND** the deeper levels dispatch for their own `R_i`, which are derived from the capacity

#### Scenario: Single level for a small array

- **WHEN** `Record` is called with a capacity at or below 256
- **THEN** exactly one compute dispatch is recorded
- **AND** every run is complete within that single block, so no record is carried to another level

#### Scenario: An entry count below the capacity ignores everything above it

- **WHEN** `Record` is called with an entry count well below the capacity
- **AND** the key array, the payload-index array and the value buffer above the entry count hold garbage, including entries whose keys are valid and whose payload indices are valid
- **THEN** the per-key sums equal those of a reduction over the first `entry count` entries only
- **AND** no value at or above the entry count is read

#### Scenario: An entry count equal to the capacity is the identity

- **WHEN** `Record` is called with an entry count equal to the capacity
- **THEN** the result is identical to the result of the same call with the bound absent

#### Scenario: A zero entry count produces no output

- **WHEN** `Record` is called with an entry count of zero
- **THEN** level 0 is still dispatched over `ceil(capacity / 256)` workgroups
- **AND** no value is read from the value buffer
- **AND** no output slot is written

#### Scenario: Level 0 gathers values by payload index

- **WHEN** `Record` is called with sorted keys `[0, 0, 5]` and payload indices `[3, 1, 0]`, and an entry count of 3
- **AND** the value buffer holds `1.0`, `2.0` and `4.0` at payload slots 3, 1 and 0 respectively
- **THEN** `out[0]` equals `1.0 + 2.0` (in either order)
- **AND** `out[5]` equals `4.0`
- **AND** the result does not depend on the order the entries appear in

#### Scenario: Invalid arguments are rejected per call

- **WHEN** `Record` is called with `num_channels` outside `[1, 8]`, with a zero capacity, or with a zero `max_key_value`
- **THEN** a `std::invalid_argument` exception is thrown
