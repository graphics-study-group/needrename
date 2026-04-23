#ifndef RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED
#define RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED

#include "IVertexBasedRenderer.h"
#include "Render/Resource/StaticMeshResource.h"

#include <memory>

namespace Engine {
    /**
     * @brief A lightweight renderer view for one submesh of a StaticMeshResource.
     *
     * GPU data ownership stays in StaticMeshResource. This class only selects
     * one submesh from that shared resource for draw submission.
     */
    class StaticHomogeneousMesh : public IVertexBasedRenderer {
    public:
        using StaticHMeshSharedDataBlock = StaticMeshResource::StaticHMeshSharedDataBlock;

    private:
        uint32_t m_submesh_index{0};
        StaticMeshResource *m_resource{};

    public:
        StaticHomogeneousMesh(uint32_t index, StaticMeshResource *resource);
        virtual ~StaticHomogeneousMesh() noexcept;

        StaticHomogeneousMesh(const StaticHomogeneousMesh &) = delete;
        void operator=(const StaticHomogeneousMesh &) = delete;

        uint32_t GetIndexCount() const noexcept override;

        uint32_t GetVertexAttributeCount() const noexcept override;

        VertexAttribute GetVertexAttributeFormat() const noexcept override;

        void FillVertexAttributeBufferBindings(std::vector<BufferBindingInfo> &bindings) const noexcept override;

        BufferBindingInfo GetIndexBufferBinding() const noexcept override;

        bool IsReady() const noexcept override;
    };
} // namespace Engine

#endif // RENDER_RENDERER_STATICHOMOGENEOUSMESH_INCLUDED
