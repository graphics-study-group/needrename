# headless-present-pipeline

## MODIFIED Requirements

### Requirement: RenderGraph records only — submission belongs to the frame-completion point

`RenderGraph::RecordIntoMainCommandBuffer` SHALL record passes into the main command buffer without beginning, ending or submitting it. Submission SHALL happen via `RenderSystem::CompleteFrame` → `FrameManager::SubmitFrame`.

#### Scenario: RecordIntoMainCommandBuffer records only

- **WHEN** `RenderGraph::RecordIntoMainCommandBuffer` is called
- **THEN** it records all passes onto the current frame-in-flight main command buffer and returns
- **AND** it does NOT begin the command buffer (the caller began it via `FrameManager::BeginMainCommandBuffer`)
- **AND** it does NOT end the command buffer (`SubmitFrame` ends it)
- **AND** it does NOT submit — neither through the frame manager nor via `vkQueueSubmit2` (submission happens later in `CompleteFrame`)

#### Scenario: Caller submits via CompleteFrame

- **WHEN** a caller has recorded passes
- **THEN** it calls `RenderSystem::CompleteFrame(final_rtt, last_access)` to submit the frame-completion batch and present
- **AND** nothing that depends on the submitted batch having executed runs in the same frame — a caller that needs the result reads it back through the frame manager's readback path on a later frame
