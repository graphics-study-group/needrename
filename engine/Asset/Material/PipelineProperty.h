#ifndef ASSET_MATERIAL_PIPELINEPROPERTY_INCLUDED
#define ASSET_MATERIAL_PIPELINEPROPERTY_INCLUDED

#include <Reflection/macros.h>
#include <Reflection/serialization_unordered_map.h>
#include <Reflection/serialization_vector.h>
#include <Reflection/serialization_smart_pointer.h>

#include <Render/ImageUtils.h>
#include <Render/AttachmentUtils.h>
#include <Render/Pipeline/PipelineEnums.h>

#include <Asset/AssetRef.h>
#include <Asset/Material/ShaderAsset.h>

namespace Engine
{
    namespace PipelineProperties {
        /// @brief C.f. `vkPipelineRasterizationStateCreateInfo`
        struct REFL_SER_CLASS(REFL_BLACKLIST) RasterizerProperties {
            REFL_SER_SIMPLE_STRUCT(RasterizerProperties)

            using FillingMode = PipelineUtils::FillingMode;
            using CullingMode = PipelineUtils::CullingMode;
            using FrontFace = PipelineUtils::FrontFace;

            FillingMode filling{FillingMode::Fill};
            float line_width{1.0f};
            CullingMode culling{CullingMode::None};
            FrontFace front{FrontFace::Counterclockwise};
        };

        /// @brief C.f. `vkPipelineDepthStencilStateCreateInfo`
        struct REFL_SER_CLASS(REFL_BLACKLIST) DSProperties {
            REFL_SER_SIMPLE_STRUCT(DSProperties)

            bool ds_write_enabled{true};
            bool ds_test_enabled{true};
            float min_depth{0.0f};
            float max_depth{1.0f};
        };
        
        /// @brief C.f. `vkPipelineShaderStageCreateInfo`
        struct REFL_SER_CLASS(REFL_BLACKLIST) Shaders {
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
        };

        /// @brief C.f. https://registry.khronos.org/vulkan/specs/latest/man/html/VkPipelineColorBlendAttachmentState.html
        struct REFL_SER_CLASS(REFL_BLACKLIST) ColorBlendingProperties {
            REFL_SER_SIMPLE_STRUCT(ColorBlendingProperties)

            using BlendOperation = PipelineUtils::BlendOperation;
            using BlendFactor = PipelineUtils::BlendFactor;

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
        struct REFL_SER_CLASS(REFL_BLACKLIST) Attachments {
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
        };
    }
}

#endif // ASSET_MATERIAL_PIPELINEPROPERTY_INCLUDED
