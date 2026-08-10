#include "SwapchainPresentProvider.h"

#include "Render/Memory/MemoryAccessHelper.hpp"
#include "Render/Memory/RenderTargetTexture.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

namespace {
    static constexpr std::array PREFERED_COLOR_FORMATS = {
        vk::Format::eR8G8B8A8Srgb, vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8Srgb, vk::Format::eR8G8B8Unorm
    };

    vk::SurfaceFormatKHR SelectSwapchainFormat(const std::vector<vk::SurfaceFormatKHR> &formats) {
        vk::SurfaceFormatKHR pickedFormat{};
        uint32_t color_formats = PREFERED_COLOR_FORMATS.size();
        for (const auto &format : formats) {
            if (format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
                auto itr = std::find(PREFERED_COLOR_FORMATS.begin(), PREFERED_COLOR_FORMATS.end(), format.format);
                if (itr != PREFERED_COLOR_FORMATS.end()) {
                    color_formats =
                        std::min(color_formats, (uint32_t)std::distance(PREFERED_COLOR_FORMATS.begin(), itr));
                }
            }
        }
        if (color_formats >= PREFERED_COLOR_FORMATS.size()) {
            SDL_LogCritical(
                SDL_LOG_CATEGORY_RENDER, "This device support neither B8G8R8A8 nor R8G8B8A8 swapchain format."
            );
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR,
                "Critical Error",
                "Your GPU does not support required color formats for rendering.\n"
                "This is an unrecoverable error and the program will now terminate.",
                nullptr
            );
            std::terminate();
        }
        pickedFormat.format = PREFERED_COLOR_FORMATS[color_formats];
        return pickedFormat;
    }

    vk::PresentModeKHR SelectPresentMode(const std::vector<vk::PresentModeKHR> &modes) {
        vk::PresentModeKHR pickedMode = vk::PresentModeKHR::eFifo;
        for (const auto &mode : modes) {
            if (mode == vk::PresentModeKHR::eMailbox) {
                pickedMode = mode;
                break;
            }
        }
        if (pickedMode == vk::PresentModeKHR::eFifo) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Mailbox mode not supported, fall back to FIFO.");
        }
        return pickedMode;
    }

    vk::Extent2D SelectSwapchainExtent(const vk::SurfaceCapabilitiesKHR &caps, vk::Extent2D expected_extent) {
        vk::Extent2D extent{};
        if (caps.currentExtent.height != std::numeric_limits<uint32_t>::max()) {
            extent = caps.currentExtent;
        } else {
            extent = expected_extent;
            extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        return extent;
    }

    void RecordCopyCommand(
        vk::CommandBuffer cb,
        const Engine::RenderTargetTexture &texture,
        const std::vector<vk::Image> &images,
        vk::Extent2D extent,
        uint32_t target_framebuffer,
        Engine::Rhi::MemoryAccessTypeImageBits last_access
    ) {
        // The source layout/access is derived from the final RTT's last access
        // (e.g. ShaderRandomWrite → eGeneral after the bloom compute pass),
        // NOT assumed to be a color attachment.
        std::array<vk::ImageMemoryBarrier2, 2> barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eAllCommands,
                Engine::GetAccessFlags({last_access}),
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferRead,
                Engine::GetImageLayout({last_access}),
                vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                texture.GetImage(),
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            },
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eNone,
                vk::PipelineStageFlagBits2::eAllTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                images[target_framebuffer],
                vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
            }
        };

        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, barriers});

        vk::ImageBlit2 blit_region{
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{(int32_t)extent.width, (int32_t)extent.height, 1}},
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            std::array{vk::Offset3D{0, 0, 0}, vk::Offset3D{(int32_t)extent.width, (int32_t)extent.height, 1}},
        };

        vk::BlitImageInfo2 blit_info{
            texture.GetImage(),
            vk::ImageLayout::eTransferSrcOptimal,
            images[target_framebuffer],
            vk::ImageLayout::eTransferDstOptimal,
            1,
            &blit_region,
            vk::Filter::eLinear
        };
        cb.blitImage2(blit_info);

        vk::ImageMemoryBarrier2 post_barrier{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::ePresentSrcKHR,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            images[target_framebuffer],
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };
        cb.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, post_barrier});
    }
} // namespace

namespace Engine::RenderSystemState {
    struct SwapchainPresentProvider::impl {
        const Rhi::DeviceInterface *m_device_interface = nullptr;

        vk::UniqueSwapchainKHR m_swapchain{};
        std::vector<vk::Image> m_images{};
        vk::SurfaceFormatKHR m_image_format{};
        vk::Extent2D m_extent{};

        // One copy command buffer per swapchain image. A swapchain image is only
        // ever owned by one in-flight frame at a time, so indexing by image index
        // is safe across frames.
        std::vector<vk::UniqueCommandBuffer> m_copy_command_buffers{};

        void AllocateCopyCommandBuffers(const vk::Device &device) {
            m_copy_command_buffers.clear();
            auto cbai = vk::CommandBufferAllocateInfo{
                m_device_interface->GetQueueInfo().graphicsPool.get(),
                vk::CommandBufferLevel::ePrimary,
                (uint32_t)m_images.size()
            };
            m_copy_command_buffers = device.allocateCommandBuffersUnique(cbai);
        }
    };

    SwapchainPresentProvider::SwapchainPresentProvider() : pimpl(std::make_unique<impl>()) {
    }

    SwapchainPresentProvider::~SwapchainPresentProvider() = default;

    void SwapchainPresentProvider::Initialize(
        const Rhi::DeviceInterface &device_interface, vk::Extent2D expected_extent
    ) {
        pimpl->m_device_interface = &device_interface;
        Recreate(expected_extent);
    }

    void SwapchainPresentProvider::Recreate(vk::Extent2D expected_extent) {
        const auto &di = *pimpl->m_device_interface;
        const auto swapchain_support = di.GetSwapchainSupport();
        const auto extent = SelectSwapchainExtent(swapchain_support.capabilities, expected_extent);
        const auto format = SelectSwapchainFormat(swapchain_support.formats);
        const auto mode = SelectPresentMode(swapchain_support.modes);

        uint32_t image_count = swapchain_support.capabilities.minImageCount + 1;
        if (swapchain_support.capabilities.maxImageCount > 0
            && image_count > swapchain_support.capabilities.maxImageCount) {
            image_count = swapchain_support.capabilities.maxImageCount;
        }

        vk::SwapchainCreateInfoKHR info;
        info.surface = di.GetSurface();
        info.minImageCount = image_count;
        info.imageFormat = format.format;
        info.imageColorSpace = format.colorSpace;
        info.presentMode = mode;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = vk::ImageUsageFlagBits::eTransferDst;
        info.preTransform = swapchain_support.capabilities.currentTransform;
        info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        info.clipped = vk::True;
        info.oldSwapchain = pimpl->m_swapchain ? pimpl->m_swapchain.get() : nullptr;

        auto graphics_index = di.GetQueueFamily(Rhi::DeviceInterface::QueueFamilyType::GraphicsMain).value();
        std::array indices{
            graphics_index,
            di.GetQueueFamily(Rhi::DeviceInterface::QueueFamilyType::GraphicsPresent).value(),
            di.GetQueueFamily(Rhi::DeviceInterface::QueueFamilyType::AsynchronousComputePresent)
                .value_or(graphics_index),
        };
        std::ranges::sort(indices);
        auto [ret, last] = std::ranges::unique(indices);
        if (std::distance(indices.begin(), ret) > 1) {
            info.imageSharingMode = vk::SharingMode::eConcurrent;
            info.queueFamilyIndexCount = std::distance(indices.begin(), ret);
            info.pQueueFamilyIndices = indices.data();
        } else {
            info.imageSharingMode = vk::SharingMode::eExclusive;
        }

        auto device = di.GetDevice();
        pimpl->m_swapchain = device.createSwapchainKHRUnique(info);
        pimpl->m_images = device.getSwapchainImagesKHR(pimpl->m_swapchain.get());
        pimpl->m_image_format = format;
        pimpl->m_extent = extent;
        pimpl->AllocateCopyCommandBuffers(device);
    }

    vk::Extent2D SwapchainPresentProvider::GetExtent() const {
        return pimpl->m_extent;
    }

    vk::Format SwapchainPresentProvider::GetColorFormat() const {
        return pimpl->m_image_format.format;
    }

    uint32_t SwapchainPresentProvider::GetImageCount() const {
        return (uint32_t)pimpl->m_images.size();
    }

    uint32_t SwapchainPresentProvider::AcquireNextImage(
        vk::Device device, vk::Semaphore image_ready_semaphore, uint64_t timeout
    ) {
        auto result = device.acquireNextImageKHR(pimpl->m_swapchain.get(), timeout, image_ready_semaphore, nullptr);
        if (result.result == vk::Result::eErrorOutOfDateKHR) {
            return ~0u;
        }
        return result.value;
    }

    vk::CommandBuffer SwapchainPresentProvider::PrepareCopy(
        vk::Device device,
        const RenderTargetTexture &final_rtt,
        uint32_t image_index,
        Rhi::MemoryAccessTypeImageBits last_access
    ) {
        // Reuse safety: a swapchain image is only re-acquired after its previous
        // present completes (the present waits on the frame completion
        // credential, which fires after the copy batch), so the previous copy
        // has finished executing before we reset its command buffer.
        auto cb = pimpl->m_copy_command_buffers[image_index].get();
        cb.reset();

        vk::CommandBufferBeginInfo begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        cb.begin(begin_info);
        RecordCopyCommand(cb, final_rtt, pimpl->m_images, pimpl->m_extent, image_index, last_access);
        cb.end();
        return cb;
    }

    bool SwapchainPresentProvider::Present(
        vk::Device device, uint32_t image_index, vk::Semaphore frame_done_semaphore
    ) {
        const auto &present_queue = pimpl->m_device_interface->GetQueueInfo().presentQueue;

        std::array<vk::SwapchainKHR, 1> swapchains{pimpl->m_swapchain.get()};
        std::array<uint32_t, 1> frame_indices{image_index};
        std::array<vk::Semaphore, 1> wait_semaphores{frame_done_semaphore};
        vk::PresentInfoKHR present_info{wait_semaphores, swapchains, frame_indices};

        bool needs_recreating = false;
        try {
            auto result = present_queue.presentKHR(present_info);
            if (result == vk::Result::eSuboptimalKHR) {
                needs_recreating = true;
            }
        } catch (vk::OutOfDateKHRError &) {
            needs_recreating = true;
        }
        return needs_recreating;
    }
} // namespace Engine::RenderSystemState
