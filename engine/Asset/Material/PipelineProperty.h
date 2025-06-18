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

        struct REFL_SER_CLASS(REFL_BLACKLIST) StencilState {
            REFL_SER_SIMPLE_STRUCT(StencilState)

            using StencilOperation = PipelineUtils::StencilOperation;
            using DSComparator = PipelineUtils::DSComparator;

            /// @brief Operation used if stencil test is failed.
            StencilOperation fail_op {StencilOperation::Keep};
            /// @brief Operation used if both stencil and depth tests are passed.
            StencilOperation pass_op {StencilOperation::Keep};
            /// @brief Operation used if stencil test is passed but depth test is failed.
            StencilOperation zfail_op {StencilOperation::Keep};

            /// @brief Comparator used for stencil test.
            DSComparator comparator {DSComparator::Never};

            /// @brief 8-bits mask applied before stencil comparing.
            uint8_t compare_mask {0xFF};
            /// @brief 8-bits mask applied before stencil writing.
            uint8_t write_mask {0xFF};
            /// @brief Reference value used for stencil comparator.
            /// It is possible to set the reference value in runtime.
            uint8_t reference {0x00};
        };

        /// @brief C.f. `vkPipelineDepthStencilStateCreateInfo`
        struct REFL_SER_CLASS(REFL_BLACKLIST) DSProperties {
            REFL_SER_SIMPLE_STRUCT(DSProperties)

            using DSComparator = PipelineUtils::DSComparator;

            bool depth_write_enable{true};
            bool depth_test_enable{true};
            /// @brief Depth comparator used in depth test.
            /// Defaults to LESS operation, which means less depth => closer.
            DSComparator depth_comparator{DSComparator::Less};

            bool stencil_test_enable{false};
            /// @brief Stencil operation state used by front-facing polygons.
            StencilState stencil_front{};
            /// @brief Stencil operation state used by back-facing polygons.
            StencilState stencil_back{};

            bool depth_bound_test_enable{false};
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
            using ColorChannelMask = PipelineUtils::ColorChannelMask;

            /**
             * @brief Blending operation of color components.
             * If either `color_op` or `alpha_op` is set to `None`, blending will be disabled,
             * and all other settings specified in the struct is ignored, including `color_write_mask`.
             */
            BlendOperation color_op {BlendOperation::None};
            /**
             * @brief Blending operation of alpha components.
             * If either `color_op` or `alpha_op` is set to `None`, blending will be disabled,
             * and all other settings specified in the struct is ignored, including `color_write_mask`.
             */
            BlendOperation alpha_op {BlendOperation::None};

            /**
             * @brief Factor multiplied to color to be drawn when blending.
             * Common values are `One` and `SrcAlpha`.
             */
            BlendFactor src_color {BlendFactor::One};
            /**
             * @brief Factor multiplied to color in the color attachment when blending.
             * Common values are `Zero` and `OneMinusSrcAlpha`.
             */
            BlendFactor dst_color {BlendFactor::Zero};
            /**
             * @brief Factor multiplied to alpha to be drawn when blending.
             * The most common value is `One`.
             */
            BlendFactor src_alpha {BlendFactor::One};
            /**
             * @brief Factor multiplied to alpha in the color attachment when blending.
             * The most common value is `Zero`.
             */
            BlendFactor dst_alpha {BlendFactor::Zero};

            ColorChannelMask color_write_mask {ColorChannelMask::All};
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

            /// @deprecated It should be specified at runtime.
            AttachmentUtils::AttachmentOp ds_ops {};
        };
    }
}

#endif // ASSET_MATERIAL_PIPELINEPROPERTY_INCLUDED
