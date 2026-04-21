#ifndef ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED
#define ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED

#include <cstdint>
#include <typeindex>

namespace Engine {
    namespace RenderSystemState {
        /**
         * @brief Non-owning typed handle payload for resources managed by render-resource managers.
         *
         * A handle is valid only if index/generation matches a live manager record.
         *
         * Design rationale:
         * - index identifies the slot in manager-owned record storage for O(1) lookup.
         * - generation is a per-slot version to reject stale handles after slot reuse.
         * - is_acquired tracks whether this handle has participated in manager refcount.
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
            /// Whether this handle currently contributes one reference in manager refcount.
            bool is_acquired{false};

            ~RenderResourceHandle() {
                assert(!is_acquired && "Handle should be released before destruction");
            }

            bool IsValid() const noexcept {
                return index != 0xFFFFFFFFu;
            }
        };

        template <typename ResourceType>
        struct ResourceTraits;
    } // namespace RenderSystemState

    class MaterialInstance;
    class MaterialLibrary;
    class StaticMeshResource;

    namespace RenderSystemState {

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
