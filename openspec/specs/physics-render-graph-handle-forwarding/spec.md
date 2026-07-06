# Physics Render Graph Handle Forwarding (DEPRECATED)

## Purpose

**DEPRECATED** — This spec previously defined how GPU physics collision detection integrated with the RenderGraph system by passing pre-imported buffer handles instead of re-importing buffers within detector passes. With the multi-RG architecture introduced in `xpbd-solver-multi-rg`, each component (solver, detectors) owns its own RenderGraph and imports buffers independently. Cross-RG synchronization is achieved through correct `prev_access` on `ImportExternalResource`.

## Requirements

*All requirements in this spec have been removed. The `PhysicsSceneBufferHandles` struct, `BroadDetectorOutputHandles`, and `NarrowDetectorOutputHandles` are no longer used. Handle forwarding across components was only needed when all passes shared a single builder.*
