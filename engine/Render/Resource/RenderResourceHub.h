#ifndef RENDER_RESOURCE_RENDERRESOURCEHUB_INCLUDED
#define RENDER_RESOURCE_RENDERRESOURCEHUB_INCLUDED

#include "Core/guid.h"

#include <cstdint>
#include <memory>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        struct ResourceToken {
            std::type_index type{typeid(void)};
            GUID guid{};
            uint32_t generation{0};
        };

        class IResourceProvider {
        public:
            virtual ~IResourceProvider() = default;

            virtual const std::type_info &GetResourceType() const noexcept = 0;
            virtual std::shared_ptr<void> Create(RenderSystem &system, const GUID &guid) = 0;
            virtual void Destroy(RenderSystem &, const GUID &, std::shared_ptr<void> &) {
            }
            virtual const char *GetDebugName() const noexcept = 0;
        };

        class RenderResourceHub final {
        public:
            using OwnerHandle = uint32_t;

            explicit RenderResourceHub(RenderSystem &system);

            void RegisterProvider(const std::type_info &resource_type, IResourceProvider *provider);

            void RegisterProvider(IResourceProvider *provider) {
                RegisterProvider(provider->GetResourceType(), provider);
            }

            template <typename ResourceT, typename ProviderT>
            void RegisterProvider(ProviderT *provider) {
                static_assert(std::is_base_of_v<IResourceProvider, ProviderT>);
                RegisterProvider(typeid(ResourceT), provider);
            }

            bool HasProvider(const std::type_info &resource_type) const noexcept;

            template <typename ResourceT>
            bool HasProvider() const noexcept {
                return HasProvider(typeid(ResourceT));
            }

            std::shared_ptr<void> Acquire(const std::type_info &resource_type, const GUID &guid, OwnerHandle owner);

            template <typename ResourceT>
            std::shared_ptr<ResourceT> Acquire(const GUID &guid, OwnerHandle owner) {
                return std::static_pointer_cast<ResourceT>(Acquire(typeid(ResourceT), guid, owner));
            }

            void Release(const std::type_info &resource_type, const GUID &guid, OwnerHandle owner);

            template <typename ResourceT>
            void Release(const GUID &guid, OwnerHandle owner) {
                Release(typeid(ResourceT), guid, owner);
            }

            void ReleaseOwner(OwnerHandle owner);

            void SetDeferredReleaseFrames(uint32_t frame_count) noexcept;
            void CollectGarbage();

            size_t GetLiveSlotCount() const noexcept;

        private:
            std::shared_ptr<void> AcquireInternal(std::type_index type_key, const GUID &guid, OwnerHandle owner);
            void ReleaseInternal(std::type_index type_key, const GUID &guid, OwnerHandle owner);

            struct ResourceKey {
                std::type_index type{typeid(void)};
                GUID guid{};

                bool operator==(const ResourceKey &rhs) const noexcept {
                    return type == rhs.type && guid == rhs.guid;
                }
            };

            struct ResourceKeyHash {
                size_t operator()(const ResourceKey &key) const noexcept {
                    size_t h1 = std::hash<GUID>{}(key.guid);
                    size_t h2 = std::hash<std::type_index>{}(key.type);
                    return (h1 << 1) ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
                }
            };

            struct ResourceSlot {
                std::shared_ptr<void> resource{};
                uint32_t ref_count{0};
                int32_t pending_release_countdown{-1};
                uint32_t generation{1};
            };

            RenderSystem &m_system;
            uint32_t m_deferred_release_frames{0};

            std::unordered_map<std::type_index, IResourceProvider *> m_providers;
            std::unordered_map<ResourceKey, ResourceSlot, ResourceKeyHash> m_slots;
            std::unordered_map<OwnerHandle, std::unordered_set<ResourceKey, ResourceKeyHash>> m_owner_index;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RESOURCE_RENDERRESOURCEHUB_INCLUDED
