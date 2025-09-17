#ifndef ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <Asset/Material/PipelineProperty.h>
#include <Asset/Material/ShaderAsset.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_unordered_map.h>
#include <Reflection/serialization_vector.h>
#include <Render/AttachmentUtils.h>
#include <Render/ImageUtils.h>

namespace Engine {
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
    /// 12. Which pass and subpass should be used, or, in case of dynamic rendering, how are attachments arranged --
    /// `vkPipelineRenderingCreateInfo`. \n Some of these structs can be determined statically when loading the
    /// material, and are stored in this struct or left as default. And these are either fixed or filled in at run-time:
    /// 2nd and 3rd, which are decided based on the mesh to be drawn;
    /// 5th, which is always dynamic and unspecified until rendering;
    /// 7th, which is always disabled due to poor compatibility with deferred rendering;
    /// 10th, which is always fixed to be viewports and scissors.
    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateSinglePassProperties {
        REFL_SER_BODY(MaterialTemplateSinglePassProperties)

        REFL_ENABLE MaterialTemplateSinglePassProperties() = default;
        virtual ~MaterialTemplateSinglePassProperties() = default;

        using RasterizerProperties = PipelineProperties::RasterizerProperties;
        using DSProperties = PipelineProperties::DSProperties;
        using Shaders = PipelineProperties::Shaders;
        using Attachments = PipelineProperties::Attachments;

        /// @brief C.f. `vkPipelineRasterizationStateCreateInfo`
        REFL_SER_ENABLE RasterizerProperties rasterizer{};

        /// @brief C.f. `vkPipelineDepthStencilStateCreateInfo`
        REFL_SER_ENABLE DSProperties depth_stencil{};

        /// @brief C.f. `vkPipelineShaderStageCreateInfo`
        REFL_SER_ENABLE Shaders shaders{};

        /// @brief C.f. `vkPipelineRenderingCreateInfo`
        /// Use UNDEFINED image format to adapt to swapchain.
        REFL_SER_ENABLE Attachments attachments{};

        // XXX: We had better support pipeline caches to speed up loading...
        // C.f. `vkGetPipelineCacheData`
        // void * pipeline_cache.
    };

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateAsset : public Asset {
        REFL_SER_BODY(MaterialTemplateAsset)
    public:
        REFL_ENABLE MaterialTemplateAsset() = default;
        virtual ~MaterialTemplateAsset() = default;

        REFL_SER_ENABLE MaterialTemplateSinglePassProperties properties{};
        REFL_SER_ENABLE std::string name{};
    };
} // namespace Engine

#endif // ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
