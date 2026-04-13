#include "RenderResourceManager.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Render/Pipeline/Material/MaterialInstance.h"
#include "Render/Pipeline/Material/MaterialLibrary.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/AllocatorState.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/Renderer/StaticHomogeneousMesh.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::RenderSystemState {
    namespace {
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

        struct StaticMeshRendererResource {
            AssetRef mesh_asset_ref{};
            std::shared_ptr<StaticHomogeneousMesh::StaticHMeshSharedDataBlock> shared_data{};
            std::shared_ptr<StaticHomogeneousMesh> renderer{};

            StaticMeshRendererResource() = default;
        };
    } // namespace

    struct RenderResourceManager::impl {
        struct ResourceRecord {
            RenderResourceKind kind{RenderResourceKind::Invalid};
            uint32_t generation{1};
            uint32_t refcount{0};
            int32_t pending_deallocation_countdown{-1};

            GUID guid{};
            uint32_t submesh_index{0};

            std::vector<RenderResourceHandle> dependencies;
            std::shared_ptr<void> payload;
        };

        std::vector<ResourceRecord> records{};
        std::vector<uint32_t> free_indices{};

        std::unordered_map<GUID, uint32_t> material_libraries{};
        std::unordered_map<GUID, uint32_t> material_instances{};
        std::unordered_map<MeshSubmeshKey, uint32_t, MeshSubmeshKeyHash> static_mesh_renderers{};

        std::unordered_map<GUID, std::weak_ptr<StaticHomogeneousMesh::StaticHMeshSharedDataBlock>>
            static_mesh_shared_blocks{};

        static RenderResourceHandle MakeHandle(uint32_t index, const ResourceRecord &record) {
            return RenderResourceHandle{index, record.generation, record.kind};
        }

        bool IsHandleValid(RenderResourceHandle handle) const noexcept {
            if (!handle.IsValid()) return false;
            if (handle.index >= records.size()) return false;
            const auto &record = records[handle.index];
            return record.kind == handle.kind && record.generation == handle.generation && record.payload != nullptr;
        }
    };

    RenderResourceManager::RenderResourceManager(RenderSystem &system) :
        m_system(system), pimpl(std::make_unique<impl>()) {
    }

    RenderResourceManager::~RenderResourceManager() = default;

    RenderResourceHandle RenderResourceManager::AcquireMaterialLibrary(GUID library_guid) {
        auto it = pimpl->material_libraries.find(library_guid);
        if (it != pimpl->material_libraries.end()) {
            auto &record = pimpl->records[it->second];
            record.refcount += 1;
            record.pending_deallocation_countdown = -1;
            return impl::MakeHandle(it->second, record);
        }

        AssetRef ref(library_guid);
        auto *asset = ref.as<MaterialLibraryAsset>();
        assert(asset);

        auto lib = std::make_shared<MaterialLibrary>(m_system);
        lib->Instantiate(*asset);

        uint32_t index = 0;
        if (!pimpl->free_indices.empty()) {
            index = pimpl->free_indices.back();
            pimpl->free_indices.pop_back();
            pimpl->records[index] = {};
        } else {
            index = static_cast<uint32_t>(pimpl->records.size());
            pimpl->records.push_back({});
        }

        auto &record = pimpl->records[index];
        record.kind = RenderResourceKind::MaterialLibrary;
        record.guid = library_guid;
        record.refcount = 1;
        record.pending_deallocation_countdown = -1;
        record.payload = lib;

        pimpl->material_libraries[library_guid] = index;
        return impl::MakeHandle(index, record);
    }

    RenderResourceHandle RenderResourceManager::AcquireMaterialInstance(GUID material_guid) {
        auto it = pimpl->material_instances.find(material_guid);
        if (it != pimpl->material_instances.end()) {
            auto &record = pimpl->records[it->second];
            record.refcount += 1;
            record.pending_deallocation_countdown = -1;
            return impl::MakeHandle(it->second, record);
        }

        AssetRef mat_ref(material_guid);
        auto *mat_asset = mat_ref.as<MaterialAsset>();
        assert(mat_asset);

        auto library_handle = AcquireMaterialLibrary(mat_asset->m_library.GetGUID());
        auto *library = ResolveMaterialLibrary(library_handle);
        assert(library);

        auto inst = std::make_shared<MaterialInstance>(m_system, *library);
        inst->Instantiate(*mat_asset);

        uint32_t index = 0;
        if (!pimpl->free_indices.empty()) {
            index = pimpl->free_indices.back();
            pimpl->free_indices.pop_back();
            pimpl->records[index] = {};
        } else {
            index = static_cast<uint32_t>(pimpl->records.size());
            pimpl->records.push_back({});
        }

        auto &record = pimpl->records[index];
        record.kind = RenderResourceKind::MaterialInstance;
        record.guid = material_guid;
        record.refcount = 1;
        record.pending_deallocation_countdown = -1;
        record.payload = inst;
        record.dependencies.push_back(library_handle);

        pimpl->material_instances[material_guid] = index;
        return impl::MakeHandle(index, record);
    }

    RenderResourceHandle RenderResourceManager::AcquireStaticMeshRenderer(
        GUID mesh_guid, uint32_t submesh_index, bool eagerly_loaded
    ) {
        MeshSubmeshKey key{mesh_guid, submesh_index};
        auto it = pimpl->static_mesh_renderers.find(key);
        if (it != pimpl->static_mesh_renderers.end()) {
            auto &record = pimpl->records[it->second];
            record.refcount += 1;
            record.pending_deallocation_countdown = -1;
            if (eagerly_loaded) {
                EnsureRendererReady(impl::MakeHandle(it->second, record));
            }
            return impl::MakeHandle(it->second, record);
        }

        auto resource = std::make_shared<StaticMeshRendererResource>();
        resource->mesh_asset_ref = AssetRef(mesh_guid);
        auto *mesh_asset = resource->mesh_asset_ref.as<MeshAsset>();
        assert(mesh_asset);

        auto weak_it = pimpl->static_mesh_shared_blocks.find(mesh_guid);
        if (weak_it != pimpl->static_mesh_shared_blocks.end()) {
            resource->shared_data = weak_it->second.lock();
        }

        if (!resource->shared_data) {
            resource->shared_data = std::make_shared<StaticHomogeneousMesh::StaticHMeshSharedDataBlock>();
            resource->shared_data->submeshes.resize(mesh_asset->GetSubmeshCount());
            pimpl->static_mesh_shared_blocks[mesh_guid] = resource->shared_data;
        }

        resource->renderer =
            std::make_shared<StaticHomogeneousMesh>(submesh_index, *mesh_asset, *resource->shared_data);

        uint32_t index = 0;
        if (!pimpl->free_indices.empty()) {
            index = pimpl->free_indices.back();
            pimpl->free_indices.pop_back();
            pimpl->records[index] = {};
        } else {
            index = static_cast<uint32_t>(pimpl->records.size());
            pimpl->records.push_back({});
        }

        auto &record = pimpl->records[index];
        record.kind = RenderResourceKind::StaticMeshRenderer;
        record.guid = mesh_guid;
        record.submesh_index = submesh_index;
        record.refcount = 1;
        record.pending_deallocation_countdown = -1;
        record.payload = resource;

        pimpl->static_mesh_renderers[key] = index;
        auto handle = impl::MakeHandle(index, record);
        if (eagerly_loaded) {
            EnsureRendererReady(handle);
        }
        return handle;
    }

    void RenderResourceManager::Release(RenderResourceHandle handle) {
        if (!pimpl->IsHandleValid(handle)) return;

        auto &record = pimpl->records[handle.index];
        if (record.refcount == 0) return;

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

            if (record.kind == RenderResourceKind::MaterialLibrary) {
                pimpl->material_libraries.erase(record.guid);
            } else if (record.kind == RenderResourceKind::MaterialInstance) {
                pimpl->material_instances.erase(record.guid);
            } else if (record.kind == RenderResourceKind::StaticMeshRenderer) {
                pimpl->static_mesh_renderers.erase(MeshSubmeshKey{record.guid, record.submesh_index});
            }

            record.payload.reset();
            record.dependencies.clear();
            record.guid = GUID{};
            record.refcount = 0;
            record.pending_deallocation_countdown = -1;
            record.kind = RenderResourceKind::Invalid;
            record.submesh_index = 0;
            record.generation += 1;

            pimpl->free_indices.push_back(i);
        }

        for (auto it = pimpl->static_mesh_shared_blocks.begin(); it != pimpl->static_mesh_shared_blocks.end();) {
            if (it->second.expired()) {
                it = pimpl->static_mesh_shared_blocks.erase(it);
            } else {
                ++it;
            }
        }
    }

    MaterialLibrary *RenderResourceManager::ResolveMaterialLibrary(RenderResourceHandle handle) const noexcept {
        if (!pimpl->IsHandleValid(handle)) return nullptr;
        if (handle.kind != RenderResourceKind::MaterialLibrary) return nullptr;
        return static_cast<MaterialLibrary *>(pimpl->records[handle.index].payload.get());
    }

    MaterialInstance *RenderResourceManager::ResolveMaterialInstance(RenderResourceHandle handle) const noexcept {
        if (!pimpl->IsHandleValid(handle)) return nullptr;
        if (handle.kind != RenderResourceKind::MaterialInstance) return nullptr;
        return static_cast<MaterialInstance *>(pimpl->records[handle.index].payload.get());
    }

    IVertexBasedRenderer *RenderResourceManager::ResolveRenderer(RenderResourceHandle handle) const noexcept {
        if (!pimpl->IsHandleValid(handle)) return nullptr;
        if (handle.kind != RenderResourceKind::StaticMeshRenderer) return nullptr;

        auto *resource = static_cast<StaticMeshRendererResource *>(pimpl->records[handle.index].payload.get());
        return resource ? resource->renderer.get() : nullptr;
    }

    bool RenderResourceManager::EnsureRendererReady(RenderResourceHandle handle) {
        if (!pimpl->IsHandleValid(handle)) return false;
        if (handle.kind != RenderResourceKind::StaticMeshRenderer) return false;

        auto *resource = static_cast<StaticMeshRendererResource *>(pimpl->records[handle.index].payload.get());
        if (!resource || !resource->renderer) return false;

        if (!resource->renderer->IsReady()) {
            resource->renderer->Submit(m_system.GetAllocatorState(), m_system.GetFrameManager().GetSubmissionHelper());
        }
        return resource->renderer->IsReady();
    }
} // namespace Engine::RenderSystemState
