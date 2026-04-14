#ifndef RENDER_RESOURCE_STATICMESHRENDERERPROVIDER_INCLUDED
#define RENDER_RESOURCE_STATICMESHRENDERERPROVIDER_INCLUDED

#include "IRenderResourceProvider.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"

#include <unordered_map>

namespace Engine {
    class AssetRef;

    namespace RenderSystemState {
        class StaticMeshRendererProvider final : public IRenderResourceProvider {
            struct MeshSubmeshKey {
                GUID mesh_guid{};
                uint32_t submesh_index{};

                bool operator==(const MeshSubmeshKey &rhs) const noexcept {
                    return mesh_guid == rhs.mesh_guid && submesh_index == rhs.submesh_index;
                }
            };

            struct MeshSubmeshKeyHash {
                size_t operator()(const MeshSubmeshKey &key) const noexcept {
                    auto h = std::hash<GUID>{}(key.mesh_guid);
                    h ^= static_cast<size_t>(key.submesh_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
                    return h;
                }
            };

            std::unordered_map<MeshSubmeshKey, uint32_t, MeshSubmeshKeyHash> m_records{};
            std::unordered_map<GUID, std::weak_ptr<StaticHomogeneousMesh::StaticHMeshSharedDataBlock>> m_shared_blocks{};

        public:
            std::type_index GetTypeID() const noexcept override;
            RenderResourceHandle Acquire(
                RenderResourceManager &manager,
                RenderSystem &system,
                GUID guid,
                const RenderResourceAcquireContext &context
            ) override;
            void *Resolve(RenderResourceManager &manager, RenderResourceHandle handle) const noexcept override;
            bool EnsureReady(RenderResourceManager &manager, RenderSystem &system, RenderResourceHandle handle) override;
            void OnRecordDestroy(GUID guid, uint32_t submesh_index) noexcept override;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_STATICMESHRENDERERPROVIDER_INCLUDED
