# rhi-compute-resource-binding

## REMOVED Requirements

### Requirement: ComputeResourceBinding declares rotation depth at construction

**Reason**: The type is deleted. Rotation depth existed to let one binding object serve several in-flight batches by owning one descriptor set and one uniform-buffer slice per slot. Compute kernels no longer hold binding state across submissions: each dispatch supplies its resources directly and obtains its descriptor set from the device's descriptor arena, which records the acquiring epoch against it (see the `descriptor-arena-epoch-buckets` change). Rotation therefore has no consumer, and a caller-declared depth would be a number with nothing to size.

**Migration**: Replace the `ComputeResourceBinding` allocation and its `slot_count` with a kernel dispatch whose resource dictionary is supplied at each dispatch site. The `slot` argument disappears: there is no per-slot state to select. A caller that previously relied on a fixed number of independently addressable slots gains the same isolation from the descriptor arena, which keeps each set valid until the epoch recorded against it has completed.

### Requirement: ComputeResourceBinding exposes neutral slot indices

**Reason**: The type and both of its accessors (`UpdateGPUInfo(slot)`, `GetDescriptorSet(slot)`) are deleted. The constraint these accessors encoded — that the Rhi compute API must contain no render-frame vocabulary — remains in force and has been carried forward verbatim in intent as *No render-frame vocabulary in the kernel interface* in the `rhi-compute-kernel` capability, which also forbids a rotation/slot parameter on dispatch.

**Migration**: A caller that called `UpdateGPUInfo(slot)` before dispatching no longer performs that step: the kernel resolves, obtains and writes its descriptor set as part of dispatch. Scan for `UpdateGPUInfo`, `GetDescriptorSet`, and `slot`/`slot_count` at compute call sites, and check the kernel's dispatch signature against the *No render-frame vocabulary in the kernel interface* scenarios.

### Requirement: ComputeStage forwards slot_count to allocated bindings

**Reason**: `ComputeStage` is deleted, and with it `AllocateResourceBinding`. A kernel is obtained by identity rather than instantiated per owner, so there is no allocation step to forward a depth through.

**Migration**: Replace `ComputeStage` instantiation plus `AllocateResourceBinding(...)` with a request for the kernel of that SPIR-V module. The request takes no depth argument. Components that previously each instantiated their own stage for the same shader now share one kernel.

### Requirement: Existing callers pass their rotation depth explicitly

**Reason**: The requirement exists only to pin the `slot_count` arguments of the deleted constructors. With no rotation concept in the compute path, there is no depth for a caller to declare, and the literal-versus-constant distinction it drew no longer applies. The related protective intent for physics — that no physics component maintains a frame counter or a rotation-depth literal — is re-stated without the deleted APIs in the `physics-push-constants` capability.

**Migration**: Delete every `AllocateResourceBinding(n)` argument and every `FrameManager::FRAMES_IN_FLIGHT`-derived depth passed to a compute binding. Render and editor callers pass their resources to dispatch instead. Verify with a search for `AllocateResourceBinding`, `slot_count`, and `FRAMES_IN_FLIGHT` at compute call sites.
