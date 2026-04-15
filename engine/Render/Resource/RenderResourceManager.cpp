#include "RenderResourceManager.h"

#include "IRenderResourceProvider.h"
#include "MaterialInstanceProvider.h"
#include "MaterialLibraryProvider.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "StaticMeshResourceProvider.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::RenderSystemState {
    struct RenderResourceManager::impl {
        struct ResourceRecord {
            std::type_index type_id{typeid(void)};
            uint32_t generation{1};
            uint32_t refcount{0};
            int32_t pending_deallocation_countdown{-1};

            GUID guid{};

            std::vector<RenderResourceHandle> dependencies{};
            std::shared_ptr<void> payload{};
        };

        std::vector<ResourceRecord> records{};
        std::vector<uint32_t> free_indices{};
        std::unordered_map<std::type_index, std::unique_ptr<IRenderResourceProvider>> providers{};

        RenderResourceHandle MakeHandle(uint32_t index, const ResourceRecord &record) const noexcept {
            return RenderResourceHandle{index, record.generation, record.type_id};
        }

        bool IsHandleValid(
            RenderResourceHandle handle, std::type_index expected_type_id = typeid(void)
        ) const noexcept {
            if (!handle.IsValid()) return false;
            if (handle.index >= records.size()) return false;

            const auto &record = records[handle.index];
            if (record.type_id != handle.type_id) return false;
            if (record.generation != handle.generation) return false;
            if (record.payload == nullptr) return false;
            if (expected_type_id != typeid(void) && record.type_id != expected_type_id) return false;
            return true;
        }
    };

    RenderResourceManager::RenderResourceManager(RenderSystem &system) :
        m_system(system), pimpl(std::make_unique<impl>()) {
        auto material_library_provider = std::make_unique<MaterialLibraryProvider>();
        pimpl->providers.emplace(material_library_provider->GetTypeID(), std::move(material_library_provider));

        auto material_instance_provider = std::make_unique<MaterialInstanceProvider>();
        pimpl->providers.emplace(material_instance_provider->GetTypeID(), std::move(material_instance_provider));

        auto static_mesh_provider = std::make_unique<StaticMeshResourceProvider>();
        pimpl->providers.emplace(static_mesh_provider->GetTypeID(), std::move(static_mesh_provider));
    }

    RenderResourceManager::~RenderResourceManager() = default;

    RenderResourceHandle RenderResourceManager::AcquireByType(std::type_index type_id, GUID guid) {
        auto provider_it = pimpl->providers.find(type_id);
        if (provider_it == pimpl->providers.end()) return {};
        return provider_it->second->Acquire(*this, m_system, guid);
    }

    RenderResourceHandle RenderResourceManager::AcquireAsyncByType(std::type_index type_id, GUID guid) {
        auto provider_it = pimpl->providers.find(type_id);
        if (provider_it == pimpl->providers.end()) return {};
        return provider_it->second->AcquireAsync(*this, m_system, guid);
    }

    void RenderResourceManager::Release(RenderResourceHandle handle) {
        if (!pimpl->IsHandleValid(handle)) {
            return;
        }

        auto &record = pimpl->records[handle.index];
        if (record.refcount == 0) {
            return;
        }

        record.refcount -= 1;
        if (record.refcount == 0) {
            record.pending_deallocation_countdown = FrameManager::FRAMES_IN_FLIGHT;
        }
    }

    void RenderResourceManager::TickFrame() {
        for (uint32_t i = 0; i < pimpl->records.size(); ++i) {
            auto &record = pimpl->records[i];
            if (record.payload == nullptr) continue;
            if (record.pending_deallocation_countdown < 0) continue;

            record.pending_deallocation_countdown -= 1;
            if (record.pending_deallocation_countdown > 0) continue;

            for (auto dep : record.dependencies) {
                Release(dep);
            }

            auto provider_it = pimpl->providers.find(record.type_id);
            if (provider_it != pimpl->providers.end()) {
                provider_it->second->OnRecordDestroy(record.guid);
            }

            record.payload.reset();
            record.dependencies.clear();
            record.guid = GUID{};
            record.refcount = 0;
            record.pending_deallocation_countdown = -1;
            record.type_id = typeid(void);
            record.generation += 1;

            pimpl->free_indices.push_back(i);
        }
    }

    void *RenderResourceManager::ResolveByType(RenderResourceHandle handle, std::type_index type_id) const noexcept {
        auto provider_it = pimpl->providers.find(type_id);
        if (provider_it == pimpl->providers.end()) return nullptr;
        return provider_it->second->Resolve(const_cast<RenderResourceManager &>(*this), handle);
    }

    bool RenderResourceManager::IsReadyByType(RenderResourceHandle handle, std::type_index type_id) const noexcept {
        auto provider_it = pimpl->providers.find(type_id);
        if (provider_it == pimpl->providers.end()) return false;
        return provider_it->second->IsReady(const_cast<RenderResourceManager &>(*this), m_system, handle);
    }

    void RenderResourceManager::EnsureReadyByType(RenderResourceHandle handle, std::type_index type_id) {
        auto provider_it = pimpl->providers.find(type_id);
        if (provider_it == pimpl->providers.end()) return;
        provider_it->second->EnsureReady(*this, m_system, handle);
    }

    RenderResourceHandle RenderResourceManager::TryReuseRecord(std::type_index type_id, uint32_t index) noexcept {
        if (index >= pimpl->records.size()) return {};

        auto &record = pimpl->records[index];
        if (!pimpl->IsHandleValid(pimpl->MakeHandle(index, record), type_id)) return {};

        record.refcount += 1;
        record.pending_deallocation_countdown = -1;
        return pimpl->MakeHandle(index, record);
    }

    RenderResourceHandle RenderResourceManager::CreateRecord(
        std::type_index type_id,
        GUID guid,
        std::shared_ptr<void> payload,
        std::vector<RenderResourceHandle> dependencies
    ) {
        uint32_t index = 0;
        if (!pimpl->free_indices.empty()) {
            index = pimpl->free_indices.back();
            pimpl->free_indices.pop_back();
        } else {
            index = static_cast<uint32_t>(pimpl->records.size());
            pimpl->records.emplace_back();
        }

        auto &record = pimpl->records[index];
        auto generation = record.generation == 0 ? 1u : record.generation;
        record = {};
        record.type_id = type_id;
        record.generation = generation;
        record.guid = guid;
        record.refcount = 1;
        record.pending_deallocation_countdown = -1;
        record.dependencies = std::move(dependencies);
        record.payload = std::move(payload);
        return pimpl->MakeHandle(index, record);
    }

    void *RenderResourceManager::ResolvePayload(
        RenderResourceHandle handle, std::type_index expected_type_id
    ) const noexcept {
        if (!pimpl->IsHandleValid(handle, expected_type_id)) return nullptr;
        return pimpl->records[handle.index].payload.get();
    }
} // namespace Engine::RenderSystemState
