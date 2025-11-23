#include "SceneDataManager.h"


#include "Render/DebugUtils.h"
#include <vulkan/vulkan.hpp>

namespace Engine::RenderSystemState {
    struct SceneDataManager::impl {
        vk::UniqueDescriptorPool scene_descriptor_pool;
        vk::UniqueDescriptorSetLayout scene_descriptor_set_layout;

        static constexpr std::array<vk::DescriptorPoolSize, 1> SCENE_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer, 16
            }
        };

        static constexpr std::array<vk::DescriptorSetLayoutBinding, 1> SCENE_DESCRIPTOR_BINDINGS {
            // Uniform buffer for lights
            vk::DescriptorSetLayoutBinding{
                0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
            }
        };

        std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> descriptors{};

        struct {
            struct LightUniformBuffer {
                uint32_t light_count;
                // Light source in world coordinate, could be position or direction. 
                // The last component is unused.
                alignas(16) glm::vec4 light_source[MAX_LIGHTS];
                // Light color in linear RGB multiplied by its strength.
                // The last component is unused.
                alignas(16) glm::vec4 light_color[MAX_LIGHTS];
            };

            LightUniformBuffer front_buffer;
            std::unique_ptr <IndexedBuffer> back_buffer;

            std::array <std::weak_ptr<void>, MAX_LIGHTS> bound_light_components;
        } lights;
    };
    SceneDataManager::SceneDataManager() noexcept : pimpl(std::make_unique<impl>()) {
    }
    SceneDataManager::~SceneDataManager() noexcept = default;

    void SceneDataManager::Create(std::shared_ptr<RenderSystem> system) {
        auto & allocator = system->GetAllocatorState();
        auto device = system->GetDevice();

        // Create dedicated descriptor pool
        vk::DescriptorPoolCreateInfo dpci {
            vk::DescriptorPoolCreateFlagBits{},
            pimpl->descriptors.size(),
            impl::SCENE_DESCRIPTOR_POOL_SIZE
        };
        pimpl->scene_descriptor_pool = device.createDescriptorPoolUnique(dpci);
        DEBUG_SET_NAME_TEMPLATE(
            device, pimpl->scene_descriptor_pool.get(), "Scene Descriptor Pool"
        );

        // Create decriptor set layout and allocate descriptors
        vk::DescriptorSetLayoutCreateInfo dslci {
            vk::DescriptorSetLayoutCreateFlags{},
            pimpl->SCENE_DESCRIPTOR_BINDINGS
        };
        pimpl->scene_descriptor_set_layout = device.createDescriptorSetLayoutUnique(dslci);

        std::vector <vk::DescriptorSetLayout> layouts(pimpl->descriptors.size(), pimpl->scene_descriptor_set_layout.get());
        vk::DescriptorSetAllocateInfo dsai {pimpl->scene_descriptor_pool.get(), layouts};
        auto ret = device.allocateDescriptorSets(dsai);
        std::copy_n(ret.begin(), pimpl->descriptors.size(), pimpl->descriptors.begin());

#ifndef NDEBUG
        for (uint32_t i = 0; i < pimpl->descriptors.size(); i++) {
            DEBUG_SET_NAME_TEMPLATE(
                device, pimpl->descriptors[i], std::format("Desc Set - Scene FIF {}", i)
            );
        }
#endif

        // Allocate the back buffer for lights.
        pimpl->lights.back_buffer = IndexedBuffer::CreateUnique(
            allocator,
            Buffer::BufferType::Uniform,
            sizeof(pimpl->lights.front_buffer),
            system->GetDeviceInterface().QueryLimit(DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment),
            pimpl->descriptors.size(),
            "Scene Light Uniform Buffer"
        );
        assert(pimpl->lights.back_buffer);

        // Write out descriptors
        std::vector <vk::DescriptorBufferInfo> buffers(
            pimpl->descriptors.size(),
            vk::DescriptorBufferInfo{
                pimpl->lights.back_buffer->GetBuffer(),
                0,
                pimpl->lights.back_buffer->GetSliceSize()
            }
        );
        std::vector <vk::WriteDescriptorSet> writes(
            pimpl->descriptors.size(),
            vk::WriteDescriptorSet{
                nullptr, 0, 0, vk::DescriptorType::eUniformBuffer, {}, {}, {}
            }
        );
        for (uint32_t i = 0; i < pimpl->descriptors.size(); i++) {
            buffers[i].offset = pimpl->lights.back_buffer->GetSliceOffset(i);
            writes[i].dstSet = pimpl->descriptors[i];
            writes[i].descriptorCount = 1;
            writes[i].pBufferInfo = &buffers[i];
        }
        device.updateDescriptorSets(writes, {});
    }

    void SceneDataManager::SetLightDirectional(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept {
        assert(index < MAX_LIGHTS);
        pimpl->lights.front_buffer.light_source[index] = glm::vec4(direction, 0.0f);
        pimpl->lights.front_buffer.light_color[index] = glm::vec4(intensity, 0.0f);
    }

    void SceneDataManager::SetLight(uint32_t index, std::shared_ptr<void> light) noexcept {
        assert(index < MAX_LIGHTS);
        pimpl->lights.bound_light_components[index] = light;
    }

    void SceneDataManager::SetLightCount(uint32_t count) noexcept {
        assert(count < MAX_LIGHTS);
        pimpl->lights.front_buffer.light_count = count;
    }

    void SceneDataManager::UploadSceneData(uint32_t frame_in_flight) const noexcept {
        std::memcpy(
            pimpl->lights.back_buffer->GetSlicePtr(frame_in_flight), 
            &pimpl->lights.front_buffer,
            sizeof (pimpl->lights.front_buffer)
        );
        pimpl->lights.back_buffer->FlushSlice(frame_in_flight);
        // TODO: We should recompute matrices for the lights here for shadow maps.

        // We might need to perform descriptor writes here to update textures in the future.
    }

    void SceneDataManager::FetchLightData() noexcept {
        for (auto p : pimpl->lights.bound_light_components) {
            // ...
        }
    }

    vk::DescriptorSet SceneDataManager::GetDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->descriptors.size());
        return pimpl->descriptors[frame_in_flight];
    }

    vk::DescriptorSetLayout SceneDataManager::GetDescriptorSetLayout() const noexcept {
        return pimpl->scene_descriptor_set_layout.get();
    }

} // namespace Engine::RenderSystemState
