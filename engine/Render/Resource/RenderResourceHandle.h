#ifndef ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED
#define ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED

#include "Render/render_export.h"
#include <cstdint>
#include <typeindex>
#include <utility>

namespace Engine {
    namespace RenderSystemState {
        template <typename ResourceType>
        struct ResourceTraits;
    }

    class MaterialInstance;
    class MaterialLibrary;
    class StaticMeshResource;

    namespace RenderSystemState {

        class MaterialInstanceManager;
        class MaterialLibraryManager;
        class StaticMeshResourceManager;

        template <typename ResourceType>
        struct RENDER_API RenderResourceHandle {
            uint32_t index{0xFFFFFFFFu};
            uint32_t generation{0};
            bool is_acquired{false};

            RenderResourceHandle() = default;

            RenderResourceHandle(uint32_t idx, uint32_t gen) : index(idx), generation(gen) {
            }

            RenderResourceHandle(uint32_t idx, uint32_t gen, bool acquired) :
                index(idx), generation(gen), is_acquired(acquired) {
            }

            RenderResourceHandle(const RenderResourceHandle &other) :
                index(other.index), generation(other.generation), is_acquired(false) {
            }

            RenderResourceHandle(RenderResourceHandle &&other) noexcept :
                index(other.index), generation(other.generation), is_acquired(other.is_acquired) {
                other.index = 0xFFFFFFFFu;
                other.generation = 0;
                other.is_acquired = false;
            }

            RenderResourceHandle &operator=(RenderResourceHandle other) noexcept {
                using std::swap;
                swap(index, other.index);
                swap(generation, other.generation);
                swap(is_acquired, other.is_acquired);
                return *this;
            }

            ~RenderResourceHandle();

            bool IsValid() const noexcept {
                return index != 0xFFFFFFFFu;
            }
        };

        struct RENDER_API MaterialInstanceHandle : public RenderResourceHandle<MaterialInstance> {
            using RenderResourceHandle::RenderResourceHandle;
        };

        template <>
        struct ResourceTraits<MaterialInstance> {
            using HandleType = MaterialInstanceHandle;
            using ManagerType = MaterialInstanceManager;
        };

        struct RENDER_API MaterialLibraryHandle : public RenderResourceHandle<MaterialLibrary> {
            using RenderResourceHandle::RenderResourceHandle;
        };

        template <>
        struct ResourceTraits<MaterialLibrary> {
            using HandleType = MaterialLibraryHandle;
            using ManagerType = MaterialLibraryManager;
        };

        struct RENDER_API StaticMeshResourceHandle : public RenderResourceHandle<StaticMeshResource> {
            using RenderResourceHandle::RenderResourceHandle;
        };

        template <>
        struct ResourceTraits<StaticMeshResource> {
            using HandleType = StaticMeshResourceHandle;
            using ManagerType = StaticMeshResourceManager;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // ENGINE_RENDER_RESOURCE_RENDERRESOURCEHANDLE_INCLUDED
