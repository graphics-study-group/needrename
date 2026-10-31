#ifndef ENGINE_PHYSICS_PHYSICSDISPATCH_INCLUDED
#define ENGINE_PHYSICS_PHYSICSDISPATCH_INCLUDED

#include "Rhi/Command/CommandEncoder.h"

namespace Engine {
    /**
     * @brief Record the dependency barrier between two physics compute dispatches.
     *
     * Every physics pass writes storage buffers that a later pass reads, so this
     * is the boundary each step records. Compute is the only stage involved: no
     * physics pass writes an attachment or a transfer.
     *
     * The barrier is the seam's synchronization mark rather than a Vulkan
     * barrier: a backend that orders work at coarser boundaries carries the
     * ordering there and records nothing here. Keeping the mark at every call
     * site's current position is what keeps the Vulkan command stream identical
     * to what it was before the seam existed.
     *
     * @param encoder Encoder in recording state.
     */
    inline void DispatchBarrier(Rhi::CommandEncoder &encoder) {
        encoder.Barrier();
    }
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSDISPATCH_INCLUDED

