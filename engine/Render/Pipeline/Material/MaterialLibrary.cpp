#include "MaterialLibrary.h"

#include "Asset/Material/MaterialTemplateAsset.h"
#include <cassert>
#include <SDL3/SDL.h>

namespace Engine {
    struct MaterialLibrary::impl {
        constexpr static uint32_t MAX_MESH_TYPE_COUNT = 5;
        using PipelineBundle = std::array <std::shared_ptr<MaterialTemplate>, MAX_MESH_TYPE_COUNT>;
        std::unordered_map <std::string, PipelineBundle> pipeline_table {};
    };

    MaterialLibrary::MaterialLibrary(RenderSystem & s) : m_system(s), pimpl(std::make_unique<impl>()){
    }
    MaterialLibrary::~MaterialLibrary() {
    }
    const MaterialTemplate * MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, HomogeneousMesh::MeshVertexType mesh_type
    ) const noexcept {
        auto idx = static_cast<std::underlying_type<HomogeneousMesh::MeshVertexType>::type>(mesh_type);
        assert(idx < impl::MAX_MESH_TYPE_COUNT);

        auto itr = pimpl->pipeline_table.find(tag);
        if (itr == pimpl->pipeline_table.end()) {
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_RENDER,
                "Cannot find pipeline tagged %s.",
                tag.c_str()
            );
            return nullptr;
        }

        for (int i = idx; i >= 0; i--) {
            if (itr->second[i]) {
                if (i != idx) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Pipeline tagged %s does not support mesh type %u. Downgraded to %d.",
                        tag.c_str(),
                        (unsigned int)mesh_type,
                        i
                    );
                }
                return itr->second[i].get();
            }
        }

        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "Pipeline tagged %s exists, but none is usable for mesh type %u.",
            tag.c_str(),
            (unsigned int)mesh_type
        );
        return nullptr;
    }
    MaterialTemplate *MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, HomogeneousMesh::MeshVertexType mesh_type
    ) noexcept {
        return const_cast<MaterialTemplate *>(std::as_const(*this).FindMaterialTemplate(tag, mesh_type));
    }
    void MaterialLibrary::Instantiate(const MaterialLibraryAsset & asset) {
        pimpl->pipeline_table.clear();
        for (auto & [tag, bundle] : asset.material_bundle) {

            impl::PipelineBundle p;
            for (auto & [type, tpl] : bundle) {
                assert(type < impl::MAX_MESH_TYPE_COUNT);
                const auto & asset = *tpl->cas<const MaterialTemplateAsset>();
                p[type] = std::make_shared<MaterialTemplate>(m_system, asset.properties, static_cast<MeshVertexType>(type), asset.name);
            }
            pimpl->pipeline_table[tag] = std::move(p);
        }
    }
} // namespace Engine
