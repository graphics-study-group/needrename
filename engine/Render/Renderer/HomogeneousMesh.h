#ifndef RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED

#include "Render/Renderer/VertexStruct.h"
#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"

namespace Engine{
    class AssetRef;
    
    /// @brief A homogeneous mesh of only one material at runtime, constructed from mesh asset.
    class HomogeneousMesh {
        struct impl;
        std::unique_ptr <impl> pimpl;
    public:
        enum class MeshVertexType {
            Position,
            Basic,
            Extended,
            Skinned
        };

        HomogeneousMesh(
            std::weak_ptr <RenderSystem> system, 
            std::shared_ptr<AssetRef> mesh_asset, 
            size_t submesh_idx,
            MeshVertexType type = MeshVertexType::Basic
        );
        ~HomogeneousMesh();

        void Prepare();

        /// @brief Check if the vertex buffer of the mesh needs to be commited to GPU.
        /// The flag is always set to `false` after this check.
        /// @return whether commitment is needed.
        bool NeedCommitment();

        Buffer CreateStagingBuffer() const;
        uint32_t GetVertexIndexCount() const;
        uint32_t GetVertexCount() const;
        uint64_t GetExpectedBufferSize() const;

        const Buffer & GetBuffer() const;

        /**
         * @brief Get vertex attribute buffers, along with their offsets in the buffer.
         * 
         * Our vertex attributes are allocated in the same buffer with different offsets:
         * ```
         * POSITION ... | BASICATTR ... | EXTENDEDATTR ... | SKINNEDATTR ... | INDEX ...
         * ^ Offset 0   | ^ Offset 1    | ^ Offset 2       | ^ Offset 3      | ^ Offset 4
         * ```
         */
        std::pair <vk::Buffer, std::vector<vk::DeviceSize>>
        GetVertexBufferInfo() const;

        /**
         * @brief Get vertex index buffer, along with its offset in the buffer.
         * 
         * Our vertex attributes along with indices are allocated in the same buffer with different offsets:
         * ```
         * POSITION ... | BASICATTR ... | EXTENDEDATTR ... | SKINNEDATTR ... | INDEX ...
         * ^ Offset 0   | ^ Offset 1    | ^ Offset 2       | ^ Offset 3      | ^ Offset 4
         * ```
         */
        std::pair <vk::Buffer, vk::DeviceSize>
        GetIndexBufferInfo() const;

        static vk::PipelineVertexInputStateCreateInfo GetVertexInputState(MeshVertexType type = MeshVertexType::Basic);

    protected:
        std::weak_ptr <RenderSystem> m_system;
    };
};

#endif // RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
