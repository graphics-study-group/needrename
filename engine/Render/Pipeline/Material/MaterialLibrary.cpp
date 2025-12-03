#include "MaterialLibrary.h"

#include "Asset/Material/MaterialTemplateAsset.h"
#include <cassert>
#include <SDL3/SDL.h>

namespace Engine {
    struct MaterialLibrary::impl {
        constexpr static uint32_t MAX_MESH_TYPE_COUNT = 5;
        using PipelineAssetBundle = std::array <std::shared_ptr<AssetRef>, MAX_MESH_TYPE_COUNT>;
        using PipelineBundle = std::array <std::shared_ptr<MaterialTemplate>, MAX_MESH_TYPE_COUNT>;

        std::unordered_map <std::string, PipelineBundle> pipeline_table {};
        std::unordered_map <std::string, PipelineAssetBundle> pipeline_asset_table {};

        std::shared_ptr<MaterialTemplate> GetPipelineOrCreate(
            RenderSystem & system,
            const std::string & tag,
            uint32_t asset_type
        ) {
            assert(asset_type < MAX_MESH_TYPE_COUNT);
            auto itr = pipeline_table.find(tag);
            if (itr == pipeline_table.end() || !(itr->second[asset_type])) {
                CreatePipeline(system, tag, asset_type, asset_type);
            }
            return pipeline_table[tag][asset_type];
        }

        /**
         * @brief Create a pipeline, assuming that the asset corresponding to
         * both the tag and the mesh type exists.
         */
        void CreatePipeline(RenderSystem & system, const std::string & tag, uint32_t asset_type, uint32_t actual_type) {
            assert(asset_type < MAX_MESH_TYPE_COUNT);
            auto itr = pipeline_asset_table.find(tag);
            assert(itr != pipeline_asset_table.end() && "Pipeline tag not found.");
            auto itr2 = itr->second[asset_type];
            assert(itr2 && "Mesh type not found.");
    
            const auto & asset = itr2->cas<const MaterialTemplateAsset>();
            if (asset_type == actual_type) {
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_RENDER,
                    std::format(
                        "Creating material (name: {}, type: {}) from asset {}.",
                        tag,
                        asset_type,
                        asset->name
                    ).c_str()
                );
            } else {
                SDL_LogInfo(
                    SDL_LOG_CATEGORY_RENDER,
                    std::format(
                        "Creating material (name: {}, type: {} -> {}) from asset {}.",
                        tag,
                        asset_type,
                        actual_type,
                        asset->name
                    ).c_str()
                );
            }
            
            pipeline_table[tag][actual_type] = std::make_unique<MaterialTemplate>(
                system,
                asset->properties,
                static_cast<MeshVertexType>(actual_type),
                asset->name
            );
            SDL_LogInfo(
                SDL_LOG_CATEGORY_RENDER,
                "Created material %p.",
                static_cast<void *>(pipeline_table[tag][actual_type].get())
            );
        }
    };

    MaterialLibrary::MaterialLibrary(RenderSystem & s) : m_system(s), pimpl(std::make_unique<impl>()){
    }
    MaterialLibrary::~MaterialLibrary() {
    }
    const MaterialTemplate * MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, MeshVertexType mesh_type
    ) const noexcept {
        auto idx = static_cast<std::underlying_type<MeshVertexType>::type>(mesh_type);
        assert(idx < impl::MAX_MESH_TYPE_COUNT);

        auto itr = pimpl->pipeline_table.find(tag);
        if (itr != pimpl->pipeline_table.end() && itr->second[idx]) {
            return itr->second[idx].get();
        }
        if (itr == pimpl->pipeline_table.end()) {
            auto inserted = pimpl->pipeline_table.insert({tag, {}});
            itr = inserted.first;
        }
        
        // The pipeline is not found -> try create from asset
        auto asset_itr = pimpl->pipeline_asset_table.find(tag);
        if (asset_itr == pimpl->pipeline_asset_table.end()) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "Cannot find pipeline tagged %s",
                tag.c_str()
            );
            return nullptr;
        }
        // Find lowest available asset
        auto asset_ptr = asset_itr->second[idx];
        uint32_t available_idx = std::numeric_limits<uint32_t>::max();
        for (int i = idx; i >= 0; i--) {
            if (asset_itr->second[i]) {
                available_idx = i;
                break;
            }
        }

        if (available_idx >= impl::MAX_MESH_TYPE_COUNT) {
            SDL_LogError(
                SDL_LOG_CATEGORY_RENDER,
                "Pipeline tagged %s found, but mesh type %u is not available.",
                tag.c_str(), idx
            );
            return nullptr;
        }
        // Construct material from compatible material.
        if (available_idx != idx) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Pipeline tagged %s found, but mesh type %u is not available, and is downgraded to %u.",
                tag.c_str(), idx, available_idx
            );

            pimpl->CreatePipeline(m_system, tag, available_idx, idx);
            return pimpl->pipeline_table[tag][idx].get();
        }
    
        return pimpl->GetPipelineOrCreate(m_system, tag, idx).get();
    }

    MaterialTemplate *MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, HomogeneousMesh::MeshVertexType mesh_type
    ) noexcept {
        return const_cast<MaterialTemplate *>(std::as_const(*this).FindMaterialTemplate(tag, mesh_type));
    }

    void MaterialLibrary::Instantiate(const MaterialLibraryAsset & asset) {
        pimpl->pipeline_table.clear();
        for (auto & [tag, bundle] : asset.material_bundle) {

            impl::PipelineAssetBundle p;
            for (auto & [type, tpl] : bundle) {
                assert(type < impl::MAX_MESH_TYPE_COUNT);
                p[type] = tpl;
                // const auto & asset = *tpl->cas<const MaterialTemplateAsset>();
                // p[type] = std::make_shared<MaterialTemplate>(m_system, asset.properties, static_cast<MeshVertexType>(type), asset.name);
            }
            pimpl->pipeline_asset_table[tag] = std::move(p);
        }
    }
} // namespace Engine
