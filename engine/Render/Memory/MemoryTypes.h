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
        HostRandomAccess        = 1 << 16,
        // This buffer will only be sequentially written by the CPU.
        HostSequentialAccess    = 1 << 17,
        // This buffer supports image-like access (i.e. texel buffer).
        // Ignored for now.
        ImagelikeAccess         = 1 << 18,

        // Short-hand for `CopyFrom | HostSequentialAccess`.
        StagingToDevice         = CopyFrom | HostSequentialAccess,
        // Short-hand for `CopyTo | HostRandomAccess`.
        ReadbackFromDevice      = CopyTo | HostRandomAccess,
        // Short-hand for `ShaderReadOnly | HostRandomAccess`.
        HostAccessibleUniform   = ShaderReadOnly | HostRandomAccess
    };
    using BufferType = Flags<BufferTypeBits>;

    enum class ImageMemoryTypeBits {
        // This image can be copied/blitted from.
        CopyFrom                = 1 << 0,
        // This image can be copied/blitted to and cleared.
        CopyTo                  = 1 << 1,
        // This image can be sampled by a sampler.
        ShaderSampled           = 1 << 2,
        // This image can be randomly access as a storage image.
        ShaderRandomAccess      = 1 << 3,
        // This image can be used in a framebuffer as color part.
        ColorAttachment         = 1 << 4,
        // This image can be used in a framebuffer as depth/stencil part.
        DepthStencilAttachment  = 1 << 5,

        // The access to this image can be atomic in addition to random access.
        ShaderAtomicAccess      = 1 << 16,
        // The access to this image view can be backed by a texel buffer.
        // The buffer is assumed to be a uniform buffer if sampled, or storage
        // buffer if randomly accessed.
        // Ignored for now.
        BackedByBuffer          = 1 << 17,

        // Short-hand for `CopyFrom | CopyTo | ShaderSampled | ShaderRandomAccess | ColorAttachment`.
        DefaultColorAttachment = CopyFrom | CopyTo | ShaderSampled | ShaderRandomAccess | ColorAttachment,
        // Short-hand for `CopyFrom | CopyTo | ShaderSampled | DepthStencilAttachment`.
        DefaultDepthAttachment = CopyFrom | CopyTo | ShaderSampled | DepthStencilAttachment,
        // Short-hand for `CopyTo | ShaderSampled`.
        DefaultTexture  = CopyTo | ShaderSampled
    };
    using ImageMemoryType = Flags<ImageMemoryTypeBits>;
};

#endif // RENDER_MEMORY_MEMORYTYPES_INCLUDED
