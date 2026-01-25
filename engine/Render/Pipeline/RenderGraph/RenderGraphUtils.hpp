#ifndef PIPELINE_RENDERGRAPH_RENDERGRAPHUTILS_INCLUDED
#define PIPELINE_RENDERGRAPH_RENDERGRAPHUTILS_INCLUDED

#include "Render/Memory/MemoryAccessTypes.h"
#include "Render/Pipeline/CommandBuffer/AccessHelperFuncs.h"

#include <tuple>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

namespace Engine {
    namespace RenderGraphImpl {

        using GeneralAccessTuple = std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2>;
        using ImageAccessTuple = std::tuple<vk::PipelineStageFlags2, vk::AccessFlags2, vk::ImageLayout>;

        enum class PassType {
            Graphics,
            Compute,
            Transfer,
            None
        };

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
            DeviceBuffer &buffer [[maybe_unused]],
            AccessHelper::BufferAccessType old_access [[maybe_unused]],
            AccessHelper::BufferAccessType new_access [[maybe_unused]]
        ) {
            assert(!"Unimplemented");
            vk::BufferMemoryBarrier2 barrier{};
            return barrier;
        }

        struct BufferAccessMemo {
            struct Access {
                uint32_t pass_index;
                MemoryAccessTypeBuffer access;
            };

            // vk::Buffer does not have simple hasher here, so I'm using VkBuffer instead.
            // You can include <vulkan/vulkan_hash.hpp> to get its hasher, though.
            using Memo = std::unordered_map <VkBuffer, std::vector<Access>>;
            Memo accesses;

            void EnsureRecordExists(vk::Buffer b) {
                if (!accesses.contains(b)) {
                    accesses[b] = {};
                }
            }

            /**
             * @brief Update last access entry of a given buffer.
             */
            void UpdateLastAccess(vk::Buffer b, uint32_t pass_index, MemoryAccessTypeBuffer access) {
                EnsureRecordExists(b);
                auto & v = accesses[b];
                if(v.empty() || v.back().pass_index < pass_index) {
                    v.push_back({pass_index, access});
                } else {
                    v.back().access = access;
                }
            }

            static vk::PipelineStageFlags2 GetPipelineStage(PassType p, MemoryAccessTypeBuffer a) noexcept {
                vk::PipelineStageFlags2 ret{};

                if (
                    a.Test(MemoryAccessTypeBufferBits::ShaderRead) |
                    a.Test(MemoryAccessTypeBufferBits::ShaderSampled) |
                    a.Test(MemoryAccessTypeBufferBits::ShaderRandomRead) |
                    a.Test(MemoryAccessTypeBufferBits::ShaderRandomWrite)
                ) {
                    if (p == PassType::Graphics) {
                        ret |= vk::PipelineStageFlagBits2::eAllGraphics;
                    } else if (p == PassType::Compute) {
                        ret |= vk::PipelineStageFlagBits2::eComputeShader;
                    } else {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring invaild access pattern.");
                    }
                }
                if (
                    a.Test(MemoryAccessTypeBufferBits::IndexRead) |
                    a.Test(MemoryAccessTypeBufferBits::VertexRead)
                ) {

                    if (p == PassType::Graphics)   ret |= vk::PipelineStageFlagBits2::eVertexInput;
                    else    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring invaild access pattern.");
                }
                if (
                    a.Test(MemoryAccessTypeBufferBits::IndirectDrawRead)
                ) {
                    if (p == PassType::Graphics)   ret |= vk::PipelineStageFlagBits2::eDrawIndirect;
                    else SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring invaild access pattern.");
                }
                if (
                    a.Test(MemoryAccessTypeBufferBits::TransferRead) |
                    a.Test(MemoryAccessTypeBufferBits::TransferWrite)
                ) {
                    ret |= vk::PipelineStageFlagBits2::eAllTransfer;
                }
                if (
                    a.Test(MemoryAccessTypeBufferBits::HostAccess)
                ) {
                    ret |= vk::PipelineStageFlagBits2::eHost;
                }
                
                return ret;
            }

            static vk::AccessFlags2 GetAccessFlags(MemoryAccessTypeBuffer a) noexcept {
                vk::AccessFlags2 ret{};
                if (a.Test(MemoryAccessTypeBufferBits::IndirectDrawRead)) {
                    ret |= vk::AccessFlagBits2::eIndirectCommandRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::IndexRead)) {
                    ret |= vk::AccessFlagBits2::eIndexRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::VertexRead)) {
                    ret |= vk::AccessFlagBits2::eVertexAttributeRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::ShaderRead)) {
                    ret |= vk::AccessFlagBits2::eShaderRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::ShaderSampled)) {
                    ret |= vk::AccessFlagBits2::eShaderSampledRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::ShaderRandomRead)) {
                    ret |= vk::AccessFlagBits2::eShaderStorageRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::ShaderRandomWrite)) {
                    ret |= vk::AccessFlagBits2::eShaderStorageWrite;
                }
                if (a.Test(MemoryAccessTypeBufferBits::TransferRead)) {
                    ret |= vk::AccessFlagBits2::eTransferRead;
                }
                if (a.Test(MemoryAccessTypeBufferBits::TransferWrite)) {
                    ret |= vk::AccessFlagBits2::eTransferWrite;
                }
                if (a.Test(MemoryAccessTypeBufferBits::HostAccess)) {
                    ret |= vk::AccessFlagBits2::eHostRead | vk::AccessFlagBits2::eHostWrite;
                }
                return ret;
            }

            static vk::BufferMemoryBarrier2 GenerateBarrier(
                vk::Buffer buffer,
                MemoryAccessTypeBuffer prev_access,
                PassType prev_pass_type,
                MemoryAccessTypeBuffer curr_access,
                PassType curr_pass_type
            ) {
                return vk::BufferMemoryBarrier2 {
                    GetPipelineStage(prev_pass_type, prev_access),
                    GetAccessFlags(prev_access),
                    GetPipelineStage(curr_pass_type, curr_access),
                    GetAccessFlags(curr_access),
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    buffer,
                    0, vk::WholeSize
                };
            }
        };
    }
}

#endif // PIPELINE_RENDERGRAPH_RENDERGRAPHUTILS_INCLUDED
