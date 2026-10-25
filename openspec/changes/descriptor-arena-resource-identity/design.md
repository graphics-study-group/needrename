# Design: descriptor-arena-resource-identity

## Context

The descriptor arena keys its resident cache on binding content that currently compares raw `VkBuffer` / `VkImageView` / `VkSampler` handle values (`ResolvedBinding::operator==` and `HashContent`). Drivers recycle handle values after destruction, so a stale entry can alias a later resource. See `proposal.md` for the failure this caused and `specs/rhi-resource-identity/spec.md` / `specs/rhi-descriptor-arena/spec.md` for the required behaviour.

Constraints that shape the approach:

- Call sites already pass RHI wrapper objects (`DeviceBuffer &`, `Texture &`) into binding dictionaries; the raw handle is extracted inside `ComputeKernelResource` and `ShaderResourceBinding`. The leak is narrow — three internal structures — so the fix can be narrow too.
- `ComputeBuffer::Reallocate` / `EnsureCapacity` replace the underlying `VkBuffer` while the C++ wrapper object survives at the same address; identity must therefore follow the Vulkan object, not the wrapper.
- This bug was hard to diagnose precisely because Debug and Release behaved differently (validation layer changes the allocation sequence). Any fix or guard must not introduce configuration-dependent behaviour.
- The engine already has an identity philosophy at other layers (`ObjectHandle`, asset GUIDs): identity is explicit and never a raw pointer/handle. The RHI layer is brought in line with it.

## Goals / Non-Goals

**Goals:**

- Two distinct resources are never conflated anywhere binding content is compared or hashed, regardless of handle values.
- The type system prevents a bare handle from ever serving as binding identity again.
- The fix behaves identically in Debug and Release, with and without the validation layer.

**Non-Goals:**

- Wrapping record-time handles (`VkCommandBuffer`, `VkImage`, `VkFence`, sync primitives) or renaming existing raw-handle getters.
- Any debug-only liveness registry or self-check assert (rejected: Debug/Release behavioural divergence).
- Changing the pool layer (`AcquireRawSet`), epoch-based reclamation, or the ImGui interop path.
- Lifetime management for callers that bind destroyed or moved resources; that remains caller-UB with a documented contract.

## Decisions

### Typed binding references carrying object identity (level 2)

`ResourceRef.h` (new, `engine/Rhi/Device/`) defines `ObjectId` (a `uint64_t`-based strong type, monotonic, never reused) and `ResourceRef`-style references — `BufferRef`, `ViewRef`, `SamplerRef`, `DescriptorSetLayoutRef` — each holding `{handle, ObjectId}`, constructible **only** from the owning wrapper. Equality and hashing use identity only; the handle is carried solely to write descriptors and record commands.

*Alternatives:* (1) keep raw handles flowing and only swap the key to an id — fixes the bug but leaves the trap armed for the next `HashContent`; (2) full handle typing across the whole engine — weeks of churn on record-time code for zero additional safety in cache keys. Level 2 makes identity a required part of the type while leaving record-time currency untouched.

### Identity is minted per Vulkan object, at creation

One global monotonic counter; mint sites are exactly the creation sites: `BufferAllocation` / `ImageAllocation` construction (the sole `vmaCreateBuffer` path), `Texture::GetImageView`'s view cache entries, and `ImmutableResourceCache`'s samplers and descriptor-set layouts. Storage replacement creates a new allocation and therefore a new identity automatically; wrapper moves carry the identity along.

*Alternative rejected:* identity per logical resource (the wrapper) — it does not distinguish a buffer from its own reallocated predecessor, so the key would need a generation field to stay correct, which is per-object identity under another name. It also collides two different views of one texture.

### Refs stop at the arena boundary

`DescriptorArena::ResolveLayout` returns a `DescriptorSetLayoutRef`; `Acquire` / `AcquireRawSet` accept refs; `ComputeKernel`, `MaterialLibrary`, `CameraManager` and `SceneDataManager` store the ref their `ResolveLayout` call returned. Public getters keep returning raw handles (record-time currency); where a caller needs the ref (e.g. `MaterialTemplate` feeding `ShaderResourceBinding`), a ref-returning accessor is added beside the existing getter. No renames.

### ShaderResourceBinding resolves at acquisition

`BindBuffer` / `BindTexture` store the wrapper pointer plus the binding window (`{DeviceBuffer*, offset, size}` / `{Texture*, range}`); `GetDescriptorSet` resolves handle and identity at acquisition. Storage replacement self-heals. The documented contract: a bound resource is neither moved nor destroyed while a declared binding may name it. This is no stronger than the class's existing "holds no ownership of resources" contract in practice (bound resources already live in stable slots), and it removes the need for callers to rebind after `EnsureCapacity`.

*Alternative rejected:* snapshot the ref at bind time — simple, but every `EnsureCapacity` on a bound buffer silently leaves a dangling binding that mints a descriptor set for a destroyed object.

### Hash collisions keep both entries (chained)

The resident store maps a key to a small chain of entries; a lookup picks the entry whose stored content is exactly the requested content. Collisions are astronomically rare, so chains stay at length ~1–2 and both contents remain cached. The insert path can no longer fail and no longer returns another content's set.

*Alternatives:* replacing the old entry would need epoch-guarded retirement of the displaced set and loses its cacheability; not caching colliding contents leaks sets with no eviction path.

### No configuration-dependent behaviour

The rejected liveness registry / debug asserts would fire only in Debug, reintroducing exactly the Debug-vs-Release divergence that made this bug expensive to diagnose (and that hid the insert-collision defect behind `assert(ok)`). All behaviour introduced by this change compiles identically in every configuration; the insert-collision fix in particular is plain control flow, not an assert.

## Risks / Trade-offs

- [A caller binds a moved or destroyed resource] → Undefined behaviour, documented as a hard contract at `ShaderResourceBinding` and `ComputeKernelResource`. No release mitigation by decision; the contract matches the class's existing no-ownership stance. Misuse yields a stale binding rather than cache aliasing.
- [Chain indirection in the resident store] → One extra indirection on lookup; chains are near-universally length 1. Negligible against the existing full-content compare on every hit.
- [Ref plumbing touches four `ResolveLayout` callers and the two binding flows] → Mechanical and compile-checked; render-layer call sites keep their raw getters, so the diff stays inside `engine/Rhi` plus a handful of stored members.
- [Someone later reintroduces handle-based comparison] → `ResolvedBinding` holds refs whose `operator==` is identity-only; there is no handle left in the key to compare.
- [ImmutableResourceCache assumptions change (a sampler is ever destroyed)] → Identity is decoupled from that assumption; keys stay correct even if immortality ever ends.

## Migration Plan

Three commits, each independently revertible: (1) identity infrastructure, binding flows and arena keying — the fix proper; (2) chained insert for hash collisions; (3) the handle-recycling stress test. No data migration; verification gates each commit on the three regression tests across Debug/Release × validation-layer on/off, plus full ctest.
