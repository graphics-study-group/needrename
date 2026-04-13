#include "StaticHomogeneousMesh.h"

#include "Render/Resource/StaticMeshResource.h"

namespace Engine {
    StaticHomogeneousMesh::StaticHomogeneousMesh(
        uint32_t index, std::shared_ptr<RenderSystemState::StaticMeshResource> resource
    ) : submesh_index{index}, m_resource(std::move(resource)) {
        assert(m_resource);
    }

    void StaticHomogeneousMesh::Submit(
        const RenderSystemState::AllocatorState &allocator, RenderSystemState::SubmissionHelper &helper
    ) {
        m_resource->EnsureUploaded(submesh_index, allocator, helper);
    }

} // namespace Engine
