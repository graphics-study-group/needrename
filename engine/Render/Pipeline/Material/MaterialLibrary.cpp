#include "MaterialLibrary.h"

#include "Render/Renderer/VertexAttribute.h"
#include "Asset/Material/MaterialTemplateAsset.h"
#include <cassert>
#include <SDL3/SDL.h>

namespace Engine {
    struct MaterialLibrary::impl {
        constexpr static uint32_t MAX_MESH_TYPE_COUNT = 5;

        struct PipelineAssetItem {
            MeshVertexType expected_mesh_type;
            std::shared_ptr<AssetRef> material_template_asset;
        };
        using PipelineBundle = std::unordered_map <uint64_t, std::shared_ptr<MaterialTemplate>>;

        std::unordered_map <std::string, PipelineBundle> pipeline_table {};
        std::unordered_map <std::string, PipelineAssetItem> pipeline_asset_table {};

        std::shared_ptr<MaterialTemplate> GetPipelineOrCreate(
            RenderSystem & system,
            const std::string & tag,
            uint64_t mesh_type
        ) {
            assert(mesh_type < MAX_MESH_TYPE_COUNT);
            auto itr = pipeline_table.find(tag);
            if (itr == pipeline_table.end() || !(itr->second[mesh_type])) {
                CreatePipeline(system, tag, mesh_type);
            }
            return pipeline_table[tag][mesh_type];
        }

        /**
         * @brief Create a pipeline, assuming that the asset corresponding to
         * both the tag and the mesh type exists.
         */
        void CreatePipeline(RenderSystem & system, const std::string & tag, uint64_t actual_type) {
            assert(actual_type < MAX_MESH_TYPE_COUNT);
            auto itr = pipeline_asset_table.find(tag);
            assert(itr != pipeline_asset_table.end() && "Pipeline tag not found.");
            assert(itr->second.material_template_asset && "Invalid material template asset.");
            
            auto expected_type = static_cast<uint64_t>(itr->second.expected_mesh_type);
            const auto & asset = itr->second.material_template_asset->cas<const MaterialTemplateAsset>();

            SDL_LogInfo(
                SDL_LOG_CATEGORY_RENDER,
                std::format(
                    "Creating material (name: {}, type: {} -> {}) from asset {}.",
                    tag,
                    expected_type,
                    actual_type,
                    asset->name
                ).c_str()
            );
            
            pipeline_table[tag][actual_type] = std::make_unique<MaterialTemplate>(
                system,
                asset->properties,
                VertexAttribute{.packed = actual_type},
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
        const std::string &tag, VertexAttribute mesh_type
    ) const noexcept {
        auto idx = mesh_type.packed;
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
        auto asset_ptr = asset_itr->second.material_template_asset;
        uint64_t available_idx = static_cast<uint64_t>(asset_itr->second.expected_mesh_type);
        // Construct material from compatible material.
        if (available_idx != idx) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Pipeline tagged %s found, but mesh type %llu is not available, and is downgraded to %llu.",
                tag.c_str(), idx, available_idx
            );
        }
        return pimpl->GetPipelineOrCreate(m_system, tag, idx).get();
    }

    MaterialTemplate *MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, VertexAttribute mesh_type
    ) noexcept {
        return const_cast<MaterialTemplate *>(std::as_const(*this).FindMaterialTemplate(tag, mesh_type));
    }

    void MaterialLibrary::Instantiate(const MaterialLibraryAsset & asset) {
        pimpl->pipeline_table.clear();
        for (auto & [tag, bundle] : asset.material_bundle) {
            pimpl->pipeline_asset_table[tag] = impl::PipelineAssetItem{
                .expected_mesh_type = static_cast<MeshVertexType>(bundle.expected_mesh_type),
                .material_template_asset = bundle.material_template
            };
        }
    }
} // namespace Engine
