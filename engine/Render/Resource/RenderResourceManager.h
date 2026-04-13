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
        /**
         * @brief Runtime kind tag for resources stored in RenderResourceManager.
         */
        enum class RenderResourceKind : uint8_t {
            /// Invalid or empty handle.
            Invalid = 0,
            /// Material library resource created from MaterialLibraryAsset.
            MaterialLibrary,
            /// Material instance resource created from MaterialAsset.
            MaterialInstance,
            /// Static mesh renderer resource created from MeshAsset submesh.
            StaticMeshRenderer,
        };

        /**
         * @brief Opaque handle for resources managed by RenderResourceManager.
         *
         * A handle is valid only if index/generation/kind matches a live record.
         */
        struct RenderResourceHandle {
            /// Index into resource record storage.
            uint32_t index{0xFFFFFFFFu};
            /// Generation used to detect stale handles.
            uint32_t generation{0};
            /// Runtime type tag for safe resolve.
            RenderResourceKind kind{RenderResourceKind::Invalid};

            /**
             * @brief Check whether this handle is structurally valid.
             */
            bool IsValid() const noexcept {
                return kind != RenderResourceKind::Invalid && index != 0xFFFFFFFFu;
            }
        };

        /**
         * @brief Unified runtime manager for render resources used by draw path.
         *
         * This manager owns resource records, performs reference counting,
         * supports frame-delayed reclamation, and resolves typed payload pointers
         * from RenderResourceHandle.
         */
        class RenderResourceManager final {
            RenderSystem &m_system;
            struct impl;
            std::unique_ptr<impl> pimpl;

        public:
            /**
             * @brief Create manager bound to a RenderSystem instance.
             */
            RenderResourceManager(RenderSystem &system);
            ~RenderResourceManager();

            /**
             * @brief Acquire or create a material library resource by asset GUID.
             * @param library_guid GUID of MaterialLibraryAsset.
             * @return Handle to a MaterialLibrary resource.
             */
            RenderResourceHandle AcquireMaterialLibrary(GUID library_guid);

            /**
             * @brief Acquire or create a material instance resource by asset GUID.
             * @param material_guid GUID of MaterialAsset.
             * @return Handle to a MaterialInstance resource.
             */
            RenderResourceHandle AcquireMaterialInstance(GUID material_guid);

            /**
             * @brief Acquire or create a static mesh renderer resource.
             * @param mesh_guid GUID of MeshAsset.
             * @param submesh_index Submesh index inside the mesh asset.
             * @param eagerly_loaded If true, schedule/perform preparation immediately.
             * @return Handle to a static mesh renderer resource.
             */
            RenderResourceHandle AcquireStaticMeshRenderer(GUID mesh_guid, uint32_t submesh_index, bool eagerly_loaded);

            /**
             * @brief Decrease strong reference count of a resource handle.
             *
             * Actual destruction is delayed by frame-in-flight policy.
             */
            void Release(RenderResourceHandle handle);

            /**
             * @brief Advance one frame for deferred reclamation.
             */
            void TickFrame();

            /**
             * @brief Resolve a material library pointer from handle.
             * @return Pointer on success, nullptr if type or lifetime check fails.
             */
            MaterialLibrary *ResolveMaterialLibrary(RenderResourceHandle handle) const noexcept;

            /**
             * @brief Resolve a material instance pointer from handle.
             * @return Pointer on success, nullptr if type or lifetime check fails.
             */
            MaterialInstance *ResolveMaterialInstance(RenderResourceHandle handle) const noexcept;

            /**
             * @brief Resolve a renderer pointer from handle.
             * @return Pointer on success, nullptr if type or lifetime check fails.
             */
            IVertexBasedRenderer *ResolveRenderer(RenderResourceHandle handle) const noexcept;

            /**
             * @brief Ensure static mesh renderer resource is prepared for drawing.
             * @return True if renderer is ready after the call.
             */
            bool EnsureRendererReady(RenderResourceHandle handle);
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_RENDERRESOURCEMANAGER_INCLUDED
