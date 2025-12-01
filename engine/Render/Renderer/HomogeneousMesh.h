#ifndef RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED

namespace vk {
    class Buffer;
    class PipelineVertexInputStateCreateInfo;
}

namespace Engine {
    class AssetRef;
    class Buffer;
    
    namespace RenderSystemState {
        class AllocatorState;
    }

    /// @brief A homogeneous mesh of only one material at runtime, constructed from mesh asset.
    class HomogeneousMesh {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        enum class MeshVertexType {
            Position,
            Basic,
            Extended,
            Skinned
        };

        /**
         * @brief Create the Homogenenous mesh from an asset.
         * The Host-side buffer is allocated, but no data will be uploaded.
         * 
         * To upload any data, you need to create a staging buffer and issue
         * a buffer copy command. This procedure is automatically handled by
         * `RendererManager` if correctly registered.
         */
        HomogeneousMesh(
            const RenderSystemState::AllocatorState & allocator,
            std::shared_ptr<AssetRef> mesh_asset,
            size_t submesh_idx,
            MeshVertexType type = MeshVertexType::Basic
        );
        ~HomogeneousMesh();

        /**
         * @brief Create a staging buffer containing all vertices data
         * with the supplied allocator.
         * 
         * This method is automatically called on mesh submission, and you typically do
         * not need to call it manually.
         */
        Buffer CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const;

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
         * and indices
         * used for draw calls.
         */
        uint64_t GetExpectedBufferSize() const;

        /**
         * @brief Get the underlying buffer, which contains all vertex data.
         */
        const Buffer &GetBuffer() const;

        /**
         * @brief Get vertex attribute buffers, along with their offsets in the buffer.
         * 
         * Our
         * vertex attributes are allocated in the same buffer with different offsets:
         * ```
         * POSITION ... | BASICATTR ... | EXTENDEDATTR ... | SKINNEDATTR ... | INDEX ...
         * ^ Offset 0   | ^ Offset 1    | ^ Offset 2       | ^ Offset 3      | ^ Offset 4
         * ```
         * Note that index buffer have a
         * different element count of the rest of buffers.
         */
        std::pair<vk::Buffer, std::vector<uint64_t>> GetVertexBufferInfo() const;

        /**
         * @brief Get vertex index buffer, along with its offset in the buffer.
         * 
         * Our
         * vertex attributes along with indices are allocated in the same buffer with different offsets:
         * ```
         * POSITION ... | BASICATTR ... | EXTENDEDATTR ... | SKINNEDATTR ... | INDEX ...
         * ^ Offset 0   | ^ Offset 1    | ^ Offset 2       | ^ Offset 3      | ^ Offset 4
         * ```
         * Note that index
         * buffer have a different element count of the rest of buffers.
         */
        std::pair<vk::Buffer, uint64_t> GetIndexBufferInfo() const;

        /**
         * @brief Query `VkPipelineVertexInputStateCreateInfo` associated with the mesh type.
         */
        static vk::PipelineVertexInputStateCreateInfo GetVertexInputState(MeshVertexType type = MeshVertexType::Basic);
    };
}; // namespace Engine

#endif // RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
