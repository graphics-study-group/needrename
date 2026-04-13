#include "Render/Resource/RenderResourceHub.h"

#include "Render/RenderSystem.h"

#include <cassert>
#include <vector>

namespace Engine::RenderSystemState {
    RenderResourceHub::RenderResourceHub(RenderSystem &system) : m_system(system), m_slots{}, m_owner_index{} {
    }

    void RenderResourceHub::RegisterProvider(const std::type_info &resource_type, IResourceProvider *provider) {
        assert(provider && "Provider cannot be null.");

        std::type_index type_key(resource_type);
        auto [it, inserted] = m_providers.emplace(type_key, provider);
        assert(inserted && "Duplicate provider registration.");
        (void)it;
    }

    bool RenderResourceHub::HasProvider(const std::type_info &resource_type) const noexcept {
        return m_providers.find(std::type_index(resource_type)) != m_providers.end();
    }

    std::shared_ptr<void> RenderResourceHub::Acquire(
        const std::type_info &resource_type, const GUID &guid, OwnerHandle owner
    ) {
        return AcquireInternal(std::type_index(resource_type), guid, owner);
    }

    std::shared_ptr<void> RenderResourceHub::AcquireInternal(
        std::type_index type_key, const GUID &guid, OwnerHandle owner
    ) {
        auto provider_it = m_providers.find(type_key);
        assert(provider_it != m_providers.end() && "No provider registered for requested resource type.");
        auto *provider = provider_it->second;

        ResourceKey key{type_key, guid};
        auto &owner_set = m_owner_index[owner];
        auto [owner_it, inserted] = owner_set.insert(key);
        if (!inserted) {
            (void)owner_it;
            auto slot_it = m_slots.find(key);
            assert(slot_it != m_slots.end() && "Owner index and slot index are inconsistent.");
            return slot_it->second.resource;
        }

        auto &slot = m_slots[key];
        if (!slot.resource) {
            if (slot.dependency_owner == 0) {
                slot.dependency_owner = m_next_dependency_owner++;
            }
            slot.resource = provider->Create(m_system, *this, slot.dependency_owner, guid);
            assert(slot.resource && "Provider returned null resource.");
        }

        slot.ref_count += 1;
        slot.pending_release_countdown = -1;
        return slot.resource;
    }

    void RenderResourceHub::Release(const std::type_info &resource_type, const GUID &guid, OwnerHandle owner) {
        ReleaseInternal(std::type_index(resource_type), guid, owner);
    }

    void RenderResourceHub::ReleaseInternal(std::type_index type_key, const GUID &guid, OwnerHandle owner) {
        ResourceKey key{type_key, guid};

        auto owner_it = m_owner_index.find(owner);
        if (owner_it == m_owner_index.end()) return;

        auto key_it = owner_it->second.find(key);
        if (key_it == owner_it->second.end()) return;

        owner_it->second.erase(key_it);
        if (owner_it->second.empty()) {
            m_owner_index.erase(owner_it);
        }

        auto slot_it = m_slots.find(key);
        assert(slot_it != m_slots.end() && "Owner index and slot index are inconsistent.");
        assert(slot_it->second.ref_count > 0 && "Resource ref count underflow.");

        slot_it->second.ref_count -= 1;
        if (slot_it->second.ref_count == 0) {
            slot_it->second.pending_release_countdown = static_cast<int32_t>(m_deferred_release_frames);
        }
    }

    void RenderResourceHub::ReleaseOwner(OwnerHandle owner) {
        auto owner_it = m_owner_index.find(owner);
        if (owner_it == m_owner_index.end()) return;

        std::vector<ResourceKey> to_release;
        to_release.reserve(owner_it->second.size());
        for (const auto &key : owner_it->second) {
            to_release.push_back(key);
        }

        for (const auto &key : to_release) {
            ReleaseInternal(key.type, key.guid, owner);
        }
    }

    void RenderResourceHub::SetDeferredReleaseFrames(uint32_t frame_count) noexcept {
        m_deferred_release_frames = frame_count;
    }

    void RenderResourceHub::CollectGarbage() {
        for (auto it = m_slots.begin(); it != m_slots.end();) {
            auto &slot = it->second;
            if (slot.ref_count > 0) {
                ++it;
                continue;
            }

            if (slot.pending_release_countdown > 0) {
                slot.pending_release_countdown -= 1;
                ++it;
                continue;
            }

            if (slot.resource) {
                auto provider_it = m_providers.find(it->first.type);
                assert(provider_it != m_providers.end() && "No provider registered for requested resource type.");
                provider_it->second->Destroy(m_system, *this, slot.dependency_owner, it->first.guid, slot.resource);
                slot.resource.reset();
            }
            it = m_slots.erase(it);
        }
    }

    size_t RenderResourceHub::GetLiveSlotCount() const noexcept {
        return m_slots.size();
    }
} // namespace Engine::RenderSystemState
