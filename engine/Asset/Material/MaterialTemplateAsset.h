#ifndef ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_unordered_map.h>
#include <Reflection/serialization_vector.h>
#include <Reflection/serialization_smart_pointer.h>
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
    /// 7th, which is always disabled due to poor compatibility with deferred rendering;
    /// 10th, which is always fixed to be viewports and scissors.
    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateSinglePassProperties
    {
        REFL_SER_BODY(MaterialTemplateSinglePassProperties)

        REFL_ENABLE MaterialTemplateSinglePassProperties() = default;
        virtual ~MaterialTemplateSinglePassProperties() = default;

        /// @brief C.f. `vkPipelineRasterizationStateCreateInfo`
        REFL_SER_ENABLE struct REFL_SER_CLASS(REFL_BLACKLIST) RasterizerProperties {
            REFL_SER_SIMPLE_STRUCT(RasterizerProperties)

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
        REFL_SER_ENABLE struct REFL_SER_CLASS(REFL_BLACKLIST) DSProperties {
            REFL_SER_SIMPLE_STRUCT(DSProperties)

            bool ds_write_enabled{true};
            bool ds_test_enabled{true};
            float min_depth{0.0f};
            float max_depth{1.0f};
        } depth_stencil {};
        
        /// @brief C.f. `vkPipelineShaderStageCreateInfo`
        REFL_SER_ENABLE struct REFL_SER_CLASS(REFL_BLACKLIST) Shaders {
            REFL_SER_SIMPLE_STRUCT(Shaders)

            /// @brief A vector of all shader programs used in the pipeline
            std::vector <std::shared_ptr<AssetRef>> shaders {};
            // TODO: Support shader specialization
            // std::vector <...> specialization;

            /// @brief stores information regarding layout info, aka descriptors, shared across all stages.
            /// @deprecated Automatically reflected from shader SPIR-V source code.
            std::vector <ShaderVariableProperty> uniforms {};

            /// @brief stores information of variables stored in the UBO.
            /// @deprecated Automatically reflected from shader SPIR-V source code.
            std::vector <ShaderInBlockVariableProperty> ubo_variables {};
        } shaders {};

        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkPipelineColorBlendAttachmentState.html
        REFL_SER_ENABLE struct REFL_SER_CLASS(REFL_BLACKLIST) ColorBlendingProperties {
            REFL_SER_SIMPLE_STRUCT(ColorBlendingProperties)

            /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendOp.html
            enum class BlendOperation {
                None,
                Add,
                Substract,
                ReverseSubstract,
                Min,
                Max
            };

            /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkBlendFactor.html
            enum class BlendFactor {
                Zero,
                One,
                SrcColor,
                OneMinusSrcColor,
                DstColor,
                OneMinusDstColor,
                SrcAlpha,
                OneMinusSrcAlpha,
                DstAlpha,
                OneMinusDstAlpha
            };

            /**
             * @brief Blending operation of color components.
             * If either `color_op` or `alpha_op` is set to `None`, blending will be disabled.
             */
            BlendOperation color_op {BlendOperation::None};
            /**
             * @brief Blending operation of alpha components.
             * If either `color_op` or `alpha_op` is set to `None`, blending will be disabled.
             */
            BlendOperation alpha_op {BlendOperation::None};
            BlendFactor src_color {BlendFactor::One};
            BlendFactor dst_color {BlendFactor::Zero};
            BlendFactor src_alpha {BlendFactor::One};
            BlendFactor dst_alpha {BlendFactor::Zero};
        };

        /// @brief C.f. `vkPipelineRenderingCreateInfo`
        /// Use UNDEFINED image format to adapt to swapchain.
        REFL_SER_ENABLE struct REFL_SER_CLASS(REFL_BLACKLIST) Attachments {
            REFL_SER_SIMPLE_STRUCT(Attachments)

            /// @brief Color attachments. If they and depth attachment are all left empty,
            /// the pipeline will be configured to use the current swapchain as attachments.
            /// You can specify `UNDEFINED` format to use default image format determined at runtime.
            std::vector <ImageUtils::ImageFormat> color {};
            /**
             * @brief Color attachment operations.
             * When specified, its size must be equal to the size of `color`.
             * @deprecated It should be specified at runtime.
             */
            std::vector <AttachmentUtils::AttachmentOp> color_ops {};
            /**
             * @brief Color attachment blending operations.
             * When specified, its size must be equal to the size of `color`.
             */
            std::vector <ColorBlendingProperties> color_blending {};

            /// @brief Depth attachment format. If color attachments and it are all left empty,
            /// the pipeline will be configured to use the current swapchain as attachments.
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

        REFL_ENABLE MaterialTemplateProperties() = default;
        virtual ~MaterialTemplateProperties() = default;

        REFL_SER_ENABLE std::unordered_map <uint32_t, MaterialTemplateSinglePassProperties> properties {};
    };

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialTemplateAsset : public Asset
    {
        REFL_SER_BODY(MaterialTemplateAsset)
    public:

        REFL_ENABLE MaterialTemplateAsset() = default;
        virtual ~MaterialTemplateAsset() = default;

        REFL_SER_ENABLE MaterialTemplateProperties properties {};
        REFL_SER_ENABLE std::string name {};
    };
}

#endif // ASSET_MATERIAL_MATERIALTEMPLATEASSET_INCLUDED
