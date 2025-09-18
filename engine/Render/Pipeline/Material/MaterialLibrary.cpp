#include "MaterialLibrary.h"
#include <cassert>

namespace Engine {
    struct MaterialLibrary::impl {
        constexpr static uint32_t MAX_MESH_TYPE_COUNT = 5;
        using PipelineBundles = std::array <std::shared_ptr<MaterialTemplate>, MAX_MESH_TYPE_COUNT>;
        std::unordered_map <std::string, PipelineBundles> pipeline_table {};
    };

    MaterialLibrary::MaterialLibrary() : pimpl(std::make_unique<impl>()){
    }
    MaterialLibrary::~MaterialLibrary() {
    }
    const MaterialTemplate * MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, HomogeneousMesh::MeshVertexType mesh_type
    ) const noexcept {
        auto idx = static_cast<std::underlying_type<HomogeneousMesh::MeshVertexType>::type>(mesh_type);
        assert(idx < impl::MAX_MESH_TYPE_COUNT);

        auto itr = pimpl->pipeline_table.find(tag);
        if (itr == pimpl->pipeline_table.end()) return nullptr;

        for (int i = idx; i >= 0; i--) {
            if (itr->second[i]) {
                return itr->second[i].get();
            }
        }
        return nullptr;
    }
    MaterialTemplate *MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, HomogeneousMesh::MeshVertexType mesh_type
    ) noexcept {
        auto idx = static_cast<std::underlying_type<HomogeneousMesh::MeshVertexType>::type>(mesh_type);
        assert(idx < impl::MAX_MESH_TYPE_COUNT);

        auto itr = pimpl->pipeline_table.find(tag);
        if (itr == pimpl->pipeline_table.end()) return nullptr;

        for (int i = idx; i >= 0; i--) {
            if (itr->second[i]) {
                return itr->second[i].get();
            }
        }
        return nullptr;
    }
    void MaterialLibrary::Instantiate(const MaterialLibraryAsset &) {
    }
} // namespace Engine
