#ifndef ASSET_MATERIAL_PIPELINEPROPERTY
#define ASSET_MATERIAL_PIPELINEPROPERTY

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_unordered_map.h>
#include <Reflection/serialization_vector.h>

#include <Render/ImageUtils.h>
#include <Render/Pipeline/PipelineEnums.h>

#include <Asset/AssetRef.h>
#include <Asset/Shader/ShaderAsset.h>

namespace Engine {
    namespace PipelineProperties {
        /// Rasterizer configuration of the pipeline
        /// @see https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRasterizationStateCreateInfo.html
        struct REFL_SER_CLASS(REFL_BLACKLIST) RasterizerProperties {
            REFL_SER_SIMPLE_STRUCT(RasterizerProperties)

            /// Filling mode of the rasterizer
            using FillingMode = PipelineUtils::FillingMode;
            /// Culling mode of the rasterizer.
            using CullingMode = PipelineUtils::CullingMode;
            /// How the front face of a polygon is determined.
            using FrontFace = PipelineUtils::FrontFace;

            /// Filling mode of the rasterizer.
            FillingMode filling{FillingMode::Fill};
            /// Line width of the rasterizer.
            /// This configuration might be ignored due to hardware limitations.
            float line_width{1.0f};
            /// Culling mode of the rasterizer.
            CullingMode culling{CullingMode::None};
            /// How the front face of a polygon is determined.
            FrontFace front{FrontFace::Counterclockwise};

            /**
             * @brief Depth bias slope factor.
             * Mostly used for shadow maps.
             *
             * If any of `depth_bias_slope` and `depth_bias_constant` is
             * not applicable (e.g. NaNs or Infs) or they are both zero,
             * depth bias will be disabled for the created pipeline.
             */
            float depth_bias_slope{0.0f};

            /**
             * @brief Depth bias constant factor.
             * Mostly used for shadow maps.
             *
             * If any of `depth_bias_slope` and `depth_bias_constant` is
             * not applicable (e.g. NaNs or Infs) or they are both zero,
             * depth bias will be disabled for the created pipeline.
             */
            float depth_bias_constant{0.0f};
        };

        /// @brief Stencil test state of a pipeline
        struct REFL_SER_CLASS(REFL_BLACKLIST) StencilState {
            REFL_SER_SIMPLE_STRUCT(StencilState)

            /// @brief Stencil operation that can be used.
            using StencilOperation = PipelineUtils::StencilOperation;
            /// @brief Comparator that can be used.
            using DSComparator = PipelineUtils::DSComparator;

            /// @brief Operation used if stencil test is failed.
            StencilOperation fail_op{StencilOperation::Keep};
            /// @brief Operation used if both stencil and depth tests are passed.
            StencilOperation pass_op{StencilOperation::Keep};
            /// @brief Operation used if stencil test is passed but depth test is failed.
            StencilOperation zfail_op{StencilOperation::Keep};

            /// @brief Comparator used for stencil test.
            DSComparator comparator{DSComparator::Never};

            /// @brief 8-bits mask applied before stencil comparing.
            uint8_t compare_mask{0xFF};
            /// @brief 8-bits mask applied before stencil writing.
            uint8_t write_mask{0xFF};
            /// @brief Reference value used for stencil comparator.
            /// It is possible to set the reference value in runtime.
            uint8_t reference{0x00};
        };

        /// Depth stencil configuration of the pipeline
        /// @see https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineDepthStencilStateCreateInfo.html
        struct REFL_SER_CLASS(REFL_BLACKLIST) DSProperties {
            REFL_SER_SIMPLE_STRUCT(DSProperties)

            /// @brief Comparator that can be used.
            using DSComparator = PipelineUtils::DSComparator;

            /// @brief Whether depth values of fragments can be written into the depth buffer.
            bool depth_write_enable{true};
            /// @brief Whether depth test is enabled
            bool depth_test_enable{true};
            /// @brief Depth comparator used in depth test.
            /// Defaults to LESS operation, which means less depth => closer.
            DSComparator depth_comparator{DSComparator::Less};

            /// @brief Whether stencil test is enabled
            bool stencil_test_enable{false};
            /// @brief Stencil operation state used by front-facing polygons.
            StencilState stencil_front{};
            /// @brief Stencil operation state used by back-facing polygons.
            StencilState stencil_back{};

            /// @brief Whether depth bound test is enable for the fragment samples.
            /// @see https://docs.vulkan.org/spec/latest/chapters/fragops.html#fragops-dbt
            bool depth_bound_test_enable{false};
            /// @brief minimal depth of depth bound test
            float min_depth{0.0f};
            /// @brief maximal depth of depth bound test
            float max_depth{1.0f};
        };

        /// @brief Shaders used by the pipeline
        /// @see https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageCreateInfo.html
        struct REFL_SER_CLASS(REFL_BLACKLIST) Shaders {
            REFL_SER_SIMPLE_STRUCT(Shaders)

            /// @brief A vector of all shader programs used in the pipeline
            std::vector<AssetRef> shaders{};

            /// @brief Specialization constants.
            /// We are supporting `int` (`int32_t`) only.
            /// `bool` is not supported, but you can use `int` instead.
            /// `float` is not supported until `std::variant` reflection is supported.
            std::unordered_map<uint32_t, int32_t> specialization_constants{};
        };

        /// @brief Color blending operation of the color attachments
        /// @see https://registry.khronos.org/vulkan/specs/latest/man/html/VkPipelineColorBlendAttachmentState.html
        struct REFL_SER_CLASS(REFL_BLACKLIST) ColorBlendingProperties {
            REFL_SER_SIMPLE_STRUCT(ColorBlendingProperties)

            /// Blending operation.
            using BlendOperation = PipelineUtils::BlendOperation;
            /// Blending factor.
            using BlendFactor = PipelineUtils::BlendFactor;
            /// Mask of color channel for color writes.
            using ColorChannelMask = PipelineUtils::ColorChannelMask;

            /**
             * @brief Blending operation of color components.
             * If either `color_op` or
             * `alpha_op` is set to `None`, blending will be disabled,
             * and all other settings specified
             * in the struct is ignored, including `color_write_mask`.
             */
            BlendOperation color_op{BlendOperation::None};
            /**
             * @brief Blending operation of alpha components.
             * If either `color_op` or
             * `alpha_op` is set to `None`, blending will be disabled,
             * and all other settings specified
             * in the struct is ignored, including `color_write_mask`.
             */
            BlendOperation alpha_op{BlendOperation::None};

            /**
             * @brief Factor multiplied to color to be drawn when blending.
             * Common values
             * are `One` and `SrcAlpha`.
             */
            BlendFactor src_color{BlendFactor::One};
            /**
             * @brief Factor multiplied to color in the color attachment when blending.
             *
             * Common values are `Zero` and `OneMinusSrcAlpha`.
             */
            BlendFactor dst_color{BlendFactor::Zero};
            /**
             * @brief Factor multiplied to alpha to be drawn when blending.
             * The most
             * common value is `One`.
             */
            BlendFactor src_alpha{BlendFactor::One};
            /**
             * @brief Factor multiplied to alpha in the color attachment when blending.
             *
             * The most common value is `Zero`.
             */
            BlendFactor dst_alpha{BlendFactor::Zero};

            /**
             * @brief Which color channels can be written by this operation.
             *
             * @todo Automatically set this mask to zero when no color
             * attachment is set to avoid UB.
             */
            ColorChannelMask color_write_mask{ColorChannelMask::All};
        };

        /// @brief Attachment information for the pipeline.
        /// @see https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineRenderingCreateInfo.html
        struct REFL_SER_CLASS(REFL_BLACKLIST) Attachments {
            REFL_SER_SIMPLE_STRUCT(Attachments)

            /// @brief Color attachments. If they and depth attachment are all left empty,
            /// the pipeline will be configured to use the current swapchain as attachments.
            /// You can specify `UNDEFINED` format to use default image format determined at runtime.
            std::vector<ImageUtils::ImageFormat> color{};

            /**
             * @brief Color attachment blending operations.
             * When specified, its size must
             * be equal to the size of `color`.
             */
            std::vector<ColorBlendingProperties> color_blending{};

            /// @brief Depth attachment format. If color attachments and it are all left empty,
            /// the pipeline will be configured to use the current swapchain as attachments.
            ImageUtils::ImageFormat depth{};

            /// @brief Stencial attachment format.
            /// @deprecated Inferred from the depth attachment format and unused.
            ImageUtils::ImageFormat stencil{};
        };

        /**
         * @brief While how many samples are used are determined at run-time,
         * this struct controls how some techniques are used. If multisampling
         * are not actually used, how these techniques perform is
         * implementation-defined.
         *
         * @see https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineMultisampleStateCreateInfo.html
         */
        struct REFL_SER_CLASS(REFL_BLACKLIST) Multisampling {
            REFL_SER_SIMPLE_STRUCT(Multisampling)
            /**
             * @brief Enable alpha-to-coverage technique for multisampling.
             */
            bool alpha_to_coverage_enable{false};
            /**
             * @brief Write one to alpha channel after multisampling.
             * It should be combined with alpha-to-coverage technique.
             */
            bool alpha_to_one_enable{false};
        };
    } // namespace PipelineProperties
} // namespace Engine

#endif // ASSET_MATERIAL_PIPELINEPROPERTY
