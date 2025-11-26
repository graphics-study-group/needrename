#include "SceneDataManager.h"

#include "Render/Memory/IndexedBuffer.h"
#include "Render/DebugUtils.h"
#include <vulkan/vulkan.hpp>
#include <ext/matrix_transform.hpp>
#include <ext/matrix_clip_space.hpp>

namespace Engine::RenderSystemState {
    struct SceneDataManager::impl {
        vk::Device device;
        vk::UniqueDescriptorPool scene_descriptor_pool;
        vk::UniqueDescriptorSetLayout scene_descriptor_set_layout;

        static constexpr std::array SCENE_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer, 1 * FrameManager::FRAMES_IN_FLIGHT
            },
            vk::DescriptorPoolSize{
                vk::DescriptorType::eCombinedImageSampler, MAX_SHADOW_CASTING_LIGHTS * FrameManager::FRAMES_IN_FLIGHT
            }
        };

        static constexpr std::array SCENE_DESCRIPTOR_BINDINGS {
            // Uniform buffer for lights
            vk::DescriptorSetLayoutBinding{
                0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
            },
            // Shadow maps binding
            vk::DescriptorSetLayoutBinding{
                1, vk::DescriptorType::eCombinedImageSampler, MAX_SHADOW_CASTING_LIGHTS, vk::ShaderStageFlagBits::eAllGraphics
            }
        };

        std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> descriptors{};

        struct {
            struct ShadowCastingLightUniformBuffer {
                uint32_t light_count;
                /// Light source in world coordinate, could be position or direction. 
                /// The last component is unused.
                alignas(16) glm::vec4 light_source[MAX_SHADOW_CASTING_LIGHTS];

                /// Light color in linear RGB multiplied by its strength.
                /// The last component is unused.
                alignas(16) glm::vec4 light_color[MAX_SHADOW_CASTING_LIGHTS];

                /// Light matrices used in shadow mapping.
                /// Precaculated projection * view matrices.
                alignas(16) glm::mat4 light_matrices[MAX_SHADOW_CASTING_LIGHTS];
            };

            struct NonShadowCastingLightUniformBuffer {
                uint32_t light_count;
                /// Light source in world coordinate, could be position or direction. 
                /// The last component is unused.
                alignas(16) glm::vec4 light_source[MAX_NON_SHADOW_CASTING_LIGHTS];

                /// Light color in linear RGB multiplied by its strength.
                /// The last component is unused.
                alignas(16) glm::vec4 light_color[MAX_NON_SHADOW_CASTING_LIGHTS];
            };

            struct LightUniformBuffer {
                ShadowCastingLightUniformBuffer shadow_casting;
                // NonShadowCastingLightUniformBuffer non_shadow_casting;
            };

            LightUniformBuffer front_buffer;
            std::unique_ptr <IndexedBuffer> back_buffer;

            std::array <std::weak_ptr<void>, MAX_SHADOW_CASTING_LIGHTS/* + MAX_NON_SHADOW_CASTING_LIGHTS*/> bound_light_components;
            std::array <std::weak_ptr<RenderTargetTexture>, MAX_SHADOW_CASTING_LIGHTS> bound_shadow_maps;
        } lights;
    };
    SceneDataManager::SceneDataManager() noexcept : pimpl(std::make_unique<impl>()) {
    }
    SceneDataManager::~SceneDataManager() noexcept = default;

    void SceneDataManager::Create(std::shared_ptr<RenderSystem> system) {
        auto & allocator = system->GetAllocatorState();
        pimpl->device = system->GetDevice();

        // Create dedicated descriptor pool
        vk::DescriptorPoolCreateInfo dpci {
            vk::DescriptorPoolCreateFlagBits{},
            pimpl->descriptors.size(),
            impl::SCENE_DESCRIPTOR_POOL_SIZE
        };
        pimpl->scene_descriptor_pool = pimpl->device.createDescriptorPoolUnique(dpci);
        DEBUG_SET_NAME_TEMPLATE(
            pimpl->device, pimpl->scene_descriptor_pool.get(), "Scene Descriptor Pool"
        );

        // Create decriptor set layout and allocate descriptors
        auto scene_descriptor_bindings = pimpl->SCENE_DESCRIPTOR_BINDINGS;
        // Set up immutable samplers for shadow maps
        std::array <vk::Sampler, MAX_SHADOW_CASTING_LIGHTS> immutable_samplers;
        std::fill(
            immutable_samplers.begin(),
            immutable_samplers.end(),
            system->GetSamplerManager().GetSampler(
                ImageUtils::SamplerDesc{
                    .u_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge,
                    .v_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge,
                    .w_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge
                }
            )
        );
        scene_descriptor_bindings[1].setImmutableSamplers(immutable_samplers);
        vk::DescriptorSetLayoutCreateInfo dslci {
            vk::DescriptorSetLayoutCreateFlags{},
            scene_descriptor_bindings
        };
        pimpl->scene_descriptor_set_layout = pimpl->device.createDescriptorSetLayoutUnique(dslci);

        std::vector <vk::DescriptorSetLayout> layouts(pimpl->descriptors.size(), pimpl->scene_descriptor_set_layout.get());
        vk::DescriptorSetAllocateInfo dsai {pimpl->scene_descriptor_pool.get(), layouts};
        auto ret = pimpl->device.allocateDescriptorSets(dsai);
        std::copy_n(ret.begin(), pimpl->descriptors.size(), pimpl->descriptors.begin());

#ifndef NDEBUG
        for (uint32_t i = 0; i < pimpl->descriptors.size(); i++) {
            DEBUG_SET_NAME_TEMPLATE(
               pimpl-> device, pimpl->descriptors[i], std::format("Desc Set - Scene FIF {}", i)
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
        pimpl->device.updateDescriptorSets(writes, {});
    }

    void SceneDataManager::SetLightDirectional(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept {
        assert(index < MAX_SHADOW_CASTING_LIGHTS);
        pimpl->lights.front_buffer.shadow_casting.light_source[index] = glm::vec4(direction, 0.0f);
        pimpl->lights.front_buffer.shadow_casting.light_color[index] = glm::vec4(intensity, 0.0f);
        // TODO: determine clip planes and light eye position from the scene
        auto proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 10.0f);
        proj[1][1] *= -1.0f;
        auto view = glm::lookAtRH(-direction, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
        pimpl->lights.front_buffer.shadow_casting.light_matrices[index] = proj * view;
    }

    void SceneDataManager::SetLightPoint(
        uint32_t index, glm::vec3 direction, glm::vec3 intensity, float radius
    ) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::SetLightCone(
        uint32_t index, glm::vec3 direction, glm::vec3 intensity, float inner_angle, float outer_angle
    ) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::SetLightDirectionalNonShadowCasting(
        uint32_t index, glm::vec3 direction, glm::vec3 intensity
    ) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::SetLightShadowMap(uint32_t index, std::weak_ptr<RenderTargetTexture> shadowmap) noexcept {
        assert(index < MAX_SHADOW_CASTING_LIGHTS);
        assert(!shadowmap.expired());
#ifndef NDEBUG
        {
            auto desc = shadowmap.lock()->GetTextureDescription();
            assert(desc.array_layers == 1 && desc.mipmap_levels == 1);
            assert(desc.dimensions == 2 && desc.depth == 1 && desc.height > 1 && desc.width > 1);
            assert(desc.is_cube_map == false);
            assert(desc.type == ImageUtils::ImageType::DepthAttachment || desc.type == ImageUtils::ImageType::DepthStencilAttachment);
            assert(desc.format == ImageUtils::ImageFormat::D32SFLOAT);
        }
#endif
        pimpl->lights.bound_shadow_maps[index] = shadowmap;
    }

    void SceneDataManager::SetLight(uint32_t index, std::shared_ptr<void> light) noexcept {
        assert(index < MAX_SHADOW_CASTING_LIGHTS);
        pimpl->lights.bound_light_components[index] = light;
    }

    void SceneDataManager::SetLightNonShadowCasting(uint32_t index, std::shared_ptr<void> light) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::SetLightCount(uint32_t count) noexcept {
        assert(count < MAX_SHADOW_CASTING_LIGHTS);
        pimpl->lights.front_buffer.shadow_casting.light_count = count;
    }

    void SceneDataManager::SetLightCountNonShadowCasting(uint32_t count) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::UploadSceneData(uint32_t frame_in_flight) const noexcept {
        // TODO: use some dirty bit check to avoid memory write.
        std::memcpy(
            pimpl->lights.back_buffer->GetSlicePtr(frame_in_flight), 
            &pimpl->lights.front_buffer,
            sizeof (pimpl->lights.front_buffer)
        );
        pimpl->lights.back_buffer->FlushSlice(frame_in_flight);
        
        const auto shadow_casting_light_count = pimpl->lights.front_buffer.shadow_casting.light_count;
        std::vector <vk::DescriptorImageInfo> shadowmap_descriptor_write(
            shadow_casting_light_count,
            vk::DescriptorImageInfo{}
        );

        for (size_t i = 0; i < shadow_casting_light_count; i++) {
            assert(!pimpl->lights.bound_shadow_maps[i].expired());
            shadowmap_descriptor_write[i] = vk::DescriptorImageInfo{
                nullptr, pimpl->lights.bound_shadow_maps[i].lock()->GetImageView(), vk::ImageLayout::eReadOnlyOptimal
            };
        }

        pimpl->device.updateDescriptorSets(
            {
                vk::WriteDescriptorSet{
                    pimpl->descriptors[frame_in_flight], 
                    1, 0, 
                    vk::DescriptorType::eCombinedImageSampler, 
                    shadowmap_descriptor_write
                }
            },
            {}
        );
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
