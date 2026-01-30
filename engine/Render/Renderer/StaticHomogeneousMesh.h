#ifndef RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED

#include "IVertexBasedRenderer.h"

#include <memory>
#include <vector>

namespace Engine {
    class MeshAsset;
    namespace RenderSystemState {
        class AllocatorState;
    }

    /**
     * @brief A static vertex-based mesh renderer that cannot be animated.
     * 
     * This class of renderer does not own any buffers.
     * 
     * All static homogeneous meshes from the same submesh of the same asset
     * shares their index and vertex attribute buffer. The life-time and
     * content of such buffer are managed by the `RendererManager`.
     */
    class StaticHomogeneousMesh : public IVertexBasedRenderer {
    public:
        struct StaticHMeshSharedDataBlock {
            uint32_t refcnt{0};

            struct PerSubmeshData {
                VertexAttribute attributes;
                uint32_t vertex_attribute_count;
                uint32_t index_count;
                
                std::vector <uint32_t> attribute_offsets;

                std::unique_ptr <DeviceBuffer> vi_buffer;
            };

            std::vector <PerSubmeshData> submeshes;
        };

    private:
        uint32_t submesh_index;
        const MeshAsset & mesh_asset;
        StaticHMeshSharedDataBlock & data_block_ref;

    public:
        StaticHomogeneousMesh(
            uint32_t index,
            const MeshAsset & asset,
            StaticHMeshSharedDataBlock & data_block
        ) : submesh_index{index}, mesh_asset(asset), data_block_ref{data_block} {
            data_block_ref.refcnt += 1;
        };
        virtual ~StaticHomogeneousMesh() noexcept {
            data_block_ref.refcnt -= 1;
        };

        // Disable copy ctor and assignment to avoid messing with ref counter.
        StaticHomogeneousMesh(const StaticHomogeneousMesh &) = delete;
        void operator= (const StaticHomogeneousMesh &) = delete;

        uint32_t GetIndexCount() const noexcept override {
            assert(data_block_ref.submeshes[submesh_index].vi_buffer);
            return data_block_ref.submeshes[submesh_index].index_count;
        }

        uint32_t GetVertexAttributeCount() const noexcept override {
            assert(data_block_ref.submeshes[submesh_index].vi_buffer);
            return data_block_ref.submeshes[submesh_index].vertex_attribute_count;
        }

        VertexAttribute GetVertexAttributeFormat() const noexcept override {
            assert(data_block_ref.submeshes[submesh_index].vi_buffer);
            return data_block_ref.submeshes[submesh_index].attributes;
        }

        void FillVertexAttributeBufferBindings(
            std::vector <BufferBindingInfo> & bindings
        ) const noexcept override {
            const auto & submesh_ref = data_block_ref.submeshes[submesh_index];
            assert(submesh_ref.vi_buffer);
            bindings.reserve(submesh_ref.attribute_offsets.size());
            for (auto offset : submesh_ref.attribute_offsets) {
                bindings.push_back(
                    {
                        submesh_ref.vi_buffer.get(),
                        offset
                    }
                );
            }
            // Last offset is for indices.
            bindings.pop_back();
        }

        BufferBindingInfo GetIndexBufferBinding() const noexcept override {
            assert(data_block_ref.submeshes[submesh_index].vi_buffer);
            return {
                data_block_ref.submeshes[submesh_index].vi_buffer.get(),
                data_block_ref.submeshes[submesh_index].attribute_offsets.back()
            };
        }

        bool IsReady() const noexcept override {
            return static_cast<bool>(data_block_ref.submeshes[submesh_index].vi_buffer);
        }

        void Remove() noexcept override {
            // Do nothing.
            // Its resource's lifetime is managed by the Renderer Manager.
        }
        void Submit(
            const RenderSystemState::AllocatorState &,
            RenderSystemState::SubmissionHelper &
        ) override;
    };
}

#endif // RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED
