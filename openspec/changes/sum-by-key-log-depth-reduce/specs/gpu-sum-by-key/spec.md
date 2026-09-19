## MODIFIED Requirements

### Requirement: Recursive segmented reduction correctness

The reduction SHALL produce, for every key `b < max_key_value` present within the first `entry count` elements, `out[b] = sum of the values of all entries with key b`, in exact per-channel order. Entries whose key is greater than or equal to `max_key_value` (including the `0xFFFFF` INVALID sentinel) SHALL be ignored and SHALL NOT write to the output. Keys absent from those entries SHALL leave their output slots untouched.

The algorithm SHALL require the keys of the elements below the entry count to be sorted in ascending order. Relative order of entries within the same key SHALL not affect the per-key result set (sums of the same value set; floating-point rounding may vary with order).

Each block SHALL process 256 entries: load them into shared memory, merge same-key runs with a segmented pairwise doubling, write per-key sums for segments fully contained in the block directly to the output, and emit at most two boundary records per block (first segment partial, last segment partial; the second record SHALL be an all-zero record when the whole block belongs to one segment).

The block-internal merge SHALL complete in at most `log2(256) = 8` rounds, independent of run length, and every invocation of the block SHALL take part in every round that is executed. In the round for spacing `d` (`d = 1, 2, 4, ... 128`) an invocation SHALL add the value of the element `d` positions to its left to its own value if and only if those two elements carry the same key; no other bookkeeping SHALL be required, because the ascending-key order makes equal keys at the ends of the window imply that the entire window lies within one run. A dead/alive marking scheme SHALL NOT be required. The number of rounds SHALL NOT grow with the length of a run, and no invocation SHALL sum a run element by element.

After the last executed round, every invocation's value SHALL equal the sum of the elements of its own run that lie in the block, up to and including itself, so the run's last element holds its run's in-block sum. The number of executed rounds SHALL be the same for all invocations of a block and SHALL be permitted to be fewer than 8 when the block's runs are resolved earlier: a block SHALL execute further rounds only while at least one invocation whose element is the last of its run still needed a merge in the round just completed, and the decision to stop SHALL be observed uniformly by every invocation of the block.

Elements SHALL be classified against the block's extent from the block's keys alone, without enumerating a run: whether a run reaches the block's first element, and whether it reaches the block's last real element, SHALL each be decided by a comparison of keys.

A level's run structure — which block emits which record, and whether a record's run is complete — SHALL be derived from the level element count, never from the entry count. Elements at or beyond the entry count keep their keys (with zeroed values) so that they participate in run classification exactly like any other element; their keys SHALL be such that they never write to the output.

#### Scenario: Out-of-range payload slot contributes nothing

- **WHEN** the sorted pair array contains the entry `(7, 999)` while the capacity is 256
- **THEN** no value is read at slot 999
- **AND** that entry adds 0.0 to every channel of key 7
- **AND** every other key's sums are unaffected

#### Scenario: Single key spanning many blocks

- **WHEN** the sorted input contains 10000 entries all with key 5 and values all 1.0, with an entry count of 10000
- **THEN** `out[5]` equals 10000.0 after `Record` completes

#### Scenario: Many keys within one block

- **WHEN** the sorted input contains 100 entries with keys 0..99 each appearing once with value 2.0, with an entry count of 100
- **THEN** `out[b]` equals 2.0 for every `b` in 0..99

#### Scenario: A single run occupying a whole block

- **WHEN** one block's 256 entries all carry the same key with value 1.0, with an entry count covering that block
- **THEN** that key receives 256.0 from that block
- **AND** the result is identical whether or not the run continues into a neighbouring block

#### Scenario: Run lengths at and around the merge window boundaries

- **WHEN** a block holds runs whose lengths include 1, 2, 3, 7, 8, 9, and a further run filling the remainder of the block
- **THEN** every key's sum equals the sum of the values of its own elements
- **AND** no element is counted in another run's sum

#### Scenario: One block holding an odd trailing run

- **WHEN** a block's entries end in a run whose last element is not the block's last entry
- **THEN** that run's sum is written to the output exactly once
- **AND** the run that follows it is unaffected

#### Scenario: INVALID sentinel entries are ignored

- **WHEN** the entries below the entry count contain valid entries plus a trailing run of entries with key `0xFFFFF` (greater than `max_key_value`)
- **THEN** no output slot outside `[0, max_key_value)` is written
- **AND** all valid per-key sums are unaffected

#### Scenario: Absent keys keep their output untouched

- **WHEN** no entry below the entry count has key 3
- **THEN** `out[3]` retains whatever value it held before `Record`

#### Scenario: An entry count below the capacity leaves a long inert tail

- **WHEN** a call's entry count covers fewer than two blocks while its capacity covers many
- **AND** every entry at or beyond the entry count carries the `0xFFFFF` key with an arbitrary value
- **THEN** the per-key sums equal those of a reduction over the entries below the entry count
- **AND** the tail's records and partial sums never reach the output

#### Scenario: A smaller entry count after a larger one leaves no residue

- **WHEN** a call with a small entry count follows a call that used a larger entry count on the same buffers
- **THEN** the final per-key sums are unaffected by the earlier call's records

#### Scenario: Every channel count produces the same result

- **WHEN** the same sorted input is reduced with `num_channels` of 1, 4, 7 and 8
- **THEN** each channel's per-key sums match a per-channel serial sum of the same values
- **AND** the number of dispatches is independent of the channel count
