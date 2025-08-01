#ifndef PIPELINE_COMMANDBUFFER_ACCESSHELPERFUNCS_INCLUDED
#define PIPELINE_COMMANDBUFFER_ACCESSHELPERFUNCS_INCLUDED

/**
 * @file This header contains function implementation for AccessHelper classes. 
 * Do not include it in other headers. 
 */

#include "AccessHelperTypes.h"

#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <tuple>

namespace Engine {
    namespace AccessHelper {
        /**
         * @brief Obtain access scope from access type.
         */
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
                return {
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentRead,
                    vk::ImageLayout::eColorAttachmentOptimal
                };
            case ImageAccessType::DepthAttachmentRead:
                return {
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal
                };
            case ImageAccessType::ShaderRead:
                return {
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eShaderRead,
                    vk::ImageLayout::eReadOnlyOptimal
                };
            case ImageAccessType::ColorAttachmentWrite:
                return {
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                    vk::ImageLayout::eColorAttachmentOptimal,
                };
            case ImageAccessType::DepthAttachmentWrite:
                return {
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                    vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
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

        /**
         * @brief Infer the related aspect of an image from usages.
         */
        [[deprecated]]
        constexpr inline vk::ImageAspectFlags InferImageAspectFromUsage(ImageAccessType currentAccess, ImageAccessType previousAccess)
        {
            switch(previousAccess) {
            case ImageAccessType::ColorAttachmentRead:
            case ImageAccessType::ColorAttachmentWrite:
            case ImageAccessType::ShaderRead:
                return vk::ImageAspectFlagBits::eColor;
            case ImageAccessType::DepthAttachmentRead:
            case ImageAccessType::DepthAttachmentWrite:
                return vk::ImageAspectFlagBits::eDepth;
            default:
                switch(currentAccess) {
                case ImageAccessType::ColorAttachmentRead:
                case ImageAccessType::ColorAttachmentWrite:
                case ImageAccessType::ShaderRead:
                        return vk::ImageAspectFlagBits::eColor;
                case ImageAccessType::DepthAttachmentRead:
                case ImageAccessType::DepthAttachmentWrite:
                    return vk::ImageAspectFlagBits::eDepth;
                default:
                    return vk::ImageAspectFlagBits::eNone;
                }
            }
        }
    }
}

#endif // PIPELINE_COMMANDBUFFER_ACCESSHELPERFUNCS_INCLUDED
