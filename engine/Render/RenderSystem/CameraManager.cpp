#include "CameraManager.h"

#include "Render/Renderer/Camera.h"
#include "Render/DebugUtils.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include <glm.hpp>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>

namespace Engine::RenderSystemState {
    struct CameraManager::impl {
        struct CameraData {
            glm::mat4 view_matrix;
            glm::mat4 proj_matrix;
        };

        static constexpr std::array<vk::DescriptorPoolSize, 1> CAMERA_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer, 16
            }
        };

        static constexpr std::array<vk::DescriptorSetLayoutBinding, 1> CAMERA_DESCRIPTOR_BINDINGS {
            vk::DescriptorSetLayoutBinding{
                0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
            }
        };

        std::weak_ptr <RenderSystem> system {};

        vk::UniqueDescriptorPool camera_descriptor_pool {};

        // Camera descriptor set layout, currently containing only one UBO.
        vk::UniqueDescriptorSetLayout camera_descriptor_set_layout {};

        // A front buffer storing all camera data on the CPU side.
        std::array <CameraData, MAX_CAMERAS> front_buffer {};

        // Back buffer for each frame in flight
        std::unique_ptr <IndexedBuffer> back_buffer {};

        // Descriptors for each frame in flight
        std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> descriptors {};

        // Registered cameras
        std::array <std::weak_ptr<Camera>, MAX_CAMERAS> registered_cameras;
    };

    CameraManager::CameraManager() noexcept : pimpl(std::make_unique<impl>()) {
    }
    CameraManager::~CameraManager() noexcept = default;

    void CameraManager::Create(std::shared_ptr <RenderSystem> system) {
        pimpl->system = system;

        const auto & allocator = system->GetAllocatorState();
        auto device = system->GetDevice();

        vk::DescriptorPoolCreateInfo dpci {
            vk::DescriptorPoolCreateFlagBits{},
            pimpl->descriptors.size(),
            impl::CAMERA_DESCRIPTOR_POOL_SIZE
        };
        pimpl->camera_descriptor_pool = device.createDescriptorPoolUnique(dpci);
        DEBUG_SET_NAME_TEMPLATE(
            device, pimpl->camera_descriptor_pool.get(), "Camera Descriptor Pool"
        );

        // Create decriptor set layout and allocate descriptors
        vk::DescriptorSetLayoutCreateInfo dslci {
            vk::DescriptorSetLayoutCreateFlags{},
            pimpl->CAMERA_DESCRIPTOR_BINDINGS
        };
        pimpl->camera_descriptor_set_layout = device.createDescriptorSetLayoutUnique(dslci);

        std::vector <vk::DescriptorSetLayout> layouts(pimpl->descriptors.size(), pimpl->camera_descriptor_set_layout.get());
        vk::DescriptorSetAllocateInfo dsai {pimpl->camera_descriptor_pool.get(), layouts};
        auto ret = device.allocateDescriptorSets(dsai);
        std::copy_n(ret.begin(), pimpl->descriptors.size(), pimpl->descriptors.begin());

#ifndef NDEBUG
        for (uint32_t i = 0; i < pimpl->descriptors.size(); i++) {
            DEBUG_SET_NAME_TEMPLATE(
                device, pimpl->descriptors[i], std::format("Desc Set - Camera FIF {}", i)
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
            writes[i].dstSet = pimpl->descriptors[i];
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &buffers[i];
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
    void CameraManager::RegisterCamera(std::weak_ptr<Camera> camera) noexcept {
        if (camera.owner_before(std::weak_ptr<Camera>())) {
            std::fill(pimpl->registered_cameras.begin(), pimpl->registered_cameras.end(), std::weak_ptr<Camera>());
            return;
        }
        auto index = camera.lock()->m_display_id;
        assert(index < pimpl->registered_cameras.size());
        if (!pimpl->registered_cameras[index].owner_before(camera)) {
            SDL_LogInfo(SDL_LOG_CATEGORY_RENDER, "Re-registering camera id %u.", index);
        }
        pimpl->registered_cameras[index] = camera;
    }
    void CameraManager::FetchCameraData() {
        for (auto p : pimpl->registered_cameras) {
            // Possible race condition but we don't care anyway.
            if (!p.expired()) {
                auto l = p.lock();
                this->WriteCameraMatrices(l->m_display_id, l->GetViewMatrix(), l->GetProjectionMatrix());
            }
        }
    }
    vk::DescriptorSet CameraManager::GetDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->descriptors.size());
        return pimpl->descriptors[frame_in_flight];
    }
    vk::DescriptorSetLayout CameraManager::GetDescriptorSetLayout() const noexcept {
        assert(pimpl->camera_descriptor_set_layout);
        return pimpl->camera_descriptor_set_layout.get();
    }
    void CameraManager::SetActiveCameraIndex(uint32_t index) noexcept {
        assert(index < pimpl->registered_cameras.size());
        if (pimpl->registered_cameras[index].expired()) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                std::format(
                    "Camera {} is expired or not registered, but is set to be the active camera.", 
                    m_active_camera_index
                ).c_str()
            );
        }
        m_active_camera_index = index;
    }
    uint32_t CameraManager::GetActiveCameraIndex() const noexcept {
        if (pimpl->registered_cameras[m_active_camera_index].expired()) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER, 
                std::format("Currently active camera {} is expired or not registered.", m_active_camera_index).c_str()
            );
        }
        return m_active_camera_index;
    }
} // namespace Engine::RenderSystemState
