## ADDED Requirements

### Requirement: Algorithm shader source comments

Every GLSL source file under `engine/Physics/shader/algorithm/` SHALL open with a header comment stating, in the present tense, what the file does: the contract it implements, the invariants it keeps, its dispatch geometry, and the layout of the buffers it binds.

The header SHALL describe the shader only as it is. It SHALL NOT contain the rationale for a design decision or for a rejected alternative, the file's own history (what it used to do, what it replaced, what it was changed from), or worked examples and arithmetic derivations. A statement whose purpose is to justify the current shape of the code rather than to describe it SHALL be left out.

Detail that explains one non-obvious step SHALL sit at the declaration or the statement it explains, rather than lengthening the header.

Every parameter of a shader's top-level entry function SHALL be documented, in the header or at the declaration, with what the parameter means, where its value comes from — a push constant, a bound buffer, or a value the shader derives — and the range it is valid over where one exists.

#### Scenario: The reduction body's header states the current contract

- **WHEN** `sum_by_key_body.glsl` is inspected
- **THEN** its header states the bound a level applies, the level geometry it derives, and the rule by which a run is settled or carried to the next level
- **AND** it is written in the present tense

#### Scenario: Algorithm shader headers carry no rationale, history or worked examples

- **WHEN** the headers of `sum_by_key_body.glsl`, `sum_by_key.comp`, `sum_by_key_push.comp`, `radix_block_histogram.comp`, `radix_scatter.comp` and `radix_copy_back.comp` are inspected
- **THEN** none of them argues for a design decision or against an alternative one
- **AND** none of them narrates what the file used to do, what it replaced, or what it was changed from
- **AND** none of them contains a worked example or an arithmetic derivation

#### Scenario: SumByKeyRun's parameters are documented

- **WHEN** the `SumByKeyRun` declaration is inspected
- **THEN** `input_count`, `num_channels`, `max_key_value`, `record_stride`, `gather_payload` and `entry_count` each carry a description of what it means, where its value comes from and, where one exists, its valid range
- **AND** the descriptions agree with the push block the two including shaders pass it from

#### Scenario: The radix shaders' headers state what each file does

- **WHEN** the radix sort shaders are inspected
- **THEN** each header states the pass it performs, the arrays it reads and writes, and the invariant that keeps the pass stable or the histogram complete
- **AND** the copy-back's header states that it copies only the elements below the call's element count
