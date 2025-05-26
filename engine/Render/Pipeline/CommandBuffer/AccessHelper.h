#ifndef PIPELINE_COMMANDBUFFER_ACCESSHELPER_INCLUDED
#define PIPELINE_COMMANDBUFFER_ACCESSHELPER_INCLUDED

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <tuple>

namespace Engine {
    namespace AccessHelper {
        enum class ImageAccessType {
            None,
            /// Read by transfer command, in transfer source layout.
            TransferRead,
            /// Read by rasterization pipeline in attachment load, blending, etc. operation, in color attachment layout.
            /// @warning Disregarded for now.
            ColorAttachmentRead,
            /// Read by rasterization pipeline in attachment load operation, in depth attachment layout.
            /// @warning Disregarded for now.
            DepthAttachmentRead,
            /// Read by shader as texture with a sampler, in read-only layout.
            ShaderRead,
            /// Write by rasterization pipeline color output stage, in color attachment layout.
            ColorAttachmentWrite,
            /// Write by rasterization pipeline depth test stage, in depth attachment layout.
            DepthAttachmentWrite,
            /// Write by transfer command, in transfer destination layout.
            TransferWrite,
            /// Random write in compute shaders as storage image, in general layout.
            ShaderRandomWrite,
            /// Random read and write in compute shaders as storage image, in general layout.
            ShaderReadRandomWrite
        };

        enum class ImageGraphicsAccessType {
            /// Read by rasterization pipeline in attachment load operation, in color attachment layout.
            /// @warning Disregarded for now.
            ColorAttachmentRead,
            /// Read by rasterization pipeline in attachment load operation, in depth attachment layout.
            /// @warning Disregarded for now.
            DepthAttachmentRead,
            /// Read by shader, in shader readonly layout.
            ShaderRead,
            /// Write by rasterization pipeline color output stage, in color attachment layout.
            ColorAttachmentWrite,
            /// Write by rasterization pipeline depth test stage, in depth attachment layout.
            DepthAttachmentWrite,
        };

        enum class ImageComputeAccessType {
            /// Read by shader as texture with a sampler, in read-only layout.
            ShaderRead,
            /// Random write in compute shaders as storage image, in general layout.
            ShaderRandomWrite,
            /// Random read and write in compute shaders as storage image, in general layout.
            ShaderReadRandomWrite
        };

        constexpr inline std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2, vk::ImageLayout> GetAccessScope(ImageAccessType access)
        {
            switch(access) {
                case ImageAccessType::None:
                    return {
                        vk::PipelineStageFlagBits2::eNone, 
                        vk::AccessFlagBits2::eNone, 
                        vk::ImageLayout::eUndefined
                    };
                case ImageAccessType::TransferRead:
                    return {
                        vk::PipelineStageFlagBits2::eAllTransfer,
                        vk::AccessFlagBits2::eTransferRead,
                        vk::ImageLayout::eTransferSrcOptimal
                    };
                case ImageAccessType::ColorAttachmentRead:
                    break;
                case ImageAccessType::DepthAttachmentRead:
                    break;
                case ImageAccessType::ShaderRead:
                    return {
                        vk::PipelineStageFlagBits2::eAllCommands,
                        vk::AccessFlagBits2::eShaderRead,
                        vk::ImageLayout::eReadOnlyOptimal
                    };
                case ImageAccessType::ColorAttachmentWrite:
                    return {
                        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                        vk::AccessFlagBits2::eColorAttachmentWrite,
                        vk::ImageLayout::eColorAttachmentOptimal,
                    };
                case ImageAccessType::DepthAttachmentWrite:
                    return {
                        vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                        vk::ImageLayout::eDepthStencilAttachmentOptimal
                    };
                case ImageAccessType::TransferWrite:
                    return {
                        vk::PipelineStageFlagBits2::eAllTransfer,
                        vk::AccessFlagBits2::eTransferWrite,
                        vk::ImageLayout::eTransferDstOptimal
                    };
                case ImageAccessType::ShaderRandomWrite:
                    return {
                        vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderStorageWrite,
                        vk::ImageLayout::eGeneral
                    };
                case ImageAccessType::ShaderReadRandomWrite:
                    return {
                        vk::PipelineStageFlagBits2::eComputeShader,
                        vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderStorageRead,
                        vk::ImageLayout::eGeneral
                    };
            }
            SDL_LogError(SDL_LogCategory::SDL_LOG_CATEGORY_RENDER, "Undefined or unimplemented image access type.");
            return std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2, vk::ImageLayout>();
        }
    }
}

#endif // PIPELINE_COMMANDBUFFER_ACCESSHELPER_INCLUDED
