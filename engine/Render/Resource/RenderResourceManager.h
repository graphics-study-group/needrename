#ifndef RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
#define RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED

#include "Core/guid.h"

#include <cstdint>
#include <memory>

namespace Engine {
    class IVertexBasedRenderer;
    class MaterialInstance;
    class MaterialLibrary;
    class RenderSystem;

    namespace RenderSystemState {
        enum class RenderResourceKind : uint8_t {
            Invalid = 0,
            MaterialLibrary,
            MaterialInstance,
            StaticMeshRenderer,
        };

        struct RenderResourceHandle {
            uint32_t index{0xFFFFFFFFu};
            uint32_t generation{0};
            RenderResourceKind kind{RenderResourceKind::Invalid};

            bool IsValid() const noexcept {
                return kind != RenderResourceKind::Invalid && index != 0xFFFFFFFFu;
            }
        };

        class RenderResourceManager final {
            RenderSystem &m_system;
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            RenderResourceManager(RenderSystem &system);
            ~RenderResourceManager();

            RenderResourceHandle AcquireMaterialLibrary(GUID library_guid);
            RenderResourceHandle AcquireMaterialInstance(GUID material_guid);
            RenderResourceHandle AcquireStaticMeshRenderer(GUID mesh_guid, uint32_t submesh_index, bool eagerly_loaded);

            void Release(RenderResourceHandle handle);
            void TickFrame();

            MaterialLibrary *ResolveMaterialLibrary(RenderResourceHandle handle) const noexcept;
            MaterialInstance *ResolveMaterialInstance(RenderResourceHandle handle) const noexcept;
            IVertexBasedRenderer *ResolveRenderer(RenderResourceHandle handle) const noexcept;

            bool EnsureRendererReady(RenderResourceHandle handle);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
