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
            struct Access {
                int32_t pass_index;
                MemoryAccessTypeImage access;
            };

            using Memo = std::unordered_map<const Texture *, std::vector<Access>>;
            Memo accesses;

            void EnsureRecordExists(const Texture * t) {
                if (!accesses.contains(t)) {
                    accesses[t] = {};
                }
            }

            /**
             * @brief Update last access entry of a given buffer.
             */
            void UpdateLastAccess(const Texture * t, int32_t pass_index, MemoryAccessTypeImage access) {
                EnsureRecordExists(t);
                auto & v = accesses[t];
                if(v.empty()) {
                    v.push_back({pass_index, access});
                } else {
                    auto itr = v.begin();
                    while (itr->pass_index < pass_index && itr != v.end()) itr++;

                    if (itr == v.end()) v.push_back({pass_index, access});
                    else if (itr->pass_index == pass_index) itr->access = access;
                    else v.insert(itr, {pass_index, access});
                }
            }

            static constexpr vk::PipelineStageFlags2 GetPipelineStage(PassType p, MemoryAccessTypeImage a) noexcept {
                vk::PipelineStageFlags2 ret{};
                if (
                    a.Test(MemoryAccessTypeImageBits::ColorAttachmentRead) |
                    a.Test(MemoryAccessTypeImageBits::ColorAttachmentWrite) |
                    a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentRead) |
                    a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentWrite)
                ) {
                    if (p == PassType::Graphics)    ret |= vk::PipelineStageFlagBits2::eAllGraphics;
                    else    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring invaild access pattern.");
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::TransferRead) |
                    a.Test(MemoryAccessTypeImageBits::TransferWrite)
                ) {
                    ret |= vk::PipelineStageFlagBits2::eAllTransfer;
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::ShaderSampledRead) |
                    a.Test(MemoryAccessTypeImageBits::ShaderRandomRead) |
                    a.Test(MemoryAccessTypeImageBits::ShaderRandomWrite)
                ) {
                    if (p == PassType::Graphics) {
                        ret |= vk::PipelineStageFlagBits2::eAllGraphics;
                    } else if (p == PassType::Compute) {
                        ret |= vk::PipelineStageFlagBits2::eComputeShader;
                    } else {
                        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring invaild access pattern.");
                    }
                }
                return ret;
            }

            static constexpr vk::AccessFlags2 GetAccessFlags(MemoryAccessTypeImage a) noexcept {
                vk::AccessFlags2 ret{};
                if (a.Test(MemoryAccessTypeImageBits::ColorAttachmentRead)) {
                    ret |= vk::AccessFlagBits2::eColorAttachmentRead;
                }
                if (a.Test(MemoryAccessTypeImageBits::ColorAttachmentWrite)) {
                    ret |= vk::AccessFlagBits2::eColorAttachmentWrite;
                }
                if (a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentRead)) {
                    ret |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
                }
                if (a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentWrite)) {
                    ret |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
                }
                if (a.Test(MemoryAccessTypeImageBits::TransferRead)) {
                    ret |= vk::AccessFlagBits2::eTransferRead;
                }
                if (a.Test(MemoryAccessTypeImageBits::TransferWrite)) {
                    ret |= vk::AccessFlagBits2::eTransferWrite;
                }
                if (a.Test(MemoryAccessTypeImageBits::ShaderSampledRead)) {
                    ret |= vk::AccessFlagBits2::eShaderSampledRead;
                }
                if (a.Test(MemoryAccessTypeImageBits::ShaderRandomRead)) {
                    ret |= vk::AccessFlagBits2::eShaderStorageRead;
                }
                if (a.Test(MemoryAccessTypeImageBits::ShaderRandomWrite)) {
                    ret |= vk::AccessFlagBits2::eShaderStorageWrite;
                }
                return ret;
            }

            static constexpr vk::ImageLayout GetImageLayout(MemoryAccessTypeImage a) noexcept {
                if (
                    a.Test(MemoryAccessTypeImageBits::ColorAttachmentRead) |
                    a.Test(MemoryAccessTypeImageBits::ColorAttachmentWrite)
                ) {
                    return vk::ImageLayout::eColorAttachmentOptimal;
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentRead) |
                    a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentRead)
                ) {
                    return vk::ImageLayout::eDepthStencilAttachmentOptimal;
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::TransferRead)
                ) {
                    return vk::ImageLayout::eTransferSrcOptimal;
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::TransferWrite)
                ) {
                    return vk::ImageLayout::eTransferDstOptimal;
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::ShaderSampledRead)
                ) {
                    return vk::ImageLayout::eReadOnlyOptimal;
                }
                if (
                    a.Test(MemoryAccessTypeImageBits::ShaderRandomRead) |
                    a.Test(MemoryAccessTypeImageBits::ShaderRandomWrite)
                ) {
                    return vk::ImageLayout::eGeneral;
                }
                return vk::ImageLayout::eUndefined;
            }

            static constexpr vk::ImageMemoryBarrier2 GenerateBarrier(
                vk::Image image,
                MemoryAccessTypeImage prev_access,
                PassType prev_pass_type,
                MemoryAccessTypeImage curr_access,
                PassType curr_pass_type,
                vk::ImageAspectFlags aspect,
                uint32_t mipmap_base = 0,
                uint32_t mipmap_range = vk::RemainingMipLevels,
                uint32_t array_layer_base = 0,
                uint32_t array_layer_range = vk::RemainingArrayLayers
            ) {
                return vk::ImageMemoryBarrier2 {
                    GetPipelineStage(prev_pass_type, prev_access),
                    GetAccessFlags(prev_access),
                    GetPipelineStage(curr_pass_type, curr_access),
                    GetAccessFlags(curr_access),
                    GetImageLayout(prev_access),
                    GetImageLayout(curr_access),
                    vk::QueueFamilyIgnored,
                    vk::QueueFamilyIgnored,
                    image,
                    vk::ImageSubresourceRange {
                        aspect,
                        mipmap_base, mipmap_range,
                        array_layer_base, array_layer_range
                    }
                };
            }
        };

        struct RenderGraphExtraInfo {
            std::unordered_map<const Texture *, ImageAccessTuple> m_initial_image_access;
            std::unordered_map<const Texture *, ImageAccessTuple> m_final_image_access;
        };

        struct BufferAccessMemo {
            struct Access {
                int32_t pass_index;
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
            void UpdateLastAccess(vk::Buffer b, int32_t pass_index, MemoryAccessTypeBuffer access) {
                EnsureRecordExists(b);
                auto & v = accesses[b];
                if(v.empty()) {
                    v.push_back({pass_index, access});
                } else {
                    auto itr = v.begin();
                    while (itr->pass_index < pass_index && itr != v.end()) itr++;

                    if (itr == v.end()) v.push_back({pass_index, access});
                    else if (itr->pass_index == pass_index) itr->access = access;
                    else v.insert(itr, {pass_index, access});
                }
            }

            static constexpr vk::PipelineStageFlags2 GetPipelineStage(PassType p, MemoryAccessTypeBuffer a) noexcept {
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

            static constexpr vk::AccessFlags2 GetAccessFlags(MemoryAccessTypeBuffer a) noexcept {
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

            static constexpr vk::BufferMemoryBarrier2 GenerateBarrier(
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
