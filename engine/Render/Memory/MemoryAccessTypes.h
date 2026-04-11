#ifndef RENDER_MEMORY_MEMORYACCESSTYPES
#define RENDER_MEMORY_MEMORYACCESSTYPES

#include "Core/flagbits.h"

namespace Engine {
    /**
     * @brief Access types of memory.
     */
    namespace MemoryAccessTypes {
        /**
         * @brief Access types of buffers.
         */
        enum class MemoryAccessTypeBufferBits {
            None = 0,
            /// Read as indirect draw command buffer.
            IndirectDrawRead = 1 << 0,
            /// Read as index buffer.
            IndexRead = 1 << 1,
            /// Read as vertex attribute buffer.
            VertexRead = 1 << 2,
            /// Read as uniform buffer.
            ShaderRead = 1 << 3,
            /// Read as texel uniform buffer via a sampler.
            /// @note Unused.
            ShaderSampled = 1 << 4,
            /// Read as storage buffer.
            ShaderRandomRead = 1 << 5,
            /// Write as storage buffer.
            ShaderRandomWrite = 1 << 6,
            /// Read by transfer (i.e. copy) operations.
            TransferRead = 1 << 7,
            /// Write by transfer (i.e. copy) operations.
            TransferWrite = 1 << 8,
            /// Access by the CPU side.
            /// Basically unused.
            HostAccess = 1 << 9
        };

        /**
         * @brief Access types of images.
         */
        enum class MemoryAccessTypeImageBits {
            None = 0,
            /// Color attachment read, performed during loading, blending, etc.
            ColorAttachmentRead = 1 << 0,
            /// Color attachment write.
            ColorAttachmentWrite = 1 << 1,
            /// Depth-stencil attachment read, performed mainly during loading.
            DepthStencilAttachmentRead = 1 << 2,
            /// Depth-stencil attachment write.
            DepthStencilAttachmentWrite = 1 << 3,
            /// Read by transfer routine like copied from.
            TransferRead = 1 << 4,
            /// Write by transfer routine like blitted to.
            TransferWrite = 1 << 5,
            /// Read as being sampled by a sampler.
            ShaderSampledRead = 1 << 6,
            /// Random read as a storage image.
            ShaderRandomRead = 1 << 7,
            /// Random write as a storage image.
            ShaderRandomWrite = 1 << 8,

            DepthStencilAttachmentDefault = (DepthStencilAttachmentRead | DepthStencilAttachmentWrite),
            ColorAttachmentDefault = (ColorAttachmentRead | ColorAttachmentWrite),
            ShaderRandomDefault = (ShaderRandomRead | ShaderRandomWrite)
        };

        /// Bit flags for `MemoryAccessTypeBufferBits`.
        using MemoryAccessTypeBuffer = Flags<MemoryAccessTypeBufferBits>;
        /// Bit flags for `MemoryAccessTypeImageBits`.
        using MemoryAccessTypeImage = Flags<MemoryAccessTypeImageBits>;
    } // namespace MemoryAccessTypes

    using MemoryAccessTypeBufferBits = MemoryAccessTypes::MemoryAccessTypeBufferBits;
    using MemoryAccessTypeImageBits = MemoryAccessTypes::MemoryAccessTypeImageBits;
    /// Bit flags for `MemoryAccessTypeBufferBits`.
    using MemoryAccessTypeBuffer = MemoryAccessTypes::MemoryAccessTypeBuffer;
    /// Bit flags for `MemoryAccessTypeImageBits`.
    using MemoryAccessTypeImage = MemoryAccessTypes::MemoryAccessTypeImage;
} // namespace Engine

#endif // RENDER_MEMORY_MEMORYACCESSTYPES
