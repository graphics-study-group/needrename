#ifndef ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED
#define ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED

#include <cstdint>
#include <typeindex>

namespace Engine {
    namespace RenderSystemState {
        /**
         * @brief Opaque handle for resources managed by RenderResourceManager.
         *
         * A handle is valid only if index/generation/type_id matches a live record.
         *
         * Design rationale:
         * - index identifies the slot in manager-owned record storage for O(1) lookup.
         * - generation is a per-slot version to reject stale handles after slot reuse.
         * - type_id prevents resolving a handle through the wrong provider/payload type.
         *
         * Why index + generation are both needed:
         * - Slots are recycled after deferred reclamation.
         * - A stale handle may still carry a valid historical index.
         * - generation mismatch lets us distinguish "same slot" from "same resource".
         */
        struct RenderResourceHandle {
            /// Index into resource record storage.
            uint32_t index{0xFFFFFFFFu};
            /// Slot version used to detect stale handles after slot reuse.
            uint32_t generation{0};
            /// Type index for quick runtime type checking during handle resolution.
            bool is_acuired{false};

            bool IsValid() const noexcept {
                return index != 0xFFFFFFFFu;
            }
        };

        template <typename ResourceType>
        struct ResourceTraits;
    } // namespace RenderSystemState

    /************************************************************************************
     * Define specialized ResourceTraits for each resource type to provide type-specific
     * information and behavior.
     ************************************************************************************/

    class MaterialInstance;
    class MaterialLibrary;
    class StaticMeshResource;

    namespace RenderSystemState {
        template <typename ResourceType>
        concept SupportedRenderResource = requires {
            std::is_same_v<ResourceType, MaterialInstance> || std::is_same_v<ResourceType, MaterialLibrary>
                || std::is_same_v<ResourceType, StaticMeshResource>;
        };

        class MaterialInstanceManager;
        struct MaterialInstanceHandle : public RenderResourceHandle {};

        template <>
        struct ResourceTraits<MaterialInstance> {
            using HandleType = MaterialInstanceHandle;
            using ManagerType = MaterialInstanceManager;
        };

        class MaterialLibraryManager;
        struct MaterialLibraryHandle : public RenderResourceHandle {};

        template <>
        struct ResourceTraits<MaterialLibrary> {
            using HandleType = MaterialLibraryHandle;
            using ManagerType = MaterialLibraryManager;
        };

        class StaticMeshResourceManager;
        struct StaticMeshResourceHandle : public RenderResourceHandle {};

        template <>
        struct ResourceTraits<StaticMeshResource> {
            using HandleType = StaticMeshResourceHandle;
            using ManagerType = StaticMeshResourceManager;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED
