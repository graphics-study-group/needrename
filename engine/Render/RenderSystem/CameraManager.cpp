#include "CameraManager.h"

#include "Render/DebugUtils.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include <glm.hpp>
#include <vulkan/vulkan.h>

namespace Engine::RenderSystemState {
    struct CameraManager::impl {
        struct CameraData {
            glm::mat4 view_matrix;
            glm::mat4 proj_matrix;
        };

        static constexpr std::array<vk::DescriptorSetLayoutBinding, 1> CAMERA_DESCRIPTOR_BINDINGS {
            vk::DescriptorSetLayoutBinding{
                0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
            }
        };

        std::weak_ptr <RenderSystem> system {};

        // Camera descriptor set layout, currently containing only one UBO.
        vk::UniqueDescriptorSetLayout camera_descriptor_set_layout {};

        // A front buffer storing all camera data on the CPU side.
        std::array <CameraData, MAX_CAMERAS> front_buffer {};

        // Back buffer for each frame in flight
        std::unique_ptr <IndexedBuffer> back_buffer {};

        // Descriptors for each frame in flight
        std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> descriptors {};
    };

    CameraManager::CameraManager() noexcept : pimpl(std::make_unique<impl>()) {
    }
    CameraManager::~CameraManager() noexcept = default;

    void CameraManager::Create(std::shared_ptr <RenderSystem> system) {
        pimpl->system = system;

        auto desc_pool = system->GetGlobalConstantDescriptorPool().get();
        const auto & allocator = system->GetAllocatorState();
        auto device = system->GetDevice();
        assert(desc_pool && "Camera manager must be initialized after GlobalConstantDescriptorPool.");

        // Create decriptor set layout and allocate descriptors
        vk::DescriptorSetLayoutCreateInfo dslci {
            vk::DescriptorSetLayoutCreateFlags{},
            pimpl->CAMERA_DESCRIPTOR_BINDINGS
        };
        pimpl->camera_descriptor_set_layout = device.createDescriptorSetLayoutUnique(dslci);

        std::vector <vk::DescriptorSetLayout> layouts(pimpl->descriptors.size(), pimpl->camera_descriptor_set_layout.get());
        vk::DescriptorSetAllocateInfo dsai {desc_pool, layouts};
        auto ret = device.allocateDescriptorSets(dsai);
        std::copy_n(ret.begin(), pimpl->descriptors.size(), pimpl->descriptors.begin());

#ifndef NDEBUG
        for (uint32_t i = 0; i < pimpl->descriptors.size(); i++) {
            DEBUG_SET_NAME_TEMPLATE(
                system->GetDevice(), pimpl->descriptors[i], std::format("Desc Set - Camera {}", i)
            );
        }
#endif

        // Allocate the back buffer.
        static_assert(sizeof(impl::CameraData) * MAX_CAMERAS == sizeof(impl::front_buffer));
        pimpl->back_buffer = IndexedBuffer::CreateUnique(
            allocator,
            Buffer::BufferType::Uniform,
            sizeof(impl::front_buffer),
            system->GetDeviceInterface().QueryLimit(DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment),
            pimpl->descriptors.size(),
            "Aggregated Camera Uniform Buffer"
        );
        assert(pimpl->back_buffer);

        // Write out descriptors
        std::vector <vk::DescriptorBufferInfo> buffers(
            pimpl->descriptors.size(),
            vk::DescriptorBufferInfo{
                pimpl->back_buffer->GetBuffer(),
                0,
                pimpl->back_buffer->GetSliceSize()
            }
        );
        std::vector <vk::WriteDescriptorSet> writes(
            pimpl->descriptors.size(),
            vk::WriteDescriptorSet{
                nullptr, 0, 0, vk::DescriptorType::eUniformBuffer, {}, {}, {}
            }
        );
        for (uint32_t i = 0; i < pimpl->descriptors.size(); i++) {
            buffers[i].offset = pimpl->back_buffer->GetSliceOffset(i);
            writes[i].setBufferInfo({buffers[i]});
        }
        device.updateDescriptorSets(writes, {});
    }
    void CameraManager::WriteCameraMatrices(const glm::mat4 &view_matrix, const glm::mat4 &projection_matrix) {
        this->WriteCameraMatrices(this->GetActiveCameraIndex(), view_matrix, projection_matrix);
    }
    void CameraManager::WriteCameraMatrices(
        uint32_t index, const glm::mat4 &view_matrix, const glm::mat4 &projection_matrix
    ) {
        pimpl->front_buffer[index].view_matrix = view_matrix;
        pimpl->front_buffer[index].proj_matrix = projection_matrix;
    }
    void CameraManager::UploadCameraData(uint32_t frame_in_flight) const noexcept {
        std::memcpy(
            pimpl->back_buffer->GetSlicePtr(frame_in_flight), 
            pimpl->front_buffer.data(),
            sizeof (pimpl->front_buffer)
        );
        pimpl->back_buffer->FlushSlice(frame_in_flight);
    }
    vk::DescriptorSet CameraManager::GetDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->descriptors.size());
        return pimpl->descriptors[frame_in_flight];
    }
    vk::DescriptorSetLayout CameraManager::GetDescriptorSetLayout() const noexcept {
        assert(pimpl->camera_descriptor_set_layout);
        return pimpl->camera_descriptor_set_layout.get();
    }
} // namespace Engine::RenderSystemState
