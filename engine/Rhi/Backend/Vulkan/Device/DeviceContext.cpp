#include "Rhi/Device/DeviceContext.h"

#include <stdexcept>

#include "Rhi/Backend/Vulkan/VulkanHandles.h"
#include "Rhi/Backend/Vulkan/VulkanStructs.h"

namespace {
    /// @brief The stage a dependency is observed at.
    vk::PipelineStageFlags2 ToVkStage(Engine::Rhi::PipelineScope scope) {
        switch (scope) {
        case Engine::Rhi::PipelineScope::Transfer:
            return vk::PipelineStageFlagBits2::eAllTransfer;
        case Engine::Rhi::PipelineScope::Compute:
            return vk::PipelineStageFlagBits2::eComputeShader;
        case Engine::Rhi::PipelineScope::Graphics:
            return vk::PipelineStageFlagBits2::eAllGraphics;
        default:
            return vk::PipelineStageFlagBits2::eAllCommands;
        }
    }
} // namespace

#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Resource/ImmutableResourceCache.h"
#include "Rhi/Submission/EpochTracker.h"

#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    DeviceContext::DeviceContext(DeviceInterface::DeviceConfiguration cfg) :
        m_device_interface(std::make_unique<DeviceInterface>(std::move(cfg))) {
        VULKAN_HPP_DEFAULT_DISPATCHER.init(ToVulkanInstance(m_device_interface->GetInstance()), ::vkGetInstanceProcAddr);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(ToVulkanDevice(m_device_interface->GetDevice()));

        m_immutable_resource_cache = std::make_unique<ImmutableResourceCache>(m_device_interface->GetDevice());
        m_allocator_state = std::make_unique<AllocatorState>(*m_device_interface);
        m_epoch_tracker = std::make_unique<EpochTracker>(m_device_interface->GetDevice());
        // The arena reads the tracker, so the tracker must exist first.
        m_descriptor_arena = std::make_unique<DescriptorArena>(*this);
        m_compute_kernel_cache = std::make_unique<ComputeKernelCache>(*this);

        const vk::PhysicalDeviceLimits limits =
            ToVulkanPhysicalDevice(m_device_interface->GetPhysicalDevice()).getProperties().limits;
        m_capabilities = DeviceCapabilities{
            .max_push_constant_bytes = limits.maxPushConstantsSize,
            .uniform_buffer_offset_alignment = limits.minUniformBufferOffsetAlignment,
            .max_storage_buffer_range = limits.maxStorageBufferRange,
            .max_compute_invocations = limits.maxComputeWorkGroupInvocations,
            .max_bind_groups = limits.maxBoundDescriptorSets
        };
        // Install the tracker as the allocator's retire sink: from here on every
        // buffer allocated through this context is retire-safe by construction.
        m_allocator_state->SetRetireSink(m_epoch_tracker.get());
    }

    DeviceContext::~DeviceContext() = default;

    const DeviceCapabilities &DeviceContext::GetCapabilities() const noexcept {
        return m_capabilities;
    }

    DeviceInterface &DeviceContext::GetDeviceInterface() noexcept {
        return *m_device_interface;
    }

    const DeviceInterface &DeviceContext::GetDeviceInterface() const noexcept {
        return *m_device_interface;
    }

    AllocatorState &DeviceContext::GetAllocatorState() noexcept {
        return *m_allocator_state;
    }

    const AllocatorState &DeviceContext::GetAllocatorState() const noexcept {
        return *m_allocator_state;
    }

    ImmutableResourceCache &DeviceContext::GetIRCache() noexcept {
        return *m_immutable_resource_cache;
    }

    const ImmutableResourceCache &DeviceContext::GetIRCache() const noexcept {
        return *m_immutable_resource_cache;
    }

    DescriptorArena &DeviceContext::GetDescriptorArena() noexcept {
        return *m_descriptor_arena;
    }

    const DescriptorArena &DeviceContext::GetDescriptorArena() const noexcept {
        return *m_descriptor_arena;
    }

    ComputeKernelCache &DeviceContext::GetComputeKernelCache() noexcept {
        return *m_compute_kernel_cache;
    }

    const ComputeKernelCache &DeviceContext::GetComputeKernelCache() const noexcept {
        return *m_compute_kernel_cache;
    }

    ComputeKernel &DeviceContext::RequestComputeKernel(
        std::string_view module_id, const std::vector<uint32_t> &spirv_code, std::string_view debug_name
    ) {
        return m_compute_kernel_cache->Request(module_id, spirv_code, debug_name);
    }

    ComputeKernel &DeviceContext::RequestComputeKernelFromModule(
        std::string_view, ShaderModuleHandle, std::string_view, std::string_view
    ) {
        // This backend reflects from SPIR-V words; a caller that already has a module
        // has skipped the step this backend needs. Reported rather than approximated.
        throw std::logic_error(
            "RequestComputeKernelFromModule is not available on the Vulkan backend: it "
            "reflects from SPIR-V words, so use RequestComputeKernel."
        );
    }

    ComputeKernel *DeviceContext::FindComputeKernel(std::string_view module_id) noexcept {
        return m_compute_kernel_cache->Find(module_id);
    }

    EpochTracker &DeviceContext::GetEpochTracker() noexcept {
        return *m_epoch_tracker;
    }

    const EpochTracker &DeviceContext::GetEpochTracker() const noexcept {
        return *m_epoch_tracker;
    }

    DeviceHandle DeviceContext::GetDevice() const noexcept {
        return m_device_interface->GetDevice();
    }

    void DeviceContext::WaitForIdle() {
        ToVulkanDevice(m_device_interface->GetDevice()).waitIdle();
        // What was already parked can go: a device-idle wait proves that every
        // submission finished, not that a live resource has no owner.
        m_epoch_tracker->ReleaseAllParked();
        // What the arena adds: entries become eligible, but nothing is released.
        m_descriptor_arena->OnDeviceIdle();
    }

    void DeviceContext::UpdateDescriptorSets(std::span<const WriteDescriptorSet> writes) {
        if (writes.empty()) {
            return;
        }

        // Vulkan reads these through pointers into the two info arrays, so both
        // are sized before anything is built - a reallocation would leave a write
        // pointing at freed storage.
        std::vector<vk::DescriptorBufferInfo> buffer_infos(writes.size());
        std::vector<vk::DescriptorImageInfo> image_infos(writes.size());
        std::vector<vk::WriteDescriptorSet> vk_writes;
        vk_writes.reserve(writes.size());

        for (size_t i = 0; i < writes.size(); i++) {
            const WriteDescriptorSet &write = writes[i];
            vk::WriteDescriptorSet vk_write{};
            vk_write.dstSet = ToVulkanDescriptorSet(write.set);
            vk_write.dstBinding = write.binding;
            vk_write.dstArrayElement = write.array_element;
            vk_write.descriptorCount = 1;
            vk_write.descriptorType = ToVulkanDescriptorType(write.type);

            switch (write.type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::UniformBufferDynamic:
            case DescriptorType::StorageBuffer:
            case DescriptorType::StorageBufferDynamic:
                buffer_infos[i] = vk::DescriptorBufferInfo{
                    ToVulkanBuffer(write.buffer), write.offset, write.range
                };
                vk_write.pBufferInfo = &buffer_infos[i];
                break;
            case DescriptorType::CombinedImageSampler:
                image_infos[i] = vk::DescriptorImageInfo{
                    ToVulkanSampler(write.sampler), ToVulkanImageView(write.image_view), vk::ImageLayout::eReadOnlyOptimal
                };
                vk_write.pImageInfo = &image_infos[i];
                break;
            case DescriptorType::SampledImage:
                image_infos[i] = vk::DescriptorImageInfo{
                    nullptr, ToVulkanImageView(write.image_view), vk::ImageLayout::eReadOnlyOptimal
                };
                vk_write.pImageInfo = &image_infos[i];
                break;
            case DescriptorType::Sampler:
                image_infos[i] = vk::DescriptorImageInfo{ToVulkanSampler(write.sampler), nullptr, vk::ImageLayout::eUndefined};
                vk_write.pImageInfo = &image_infos[i];
                break;
            case DescriptorType::StorageImage:
                image_infos[i] = vk::DescriptorImageInfo{
                    nullptr, ToVulkanImageView(write.image_view), vk::ImageLayout::eGeneral
                };
                vk_write.pImageInfo = &image_infos[i];
                break;
            }
            vk_writes.push_back(vk_write);
        }

        ToVulkanDevice(m_device_interface->GetDevice()).updateDescriptorSets(vk_writes, {});
    }
    bool DeviceContext::WaitForFence(FenceHandle fence, uint64_t timeout_ns) {
        const vk::Result result =
            ToVulkanDevice(m_device_interface->GetDevice()).waitForFences({ToVulkanFence(fence)}, vk::True, timeout_ns);
        return result == vk::Result::eSuccess;
    }

    void DeviceContext::SignalSemaphore(const TimelineSignal &signal) {
        const vk::SemaphoreSignalInfo info{ToVulkanSemaphore(signal.semaphore), signal.value};
        ToVulkanDevice(m_device_interface->GetDevice()).signalSemaphore(info);
    }

    void DeviceContext::Submit(const Submission &submission, FenceHandle fence) {
        std::vector<vk::SemaphoreSubmitInfo> waits;
        waits.reserve(submission.waits.size());
        for (const SubmissionDependency &dependency : submission.waits) {
            waits.push_back(vk::SemaphoreSubmitInfo{
                ToVulkanSemaphore(dependency.semaphore),
                dependency.value,
                ToVkStage(dependency.stage),
                1
            });
        }

        std::vector<vk::SemaphoreSubmitInfo> signals;
        signals.reserve(submission.signals.size());
        for (const SubmissionDependency &dependency : submission.signals) {
            signals.push_back(vk::SemaphoreSubmitInfo{
                ToVulkanSemaphore(dependency.semaphore),
                dependency.value,
                ToVkStage(dependency.stage),
                1
            });
        }

        std::vector<vk::CommandBufferSubmitInfo> command_buffers;
        command_buffers.reserve(submission.command_buffers.size());
        for (const CommandBufferHandle command_buffer : submission.command_buffers) {
            command_buffers.push_back(vk::CommandBufferSubmitInfo{ToVulkanCommandBuffer(command_buffer)});
        }
        const vk::SubmitInfo2 info{vk::SubmitFlags{}, waits, command_buffers, signals};

        const vk::Fence submit_fence = fence.IsValid() ? ToVulkanFence(fence) : nullptr;
        m_device_interface->GetQueueInfo().graphicsQueue.submit2(info, submit_fence);
    }
} // namespace Engine::Rhi


















