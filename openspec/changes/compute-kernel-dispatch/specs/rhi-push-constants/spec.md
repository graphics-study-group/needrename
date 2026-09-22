# rhi-push-constants

## REMOVED Requirements

### Requirement: ComputeStage declares the push-constant range in its pipeline layout

**Reason**: `ComputeStage` is deleted. The requirement's substance — a compute pipeline's layout declares a push-constant range exactly covering the reflected block size, and declares none when the shader has no push-constant block — is preserved, not dropped; it is restated as *Push constants are declared from reflection and recorded by dispatch* in the `rhi-compute-kernel` capability, which owns the kernel-side push-constant contract. Removing it here avoids two capabilities stating the same contract with different subjects.

**Migration**: A caller that relied on `ComputeStage::GetPushConstantSize()` reads the reflected push-constant size from the kernel instead. A caller that relied on the pipeline layout having the range observes the same range on the kernel's pipeline layout. Verify against the kernel capability's *Shader with a push block gets a matching range* and *Shader without a push block gets no range* scenarios.

### Requirement: PushConstants helper records values

**Reason**: `Rhi::PushConstants` and `Rhi::BindComputeResource` are both deleted, so the requirement names only non-existent API. Its two normative parts are both carried forward elsewhere: recording a value of type `T` at offset 0 with an assertion that it fits the reflected block size becomes the kernel dispatch's push-constant behaviour (in `rhi-compute-kernel`), and defaulting a rotation slot to 0 becomes moot because no dispatch takes a slot at all (see the removed `rhi-compute-resource-binding` capability).

**Migration**: Replace `Rhi::PushConstants(cb, stage, value)` plus the following bind/dispatch calls with a single kernel dispatch that receives the same value. The recorded bytes are unchanged: `sizeof(T)` at offset 0. The debug assertion that `sizeof(T)` does not exceed the reflected size is preserved by dispatch. Verify with the kernel capability's *Recorded push value reaches the shader* and *Oversized push value is caught in debug builds* scenarios.
