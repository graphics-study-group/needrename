#include "MaterialLibrary.h"

#include "Render/Pipeline/PipelineRuntimeInfo.h"
#include "Render/Pipeline/PipelineUtils.hpp"
#include "Asset/Material/MaterialTemplateAsset.h"
#include "Render/DebugUtils.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"

#include <cassert>
#include <unordered_set>
#include <string_view>
#include <SDL3/SDL.h>

namespace Engine {
    struct MaterialLibrary::impl {
        static constexpr size_t MAX_MATERIAL_DESCRIPTORS_PER_POOL = 128;
        static constexpr std::array DEFAULT_MATERIAL_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 128},
            vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 128}
        };

        struct PipelineAssetItem {
            AssetRef material_template_asset {};
        };

        struct PipelineBundle {
            /// Shader modules for the pipeline.
            std::vector <vk::UniqueShaderModule> shader_modules{};
            /// Reflected pipeline info.
            ShdrRfl::SPLayout reflected{};

            /// Descriptor pool for material descriptors (could be null if no material descriptor is found).
            vk::UniqueDescriptorPool descriptor_pool{};
            /// Descriptor set layout for material descriptors (could be null if no material descriptor is found).
            vk::DescriptorSetLayout descriptor_set_layout{};
            /// Material pipeline layout.
            vk::PipelineLayout pipeline_layout{};

            std::unordered_map <
                PipelineRuntimeInfo,
                std::unique_ptr<MaterialTemplate>,
                PipelineUtils::pipeline_runtime_info_hasher
            > materials{};
        };

        std::unordered_map <std::string, PipelineBundle> pipeline_table {};
        std::unordered_map <std::string, PipelineAssetItem> pipeline_asset_table {};

        MaterialTemplate & GetPipelineOrCreate(
            RenderSystem & system,
            const std::string & tag,
            const PipelineRuntimeInfo & pri
        ) {
            auto itr = pipeline_table.find(tag);
            if (itr == pipeline_table.end() || !(itr->second.materials[pri])) {
                CreatePipeline(system, tag, pri);
            }
            return *pipeline_table[tag].materials[pri];
        }

        void CompileShaderModules (
            PipelineBundle & b,
            vk::Device d,
            const std::vector<AssetRef> & shader_refs
        ) {
            b.reflected.interfaces.clear();
            b.reflected.interface_name_mapping.clear();

            b.shader_modules.clear();
            b.shader_modules.resize(shader_refs.size());

            for (size_t i = 0; i < shader_refs.size(); i++) {
                assert(shader_refs[i].IsValid() && "Invalid shader asset.");

                auto shader_asset = shader_refs[i].cas<ShaderAsset>();
                auto code = shader_asset->binary;
                b.reflected.Merge(Engine::ShdrRfl::SPLayout::Reflect(code, true));
                vk::ShaderModuleCreateInfo ci{
                    {}, code.size() * sizeof(uint32_t), reinterpret_cast<const uint32_t *>(code.data())
                };

                b.shader_modules[i] = d.createShaderModuleUnique(ci);
                DEBUG_SET_NAME_TEMPLATE(
                    d, b.shader_modules[i].get(), std::format("Shader Module - {}", shader_asset->m_name)
                );
            }
        }

        /**
         * @brief Create general material pipeline layout,
         * which contains three descriptor sets, the last of which being reflected from shader.
         */
        void GenerateDescriptorSetAndPipelineLayout(
            PipelineBundle & b,
            RenderSystemState::ImmutableResourceCache & irc,
            vk::Device d,
            vk::DescriptorSetLayout scene_descriptors,
            vk::DescriptorSetLayout camera_descriptors,
            const std::string & name
        ) {
            auto desc_bindings = b.reflected.GenerateLayoutBindings(2, true, false);
            if (!desc_bindings.empty()) {
                unsigned ubo_count{0};
                for (auto & db : desc_bindings) {
                    if (
                        db.descriptorType == vk::DescriptorType::eUniformBuffer ||
                        db.descriptorType == vk::DescriptorType::eUniformBufferDynamic
                    ) {
                        ubo_count ++;
                    }
                }
                if (ubo_count != 1) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Found %u uniform buffer binding points instead of one.",
                        ubo_count
                    );
                }

                vk::DescriptorSetLayoutCreateInfo dslci{{}, desc_bindings};
                b.descriptor_set_layout = irc.GetDescriptorSetLayout(dslci);

                std::array<vk::PushConstantRange, 1> push_constants{
                    RenderSystemState::RendererManager::GetPushConstantRange()
                };
                std::array<vk::DescriptorSetLayout, 3> set_layouts{
                    scene_descriptors,
                    camera_descriptors,
                    b.descriptor_set_layout
                };
                vk::PipelineLayoutCreateInfo plci{{}, set_layouts, push_constants};
                b.pipeline_layout = irc.GetPipelineLayout(plci);

                // Also create a descriptor pool
                auto dpci = vk::DescriptorPoolCreateInfo{
                    vk::DescriptorPoolCreateFlags{},
                    MAX_MATERIAL_DESCRIPTORS_PER_POOL,
                    DEFAULT_MATERIAL_DESCRIPTOR_POOL_SIZE
                };
                b.descriptor_pool = d.createDescriptorPoolUnique(dpci);
                DEBUG_SET_NAME_TEMPLATE(
                    d, b.descriptor_pool.get(), std::format("Descriptor Pool - Material {}", name)
                );
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Material %s pipeline has no material descriptors.",
                    name.c_str()
                );

                std::array<vk::PushConstantRange, 1> push_constants{
                    RenderSystemState::RendererManager::GetPushConstantRange()
                };
                std::array<vk::DescriptorSetLayout, 2> set_layouts{
                    scene_descriptors, camera_descriptors
                };
                vk::PipelineLayoutCreateInfo plci{{}, set_layouts, push_constants};
                b.pipeline_layout = irc.GetPipelineLayout(plci);
            }
        }

        /**
         * @brief Create a pipeline, assuming that the asset corresponding to
         * both the tag and the runtime information.
         */
        void CreatePipeline(
            RenderSystem & system,
            const std::string & tag,
            const PipelineRuntimeInfo & pri
        ) {
            auto itr = pipeline_asset_table.find(tag);
            assert(itr != pipeline_asset_table.end() && "Pipeline tag not found.");
            assert(itr->second.material_template_asset.IsValid() && "Invalid material template asset.");
            
            const auto & asset = itr->second.material_template_asset.cas<const MaterialTemplateAsset>();

            if (pipeline_table[tag].shader_modules.empty()) {
                CompileShaderModules(
                    pipeline_table[tag],
                    system.GetDevice(),
                    asset->properties.shaders.shaders
                );
                GenerateDescriptorSetAndPipelineLayout(
                    pipeline_table[tag],
                    system.GetIRCache(),
                    system.GetDevice(),
                    system.GetSceneDataManager().GetLightDescriptorSetLayout(),
                    system.GetCameraManager().GetDescriptorSetLayout(),
                    asset->name
                );
            }
            
            const auto & b = pipeline_table[tag];
            std::vector <vk::ShaderModule> shader_modules;
            std::transform(
                b.shader_modules.begin(),
                b.shader_modules.end(),
                std::back_inserter(shader_modules),
                [] (const vk::UniqueShaderModule & usm) {
                    return usm.get();
                }
            );
            if (b.descriptor_pool) {
                pipeline_table[tag].materials[pri] = std::make_unique<MaterialTemplate>(
                    system,
                    asset->properties,
                    shader_modules,
                    b.pipeline_layout,
                    b.descriptor_pool.get(),
                    b.reflected,
                    pri,
                    asset->name
                );
            } else {
                pipeline_table[tag].materials[pri] = std::make_unique<MaterialTemplate>(
                    system,
                    asset->properties,
                    shader_modules,
                    b.pipeline_layout,
                    nullptr,
                    b.reflected,
                    pri,
                    asset->name
                );
            }
            
            SDL_LogInfo(
                SDL_LOG_CATEGORY_RENDER,
                "Created material %p.",
                static_cast<void *>(pipeline_table[tag].materials[pri].get())
            );
        }
    };

    MaterialLibrary::MaterialLibrary(RenderSystem & s) : m_system(s), pimpl(std::make_unique<impl>()){
    }
    MaterialLibrary::~MaterialLibrary() {
    }
    const MaterialTemplate * MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, const PipelineRuntimeInfo & pri
    ) const noexcept {
        auto itr = pimpl->pipeline_table.find(tag);
        if (itr != pimpl->pipeline_table.end() && itr->second.materials[pri]) {
            return itr->second.materials[pri].get();
        }
        if (itr == pimpl->pipeline_table.end()) {
            auto inserted = pimpl->pipeline_table.insert({tag, impl::PipelineBundle{}});
            assert(inserted.second && "Failed insertion into pipeline unordered map.");
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

        return &pimpl->GetPipelineOrCreate(
            m_system,
            tag,
            pri
        );
    }

    MaterialTemplate *MaterialLibrary::FindMaterialTemplate(
        const std::string &tag, const PipelineRuntimeInfo & pri
    ) noexcept {
        return const_cast<MaterialTemplate *>(std::as_const(*this).FindMaterialTemplate(tag, pri));
    }

    void MaterialLibrary::Instantiate(const MaterialLibraryAsset & asset) {
        pimpl->pipeline_table.clear();
        for (auto & [tag, bundle] : asset.material_bundle) {
            pimpl->pipeline_asset_table[tag] = impl::PipelineAssetItem{
                .material_template_asset = bundle.material_template
            };
        }
    }
} // namespace Engine
