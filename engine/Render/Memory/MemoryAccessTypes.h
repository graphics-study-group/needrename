#ifndef RENDER_MEMORY_MEMORYACCESSTYPES_INCLUDED
#define RENDER_MEMORY_MEMORYACCESSTYPES_INCLUDED

#include "Core/flagbits.h"

namespace Engine {
    enum class MemoryAccessTypeBufferBits {
        None                    = 0,
        // Read as indirect draw command buffer.
        IndirectDrawRead        = 1 << 0,
        // Read as index buffer.
        IndexRead               = 1 << 1,
        // Read as vertex attribute buffer.
        VertexRead              = 1 << 2,
        // Read as uniform buffer.
        ShaderRead              = 1 << 3,
        // Read as texel uniform buffer via a sampler.
        ShaderSampled           = 1 << 4,
        // Read as storage buffer.
        ShaderRandomRead        = 1 << 5,
        // Write as storage buffer.
        ShaderRandomWrite       = 1 << 6,
        // Read by transfer (i.e. copy) operations.
        TransferRead            = 1 << 7,
        // Write by transfer (i.e. copy) operations.
        TransferWrite           = 1 << 8,
        // Access by the CPU side.
        // Basically unused.
        HostAccess              = 1 << 9
    };
    using MemoryAccessTypeBuffer = Flags<MemoryAccessTypeBufferBits>;
}

#endif // RENDER_MEMORY_MEMORYACCESSTYPES_INCLUDED
