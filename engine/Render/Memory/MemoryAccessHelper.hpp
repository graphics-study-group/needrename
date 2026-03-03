#ifndef RENDER_MEMORY_MEMORYACCESSHELPER
#define RENDER_MEMORY_MEMORYACCESSHELPER

#include <vulkan/vulkan.hpp>
#include "MemoryAccessTypes.h"

namespace Engine {
    constexpr vk::AccessFlags2 GetAccessFlags(MemoryAccessTypeBuffer a) noexcept {
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

    constexpr bool HasReadAccess(MemoryAccessTypeBuffer a) noexcept {
        if (
            a.Test(MemoryAccessTypeBufferBits::IndirectDrawRead) ||
            a.Test(MemoryAccessTypeBufferBits::IndexRead) ||
            a.Test(MemoryAccessTypeBufferBits::VertexRead) ||
            a.Test(MemoryAccessTypeBufferBits::ShaderRead) ||
            a.Test(MemoryAccessTypeBufferBits::ShaderSampled) ||
            a.Test(MemoryAccessTypeBufferBits::ShaderRandomRead) ||
            a.Test(MemoryAccessTypeBufferBits::TransferRead)
        ) {
            return true;
        }
        return false;
    }

    constexpr bool HasWriteAccess(MemoryAccessTypeBuffer a) noexcept {
        if (
            a.Test(MemoryAccessTypeBufferBits::ShaderRandomWrite) ||
            a.Test(MemoryAccessTypeBufferBits::TransferWrite)
        ) {
            return true;
        }
        return false;
    }

    constexpr vk::AccessFlags2 GetAccessFlags(MemoryAccessTypeImage a) noexcept {
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

    constexpr vk::ImageLayout GetImageLayout(MemoryAccessTypeImage a) noexcept {
        if (
            a.Test(MemoryAccessTypeImageBits::ColorAttachmentRead) |
            a.Test(MemoryAccessTypeImageBits::ColorAttachmentWrite)
        ) {
            return vk::ImageLayout::eColorAttachmentOptimal;
        }
        if (
            a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentRead) |
            a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentWrite)
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

    constexpr bool HasReadAccess(MemoryAccessTypeImage a) noexcept {
        if (
            a.Test(MemoryAccessTypeImageBits::ColorAttachmentRead) ||
            a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentRead) ||
            a.Test(MemoryAccessTypeImageBits::TransferRead) ||
            a.Test(MemoryAccessTypeImageBits::ShaderSampledRead) ||
            a.Test(MemoryAccessTypeImageBits::ShaderRandomRead)
         ) {
            return true;
        }
        return false;
    }

    constexpr bool HasWriteAccess(MemoryAccessTypeImage a) noexcept {
        if (
            a.Test(MemoryAccessTypeImageBits::ColorAttachmentWrite) ||
            a.Test(MemoryAccessTypeImageBits::DepthStencilAttachmentWrite) ||
            a.Test(MemoryAccessTypeImageBits::TransferWrite) ||
            a.Test(MemoryAccessTypeImageBits::ShaderRandomWrite)
         ) {
            return true;
        }
        return false;
    }
}

#endif // RENDER_MEMORY_MEMORYACCESSHELPER
