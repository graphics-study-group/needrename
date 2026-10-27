# gpu-kernel-atomics Specification

## Purpose
Govern how compute shaders update contended memory: what an atomic update is allowed to be, and what a kernel must not do to work around the limits of a single atomic instruction. Applies to every compute shader in the engine, physics and algorithm kernels alike.

## Requirements

### Requirement: Atomic updates complete in one instruction

A compute shader SHALL NOT obtain the effect of an atomic update by repeated attempts: no compare-and-swap retried until it succeeds, and no bounded-attempt variant of one. Every atomic update SHALL be a single hardware atomic instruction, and the value a dispatch publishes SHALL NOT depend on the order in which its invocations execute.

No invocation SHALL abandon work because an attempt budget was exhausted. A result that depends on how many times a competing invocation ran is not admissible, whatever the attempt limit is set to.

*Why:* a retry loop's progress depends on contention, so its failure mode is a silent, load-dependent loss of work that no test reproduces reliably — and a bounded attempt count turns that into a silent correctness bug at exactly the contention the loop was written to handle. A single-instruction update has neither failure mode: its result is the same under any interleaving.

#### Scenario: No compute shader spins on an atomic

- **WHEN** the compute shader sources under `engine/` are searched for atomic operations
- **THEN** every atomic update is a single instruction (`atomicAdd`, `atomicMax`, `atomicMin`, `atomicAnd`, `atomicOr`, `atomicXor`, `atomicExchange`, or a single `atomicCompSwap`)
- **AND** no loop retries an atomic operation until it succeeds

#### Scenario: A contended counter does not lose an invocation's contribution

- **WHEN** every invocation of a dispatch updates the same counter
- **THEN** the value the dispatch publishes equals the value a serial execution of the same invocations would publish
- **AND** no invocation's update is dropped, and no invocation's contribution depends on how many others ran first

### Requirement: An allocation cursor and a published bound are separate counters

Where a kernel must both hand out disjoint index ranges and keep a published value within a bound, the two roles SHALL be carried by two counters rather than by one: an allocation cursor that hands out disjoint contiguous ranges and is allowed to run past the bound, and a published value derived from the ranges actually handed out with a single monotone atomic.

The cursor SHALL be private to the dispatch that uses it and SHALL NOT be read by any later stage. The published value SHALL equal the length of the written prefix and SHALL NOT exceed the bound, whatever order the invocations execute in.

*Why:* one monotone counter cannot be told to stop at a bound, because an atomic add cannot be given a ceiling. Emulating one with a read, a decision and then an add is not atomic either — two invocations can both read a value below the bound and both add, taking the counter past it. Splitting the roles is what makes the bound hold without a retry and without dropping an invocation's range.

#### Scenario: The published bound holds under contention

- **WHEN** more invocations want index ranges than the bound admits
- **THEN** the published value is exactly the bound
- **AND** every index below the published value was written by this dispatch

#### Scenario: An invocation that gets no range is not counted

- **WHEN** an invocation's range starts at or above the bound
- **THEN** it writes nothing and contributes nothing to the published value
- **AND** the published value still covers every index written by an invocation that did get a range
