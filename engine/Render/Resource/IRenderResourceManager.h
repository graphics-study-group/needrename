#ifndef RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
#define RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED

#include "Core/guid.h"
#include "RenderResourceHandle.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        template <typename ResourceType>
        class IRenderResourceManager {
        public:
            using HandleType = typename ResourceTraits<ResourceType>::HandleType;
            using ManagerType = typename ResourceTraits<ResourceType>::ManagerType;

            IRenderResourceManager(RenderSystem &system) : m_system(system) {
            }
            ~IRenderResourceManager() = default;

            HandleType Create(std::unique_ptr<ResourceType> resource, uint32_t deallocate_after_frames = 3u);

            HandleType CreateOrReuseFromAsset(GUID guid, uint32_t deallocate_after_frames = 3u);

            ResourceType *Resolve(HandleType handle);

            bool IsHandleValid(HandleType handle) const noexcept;

            void Acquire(HandleType handle);

            void AcquireAsync(HandleType handle);

            void Release(HandleType handle);

            bool IsReady(HandleType handle) const noexcept;

            void EnsureReady(HandleType handle);

            void OnDestroy(HandleType handle);

            void TickFrame();

        protected:
            RenderSystem &m_system;

            struct ResourceRecord {
                uint32_t generation{1};
                uint32_t refcount{0};
                int32_t pending_deallocation_countdown{-1};
                uint32_t deallocate_after_frames{3};

                std::unique_ptr<ResourceType> payload{};
            };

            std::vector<ResourceRecord> m_records{};
            std::vector<uint32_t> m_free_indices{};
            std::unordered_map<GUID, HandleType> m_guid_to_handle{};
        };
    } // namespace RenderSystemState
} // namespace Engine

#include "IRenderResourceManager.inl"

#endif // RENDER_RESOURCE_IRENDERRESOURCEPROVIDER_INCLUDED
