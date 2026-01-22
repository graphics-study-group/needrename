#ifndef RENDER_MEMORY_MEMORYTYPES_INCLUDED
#define RENDER_MEMORY_MEMORYTYPES_INCLUDED

#include "Core/flagbits.h"

namespace Engine {
    enum class BufferTypeBits {
        CopyFrom                = 1 << 0,
        CopyTo                  = 1 << 1,
        // This buffer can only be read from shaders (i.e. uniform buffer)
        ShaderReadOnly          = 1 << 2,
        // This buffer can be write from shaders (i.e. storage buffer)
        ShaderWrite             = 1 << 3,
        // This buffer can be used as index buffer.
        Index                   = 1 << 4,
        // This buffer can be used as vertex buffer.
        Vertex                  = 1 << 5,
        // This buffer can be used as indirect draw command.
        IndirectDrawCommand     = 1 << 6,
        // This buffer may be randomly access by the CPU.
        // Also automatically maps allocated memory to the host VM.
        HostRandomAccess        = 1 << 7,
        // This buffer will only be sequentially written by the CPU.
        HostSequentialAccess    = 1 << 8,
        // This buffer supports image-like access (i.e. texel buffer).
        ImagelikeAccess         = 1 << 9,

        StagingToDevice         = CopyFrom | HostSequentialAccess,
        ReadbackFromDevice      = CopyTo | HostRandomAccess,
        HostAccessibleUniform   = ShaderReadOnly | HostRandomAccess
    };
    using BufferType = Flags<BufferTypeBits>;
};

#endif // RENDER_MEMORY_MEMORYTYPES_INCLUDED
