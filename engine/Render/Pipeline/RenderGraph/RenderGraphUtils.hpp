#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHUTILS_INCLUDED
#define PIPELINE_RENDERGRAPH_RENDERGRAPHUTILS_INCLUDED

#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

#include <tuple>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace RenderGraphImpl {

        using GeneralAccessTuple = std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2>;
        using ImageAccessTuple = std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2, vk::ImageLayout>;

        struct TextureAccessMemo {
            using AccessTuple = ImageAccessTuple;
            std::unordered_map<const Texture *, AccessTuple> m_memo;
            std::unordered_map<const Texture *, AccessTuple> m_initial_access;

            void RegisterTexture(const Texture *texture, AccessTuple previous_access) {
                if (m_memo.contains(texture)) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER, "Texture %p is already registered.", static_cast<const void *>(texture)
                    );
                }
                m_memo[texture] = previous_access;
                m_initial_access[texture] = previous_access;
            }

            void UpdateAccessTuple(const Texture *texture, AccessTuple new_access_tuple) {
                if (!m_memo.contains(texture)) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Texture %p is not registered, defaulting to none.",
                        static_cast<const void *>(texture)
                    );

                    RegisterTexture(texture, std::make_tuple(vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone, vk::ImageLayout::eUndefined));
                }

                m_memo[texture] = new_access_tuple;
            };

            AccessTuple GetAccessTuple(const Texture *texture) noexcept {
                if (!m_memo.contains(texture)) {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Texture %p is not registered, defaulting to none.",
                        static_cast<const void *>(texture)
                    );

                    RegisterTexture(texture, std::make_tuple(vk::PipelineStageFlagBits2::eNone, vk::AccessFlagBits2::eNone, vk::ImageLayout::eUndefined));
                }
                return m_memo.at(texture);
            }
        };

        struct RenderGraphExtraInfo {
            std::unordered_map<const Texture *, ImageAccessTuple> m_initial_image_access;
            std::unordered_map<const Texture *, ImageAccessTuple> m_final_image_access;
        };

        inline vk::ImageMemoryBarrier2 GetImageBarrier(const Texture &texture,
            TextureAccessMemo::AccessTuple old_access,
            TextureAccessMemo::AccessTuple new_access) noexcept {
            vk::ImageMemoryBarrier2 barrier{};
            barrier.image = texture.GetImage();
            barrier.subresourceRange = vk::ImageSubresourceRange{
                ImageUtils::GetVkAspect(texture.GetTextureDescription().format),
                0,
                vk::RemainingMipLevels,
                0,
                vk::RemainingArrayLayers
            };

            if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlagBits::eNone) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to infer aspect range when inserting an image barrier.");
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor | vk::ImageAspectFlagBits::eDepth
                                                      | vk::ImageAspectFlagBits::eStencil;
            }

            std::tie(barrier.srcStageMask, barrier.srcAccessMask, barrier.oldLayout) = old_access;
            std::tie(barrier.dstStageMask, barrier.dstAccessMask, barrier.newLayout) = new_access;

            barrier.dstQueueFamilyIndex = barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
            return barrier;
        }

        inline vk::BufferMemoryBarrier2 GetBufferBarrier(
            Buffer &buffer [[maybe_unused]],
            AccessHelper::BufferAccessType old_access [[maybe_unused]],
            AccessHelper::BufferAccessType new_access [[maybe_unused]]
        ) {
            assert(!"Unimplemented");
            vk::BufferMemoryBarrier2 barrier{};
            return barrier;
        }
    }
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPHUTILS_INCLUDED
