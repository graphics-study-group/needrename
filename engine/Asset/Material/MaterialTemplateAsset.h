#ifndef ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <meta_engine/reflection.hpp>

#include <Render/ImageUtils.h>
#include <Render/AttachmentUtils.h>
#include <Asset/Material/ShaderAsset.h>

namespace Engine
{
    /// @brief Properties for a single pass, contains enough information to create a complete pipeline.
    /// @details In Vulkan, to create a pipeline, the following information must be supplied: \n 
    /// 1. Programmable stage configuration for shaders -- `VkPipelineShaderStageCreateInfo` \n 
    /// 2. How are vertex buffers intepreted -- `VkPipelineVertexInputStateCreateInfo` \n 
    /// 3. What shape should the vertices draw -- `VkPipelineInputAssemblyStateCreateInfo` \n 
    /// 4. How to tessellate the primitives -- `VkPipelineTessellationStateCreateInfo` \n 
    /// 5. What is the view port to be drawn -- `VkPipelineViewportStateCreateInfo` \n 
    /// 6. How is the rasterizer configured -- `VkPipelineRasterizationStateCreateInfo` \n 
    /// 7. Whether it needs multisampling -- `VkPipelineMultisampleStateCreateInfo` \n 
    /// 8. How is depth-stencil test carried out -- `VkPipelineDepthStencilStateCreateInfo` \n 
    /// 9. How to blend the colors -- `VkPipelineColorBlendStateCreateInfo` \n 
    /// 10. Among these configurations, which ones are allowed to be dynamic -- `VkPipelineDynamicStateCreateInfo` \n 
    /// 11. How should global uniform data be passed to the shaders -- `VkPipelineLayout` \n 
    /// 12. Which pass and subpass should be used, or, in case of dynamic rendering, how are attachments arranged -- `vkPipelineRenderingCreateInfo`. \n 
    /// Some of these structs can be determined statically when loading the material, and are stored in this struct or left as default.
    /// And these are either fixed or filled in at run-time: 
    /// 2nd and 3rd, which are decided based on the mesh to be drawn;
    /// 5th, which is always dynamic and unspecified until rendering; 
    /// 7th, which is decided by run-time settings;
    /// 9th, which is always disabled;
    /// 10th, which is always fixed to be viewports and scissors.
    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateSinglePassProperties
    {
        REFL_SER_BODY(MaterialTemplateSinglePassProperties)

        MaterialTemplateSinglePassProperties() = default;
        virtual ~MaterialTemplateSinglePassProperties() = default;

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
        } rasterizer {};

        /// @brief C.f. `vkPipelineDepthStencilStateCreateInfo`
        struct DSProperties {
            bool ds_write_enabled{true};
            bool ds_test_enabled{true};
            float min_depth{0.0f};
            float max_depth{1.0f};
        } depth_stencil {};
        
        /// @brief C.f. `vkPipelineShaderStageCreateInfo`
        struct Shaders {
            /// @brief A vector of all shader programs used in the pipeline
            std::vector <AssetRef> shaders {};
            // TODO: Support shader specialization
            // std::vector <...> specialization;

            /// @brief stores information regarding layout info, aka descriptors, shared across all stages.
            std::vector <ShaderVariableProperty> uniforms {};
        } shaders {};

        /// @brief C.f. `vkPipelineRenderingCreateInfo`
        /// Use UNDEFINED image format to adapt to swapchain.
        struct Attachments {
            /// @brief Color attachments. If left empty, then this struct is ignored
            /// and the pipeline will be configured to use the current swapchain as attachments.
            std::vector <ImageUtils::ImageFormat> color {};
            std::vector <AttachmentUtils::AttachmentOp> color_ops {};

            ImageUtils::ImageFormat depth {};
            ImageUtils::ImageFormat stencil {};
            AttachmentUtils::AttachmentOp ds_ops {};
        } attachments {};

        // XXX: We had better support pipeline caches to speed up loading...
        // C.f. `vkGetPipelineCacheData`
        // void * pipeline_cache.
    };

    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateProperties
    {
        REFL_SER_BODY(MaterialTemplateProperties)

        MaterialTemplateProperties() = default;
        virtual ~MaterialTemplateProperties() = default;

        std::unordered_map <uint32_t, MaterialTemplateSinglePassProperties> properties {};
    };

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateAsset : public Asset
    {
        REFL_SER_BODY(MaterialTemplateAsset)
    public:

        MaterialTemplateAsset() = default;
        virtual ~MaterialTemplateAsset() = default;

        MaterialTemplateProperties properties;
        std::string name;
    };
}

#endif // ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
