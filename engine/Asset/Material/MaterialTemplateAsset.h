#ifndef ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <meta_engine/reflection.hpp>

#include <Asset/Material/ShaderAsset.h>

namespace Engine
{
    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateSinglePassProperties
    {
        REFL_SER_BODY(MaterialTemplateSinglePassProperties)

        /// @brief C.f. `vkPipelineRasterizationStateCreateInfo`
        struct RasterizerProperties {
            enum class FillingMode {
                Fill,
                Line,
                Point
            };
            enum class CullingMode {
                None,
                Front,
                Back,
                All
            };
            enum class FrontFace {
                Counterclockwise,
                Clockwise
            };

            FillingMode filling{FillingMode::Fill};
            float line_width{1.0f};
            CullingMode culling{CullingMode::None};
            FrontFace front{FrontFace::Counterclockwise};
        } rasterizer;

        /// @brief C.f. `vkPipelineDepthStencilStateCreateInfo`
        struct DSProperties {
            bool ds_write_enabled = true;
            bool ds_test_enabled = true;
        } depth_stencil;
        
        /// @brief C.f. `vkPipelineShaderStageCreateInfo`
        struct Shaders {
            /// @brief A vector of all shader programs used in the pipeline
            std::vector <AssetRef> shaders;
            // TODO: Support shader specialization
            // std::vector <...> specialization;

            /// @brief stores information regarding layout info, aka descriptors and push constants.
            std::vector <ShaderVariableProperty> uniforms;
        } shaders;

        // XXX: We had better support pipeline caches to speed up loading...
        // C.f. `vkGetPipelineCacheData`
        // void * pipeline_cache.
    };

    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateProperties
    {
        REFL_SER_BODY(MaterialTemplateProperties)

        std::unordered_map <uint32_t, MaterialTemplateSinglePassProperties> properties;
    };
}

#endif // ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
