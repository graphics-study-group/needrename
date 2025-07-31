#ifndef RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED

#include "Render/Renderer/VertexStruct.h"
#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"

namespace Engine{
    class AssetRef;
    
    /// @brief A homogeneous mesh of only one material at runtime, constructed from mesh asset.
    class HomogeneousMesh {
        std::weak_ptr <RenderSystem> m_system;
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

        /**
         * @brief Create a staging buffer containing all vertices data.
         * 
         * This method is automatically called on mesh submission, and you typically do
         * not need to call it manually.
         */
        Buffer CreateStagingBuffer() const;

        /**
         * @brief Get vertex index count viz. how many vertices are drawn in the draw call.
         */
        uint32_t GetVertexIndexCount() const;

        /**
         * @brief Get vertex count viz. how many distinct vertices data are there.
         */
        uint32_t GetVertexCount() const;

        /**
         * @brief Get expected buffer size. The buffer contains all vertex attributes
         * and indices used for draw calls.
         */
        uint64_t GetExpectedBufferSize() const;

        /**
         * @brief Get the underlying buffer, which contains all vertex data.
         */
        const Buffer & GetBuffer() const;

        /**
         * @brief Get vertex attribute buffers, along with their offsets in the buffer.
         * 
         * Our vertex attributes are allocated in the same buffer with different offsets:
         * ```
         * POSITION ... | BASICATTR ... | EXTENDEDATTR ... | SKINNEDATTR ... | INDEX ...
         * ^ Offset 0   | ^ Offset 1    | ^ Offset 2       | ^ Offset 3      | ^ Offset 4
         * ```
         * Note that index buffer have a different element count of the rest of buffers.
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
         * Note that index buffer have a different element count of the rest of buffers.
         */
        std::pair <vk::Buffer, vk::DeviceSize>
        GetIndexBufferInfo() const;

        /**
         * @brief Query `VkPipelineVertexInputStateCreateInfo` associated with the mesh type.
         */
        static vk::PipelineVertexInputStateCreateInfo GetVertexInputState(MeshVertexType type = MeshVertexType::Basic);
    };
};

#endif // RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
