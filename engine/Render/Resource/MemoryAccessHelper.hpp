#ifndef RENDER_RESOURCE_MEMORYACCESSHELPER_INCLUDED
#define RENDER_RESOURCE_MEMORYACCESSHELPER_INCLUDED

#include "Rhi/Device/MemoryAccessTypes.h"
#include <vulkan/vulkan.hpp>

namespace Engine {
    constexpr vk::AccessFlags2 GetAccessFlags(Rhi::MemoryAccessTypeBuffer a) noexcept {
        vk::AccessFlags2 ret{};
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::IndirectDrawRead)) {
            ret |= vk::AccessFlagBits2::eIndirectCommandRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::IndexRead)) {
            ret |= vk::AccessFlagBits2::eIndexRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::VertexRead)) {
            ret |= vk::AccessFlagBits2::eVertexAttributeRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderRead)) {
            ret |= vk::AccessFlagBits2::eShaderRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderSampled)) {
            ret |= vk::AccessFlagBits2::eShaderSampledRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead)) {
            ret |= vk::AccessFlagBits2::eShaderStorageRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite)) {
            ret |= vk::AccessFlagBits2::eShaderStorageWrite;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::TransferRead)) {
            ret |= vk::AccessFlagBits2::eTransferRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::TransferWrite)) {
            ret |= vk::AccessFlagBits2::eTransferWrite;
        }
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::HostAccess)) {
            ret |= vk::AccessFlagBits2::eHostRead | vk::AccessFlagBits2::eHostWrite;
        }
        return ret;
    }

    constexpr bool HasReadAccess(Rhi::MemoryAccessTypeBuffer a) noexcept {
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::IndirectDrawRead)
            || a.Test(Rhi::MemoryAccessTypeBufferBits::IndexRead) || a.Test(Rhi::MemoryAccessTypeBufferBits::VertexRead)
            || a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderRead)
            || a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderSampled)
            || a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead)
            || a.Test(Rhi::MemoryAccessTypeBufferBits::TransferRead)) {
            return true;
        }
        return false;
    }

    constexpr bool HasWriteAccess(Rhi::MemoryAccessTypeBuffer a) noexcept {
        if (a.Test(Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite)
            || a.Test(Rhi::MemoryAccessTypeBufferBits::TransferWrite)) {
            return true;
        }
        return false;
    }

    constexpr vk::AccessFlags2 GetAccessFlags(Rhi::MemoryAccessTypeImage a) noexcept {
        vk::AccessFlags2 ret{};
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ColorAttachmentRead)) {
            ret |= vk::AccessFlagBits2::eColorAttachmentRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite)) {
            ret |= vk::AccessFlagBits2::eColorAttachmentWrite;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::DepthStencilAttachmentRead)) {
            ret |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::DepthStencilAttachmentWrite)) {
            ret |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::TransferRead)) {
            ret |= vk::AccessFlagBits2::eTransferRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::TransferWrite)) {
            ret |= vk::AccessFlagBits2::eTransferWrite;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ShaderSampledRead)) {
            ret |= vk::AccessFlagBits2::eShaderSampledRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ShaderRandomRead)) {
            ret |= vk::AccessFlagBits2::eShaderStorageRead;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite)) {
            ret |= vk::AccessFlagBits2::eShaderStorageWrite;
        }
        return ret;
    }

    constexpr vk::ImageLayout GetImageLayout(Rhi::MemoryAccessTypeImage a) noexcept {
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ColorAttachmentRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite)) {
            return vk::ImageLayout::eColorAttachmentOptimal;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::DepthStencilAttachmentRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::DepthStencilAttachmentWrite)) {
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::TransferRead)) {
            return vk::ImageLayout::eTransferSrcOptimal;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::TransferWrite)) {
            return vk::ImageLayout::eTransferDstOptimal;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ShaderSampledRead)) {
            return vk::ImageLayout::eReadOnlyOptimal;
        }
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ShaderRandomRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite)) {
            return vk::ImageLayout::eGeneral;
        }
        return vk::ImageLayout::eUndefined;
    }

    constexpr bool HasReadAccess(Rhi::MemoryAccessTypeImage a) noexcept {
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ColorAttachmentRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::DepthStencilAttachmentRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::TransferRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::ShaderSampledRead)
            || a.Test(Rhi::MemoryAccessTypeImageBits::ShaderRandomRead)) {
            return true;
        }
        return false;
    }

    constexpr bool HasWriteAccess(Rhi::MemoryAccessTypeImage a) noexcept {
        if (a.Test(Rhi::MemoryAccessTypeImageBits::ColorAttachmentWrite)
            || a.Test(Rhi::MemoryAccessTypeImageBits::DepthStencilAttachmentWrite)
            || a.Test(Rhi::MemoryAccessTypeImageBits::TransferWrite)
            || a.Test(Rhi::MemoryAccessTypeImageBits::ShaderRandomWrite)) {
            return true;
        }
        return false;
    }
} // namespace Engine

#endif // RENDER_RESOURCE_MEMORYACCESSHELPER_INCLUDED
