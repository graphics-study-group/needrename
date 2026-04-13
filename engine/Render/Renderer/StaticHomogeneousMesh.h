#ifndef RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED

#include "IVertexBasedRenderer.h"
#include "Render/Resource/StaticMeshResource.h"

#include <memory>
#include <vector>

namespace Engine {
    namespace RenderSystemState {
        class AllocatorState;
    }

    /**
     * @brief A static vertex-based mesh renderer that cannot be animated.
     *
    * This class of renderer does not own GPU buffers directly.

    * All static homogeneous meshes from the same asset share a
    * `StaticMeshResource` managed by the render resource hub.
     *
     * @deprecated Pending for rewrite.
     */
    class StaticHomogeneousMesh : public IVertexBasedRenderer {
    private:
        uint32_t submesh_index;
        std::shared_ptr<RenderSystemState::StaticMeshResource> m_resource;

    public:
        StaticHomogeneousMesh(uint32_t index, std::shared_ptr<RenderSystemState::StaticMeshResource> resource);
        virtual ~StaticHomogeneousMesh() noexcept = default;

        // Disable copy ctor and assignment to keep the renderer proxy immutable.
        StaticHomogeneousMesh(const StaticHomogeneousMesh &) = delete;
        void operator=(const StaticHomogeneousMesh &) = delete;

        uint32_t GetIndexCount() const noexcept override {
            return m_resource->GetIndexCount(submesh_index);
        }

        uint32_t GetVertexAttributeCount() const noexcept override {
            return m_resource->GetVertexAttributeCount(submesh_index);
        }

        VertexAttribute GetVertexAttributeFormat() const noexcept override {
            return m_resource->GetVertexAttributeFormat(submesh_index);
        }

        void FillVertexAttributeBufferBindings(std::vector<BufferBindingInfo> &bindings) const noexcept override {
            m_resource->FillVertexAttributeBufferBindings(submesh_index, bindings);
        }

        BufferBindingInfo GetIndexBufferBinding() const noexcept override {
            return m_resource->GetIndexBufferBinding(submesh_index);
        }

        bool IsReady() const noexcept override {
            return m_resource->IsReady(submesh_index);
        }

        void Remove() noexcept override {
            // Do nothing.
            // Its resource's lifetime is managed by the render resource hub.
        }
        void Submit(const RenderSystemState::AllocatorState &, RenderSystemState::SubmissionHelper &) override;
    };
} // namespace Engine

#endif // RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED
