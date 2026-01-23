#ifndef RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED

namespace vk {
    class Buffer;
    class PipelineVertexInputStateCreateInfo;
}

namespace Engine {
    class AssetRef;
    class Buffer;
    class VertexAttribute;
    
    namespace RenderSystemState {
        class AllocatorState;
    }

    /// @brief A homogeneous mesh of only one material at runtime, constructed from mesh asset.
    class HomogeneousMesh {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:

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
            size_t submesh_idx
        );
        ~HomogeneousMesh();

        /**
         * @brief Create a staging buffer containing all vertices data
         * with the supplied allocator.
         * 
         * This method is automatically called on mesh submission, and you typically do
         * not need to call it manually.
         * 
         * Vertex data are copied into the staging buffer immediately.
         */
        std::unique_ptr <Buffer> CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const;

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
         * The final element in this buffer is the offset of the index buffer.
         * You might wish to remove it before binding vertex input buffers.
         * 
         * Our vertex attributes are allocated in the same buffer with different offsets:
         * ```
         * ATTR0 ...  | ATTR1 ...  | ATTR2 ...  | INDEX ...
         * ^ Offset 0 | ^ Offset 1 | ^ Offset 2 | ^ Offset 3
         * ```
         * Note that index buffer have a different element count of the rest of buffers.
         */
        std::pair <vk::Buffer, std::vector<uint64_t>> GetVertexBufferInfo() const;

        /**
         * @brief Get vertex index buffer, along with its offset in the buffer.
         * 
         * Our
         * vertex attributes along with indices are allocated in the same buffer with different offsets:
         * ```
         * ATTR0 ...  | ATTR1 ...  | ATTR2 ...  | INDEX ...
         * ^ Offset 0 | ^ Offset 1 | ^ Offset 2 | ^ Offset 3
         * ```
         * Note that index
         * buffer have a different element count of the rest of buffers.
         */
        std::pair <vk::Buffer, uint64_t> GetIndexBufferInfo() const;

        /**
         * @brief
         */
        VertexAttribute GetVertexAttribute() const;
    };
}; // namespace Engine

#endif // RENDER_RENDERER_HOMOGENEOUSMESH_INCLUDED
