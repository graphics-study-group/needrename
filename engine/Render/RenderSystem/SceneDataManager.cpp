#include "SceneDataManager.h"

#include "Render/Memory/IndexedBuffer.h"
#include "Render/DebugUtils.h"
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>
#include <ext/matrix_transform.hpp>
#include <ext/matrix_clip_space.hpp>
#include <fstream>

namespace Engine::RenderSystemState {
    struct SceneDataManager::impl {
        vk::Device device {};
        vk::UniqueDescriptorPool scene_descriptor_pool {};

        static constexpr std::array SCENE_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer, 1 * FrameManager::FRAMES_IN_FLIGHT
            },
            vk::DescriptorPoolSize{
                // Shadowmaps + skybox cubemap
                vk::DescriptorType::eCombinedImageSampler, (MAX_SHADOW_CASTING_LIGHTS + 1) * FrameManager::FRAMES_IN_FLIGHT
            }
        };

        struct Scene {
            struct ShadowCastingLightUniformBuffer {
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
                /// Light source in world coordinate, could be position or direction. 
                /// The last component is unused.
                alignas(16) glm::vec4 light_source[MAX_NON_SHADOW_CASTING_LIGHTS];

                /// Light color in linear RGB multiplied by its strength.
                /// The last component is unused.
                alignas(16) glm::vec4 light_color[MAX_NON_SHADOW_CASTING_LIGHTS];
            };

            struct LightUniformBuffer {
                uint32_t shadow_casting_light_count;
                uint32_t non_shadow_casting_light_count;
                ShadowCastingLightUniformBuffer shadow_casting;
                NonShadowCastingLightUniformBuffer non_shadow_casting;
            };

            static constexpr std::array DESCRIPTOR_BINDINGS {
                // Uniform buffer for lights
                vk::DescriptorSetLayoutBinding{
                    0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
                },
                // Shadow maps binding
                vk::DescriptorSetLayoutBinding{
                    1, vk::DescriptorType::eCombinedImageSampler, MAX_SHADOW_CASTING_LIGHTS, vk::ShaderStageFlagBits::eAllGraphics
                }
            };

            // Light data
            LightUniformBuffer light_front_buffer{};
            std::unique_ptr <IndexedBuffer> light_back_buffer{};
            std::array <std::weak_ptr<void>, MAX_SHADOW_CASTING_LIGHTS + MAX_NON_SHADOW_CASTING_LIGHTS> bound_light_components{};
            std::array <std::weak_ptr<RenderTargetTexture>, MAX_SHADOW_CASTING_LIGHTS> bound_shadow_maps{};
            std::shared_ptr <RenderTargetTexture> default_light_map;

            // Scene data
            vk::UniqueDescriptorSetLayout scene_descriptor_set_layout{};
            std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> scene_descriptor_sets{};

            void Create(RenderSystem & system, vk::DescriptorPool pool) {
                auto & allocator = system.GetAllocatorState();
                auto device = system.GetDevice();

                // Create decriptor set layout and allocate descriptors for lighting
                auto scene_descriptor_bindings = DESCRIPTOR_BINDINGS;
                // Set up immutable samplers for shadow maps
                std::array <vk::Sampler, MAX_SHADOW_CASTING_LIGHTS> immutable_samplers;
                std::fill(
                    immutable_samplers.begin(),
                    immutable_samplers.end(),
                    system.GetSamplerManager().GetSampler(
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
                scene_descriptor_set_layout = device.createDescriptorSetLayoutUnique(dslci);
                DEBUG_SET_NAME_TEMPLATE(device, scene_descriptor_set_layout.get(), "Scene Descriptor Set Layout");

                std::vector <vk::DescriptorSetLayout> layouts(scene_descriptor_sets.size(), scene_descriptor_set_layout.get());
                vk::DescriptorSetAllocateInfo dsai {pool, layouts};
                auto ret = device.allocateDescriptorSets(dsai);
                std::copy_n(ret.begin(), scene_descriptor_sets.size(), scene_descriptor_sets.begin());

#ifndef NDEBUG
                for (uint32_t i = 0; i < scene_descriptor_sets.size(); i++) {
                    DEBUG_SET_NAME_TEMPLATE(
                        device, scene_descriptor_sets[i], std::format("Desc Set - Scene FIF {}", i)
                    );
                }
#endif

                // Allocate the back buffer for lights.
                light_back_buffer = IndexedBuffer::CreateUnique(
                    allocator,
                    {BufferTypeBits::HostAccessibleUniform},
                    sizeof(pimpl->scene.light_front_buffer),
                    system.GetDeviceInterface().QueryLimit(DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment),
                    scene_descriptor_sets.size(),
                    "Scene Light Uniform Buffer"
                );
                assert(light_back_buffer);

                // Prepare default depth map
                default_light_map = RenderTargetTexture::CreateUnique(
                    system,
                    RenderTargetTexture::RenderTargetTextureDesc{
                        .dimensions = 2,
                        .width = 16, .height = 16, .depth = 1,
                        .mipmap_levels = 1, .array_layers = 1,
                        .format = RenderTargetTexture::RTTFormat::D32SFLOAT,
                        .multisample = 1,
                        .is_cube_map = false
                    },
                    Texture::SamplerDesc{

                    },
                    "Default shadowmap"
                );
                system.GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*default_light_map, 1.0f);

                // Write out descriptors
                std::vector <vk::DescriptorBufferInfo> buffers(
                    scene_descriptor_sets.size(),
                    vk::DescriptorBufferInfo{
                        light_back_buffer->GetBuffer(),
                        0,
                        light_back_buffer->GetSliceSize()
                    }
                );
                std::vector <vk::WriteDescriptorSet> writes(
                    scene_descriptor_sets.size(),
                    vk::WriteDescriptorSet{
                        nullptr, 0, 0, vk::DescriptorType::eUniformBuffer, {}, {}, {}
                    }
                );
                for (uint32_t i = 0; i < scene_descriptor_sets.size(); i++) {
                    buffers[i].offset = light_back_buffer->GetSliceOffset(i);
                    writes[i].dstSet = scene_descriptor_sets[i];
                    writes[i].descriptorCount = 1;
                    writes[i].pBufferInfo = &buffers[i];
                }
                device.updateDescriptorSets(writes, {});
            }
        } scene{};

        struct Skybox {
            std::shared_ptr <MaterialInstance> skybox_material{};
        } skybox{};

        void Create(RenderSystem & system) {
            device = system.GetDevice();

            // Create dedicated descriptor pool
            vk::DescriptorPoolCreateInfo dpci {
                vk::DescriptorPoolCreateFlagBits{},
                scene.scene_descriptor_sets.size(),
                impl::SCENE_DESCRIPTOR_POOL_SIZE
            };
            scene_descriptor_pool = device.createDescriptorPoolUnique(dpci);
            DEBUG_SET_NAME_TEMPLATE(
                device, scene_descriptor_pool.get(), "Scene Descriptor Pool"
            );

            scene.Create(system, scene_descriptor_pool.get());
        }
    };
    SceneDataManager::SceneDataManager(
        RenderSystem & system
    ) noexcept : m_system(system), pimpl(std::make_unique<impl>()) {
    }

    SceneDataManager::~SceneDataManager() noexcept = default;

    void SceneDataManager::Create() {
        pimpl->Create(m_system);
    }

    void SceneDataManager::SetLightDirectional(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept {
        assert(index < MAX_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.shadow_casting.light_source[index] = glm::vec4(direction, 0.0f);
        pimpl->scene.light_front_buffer.shadow_casting.light_color[index] = glm::vec4(intensity, 0.0f);
        // TODO: determine clip planes and light eye position from the scene
        auto proj = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.001f, 10.0f);
        proj[1][1] *= -1.0f;
        auto view = glm::lookAtRH(-direction, glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
        pimpl->scene.light_front_buffer.shadow_casting.light_matrices[index] = proj * view;
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
        assert(index < MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.non_shadow_casting.light_source[index] = glm::vec4(direction, 0.0f);
        pimpl->scene.light_front_buffer.non_shadow_casting.light_color[index] = glm::vec4(intensity, 0.0f);
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
            assert(desc.memory_type.Test(ImageMemoryTypeBits::DepthStencilAttachment));
            assert(desc.format == ImageUtils::ImageFormat::D32SFLOAT);
        }
#endif
        pimpl->scene.bound_shadow_maps[index] = shadowmap;
    }

    void SceneDataManager::SetLight(uint32_t index, std::shared_ptr<void> light) noexcept {
        assert(index < MAX_SHADOW_CASTING_LIGHTS);
        pimpl->scene.bound_light_components[index] = light;
    }

    void SceneDataManager::SetLightNonShadowCasting(uint32_t index, std::shared_ptr<void> light) noexcept {
        assert(index < MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.bound_light_components[MAX_SHADOW_CASTING_LIGHTS + index] = light;
    }

    void SceneDataManager::SetLightCount(uint32_t count) noexcept {
        assert(count < MAX_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.shadow_casting_light_count = count;
    }

    void SceneDataManager::SetLightCountNonShadowCasting(uint32_t count) noexcept {
        assert(count < MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.non_shadow_casting_light_count = count;
    }

    void SceneDataManager::SetSkyboxMaterial(std::shared_ptr<MaterialInstance> material) noexcept {
        pimpl->skybox.skybox_material = material;
    }

    void SceneDataManager::UploadSceneData(uint32_t frame_in_flight) const noexcept {
        // TODO: use some dirty bit check to avoid memory write.
        std::memcpy(
            pimpl->scene.light_back_buffer->GetSlicePtr(frame_in_flight), 
            &pimpl->scene.light_front_buffer,
            sizeof (pimpl->scene.light_front_buffer)
        );
        pimpl->scene.light_back_buffer->FlushSlice(frame_in_flight);
        
        std::vector <vk::WriteDescriptorSet> descriptor_writes;
        descriptor_writes.reserve(2);
        std::vector <vk::DescriptorImageInfo> shadowmap_image_descriptor_writes{MAX_SHADOW_CASTING_LIGHTS, vk::DescriptorImageInfo{}};

        const auto shadow_casting_light_count = pimpl->scene.light_front_buffer.shadow_casting_light_count;
        for (size_t i = 0; i < MAX_SHADOW_CASTING_LIGHTS; i++) {
            bool use_default_map = true;
            if (i < shadow_casting_light_count) {
                if(!pimpl->scene.bound_shadow_maps[i].expired()) {
                    use_default_map = false;
                    shadowmap_image_descriptor_writes[i] = vk::DescriptorImageInfo{
                        nullptr, pimpl->scene.bound_shadow_maps[i].lock()->GetImageView(), vk::ImageLayout::eReadOnlyOptimal
                    };
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Shadowmap %llu is not assigned, and is defaulted.", i);
                }
            }

            if (use_default_map) {
                shadowmap_image_descriptor_writes[i] = vk::DescriptorImageInfo{
                    nullptr, pimpl->scene.default_light_map->GetImageView(), vk::ImageLayout::eReadOnlyOptimal
                };
            }
        }

        descriptor_writes.push_back(
            vk::WriteDescriptorSet{
                pimpl->scene.scene_descriptor_sets[frame_in_flight], 1, 0,
                vk::DescriptorType::eCombinedImageSampler,
                shadowmap_image_descriptor_writes
            }
        );

        if (pimpl->skybox.skybox_material) {
            auto tpl = pimpl->skybox.skybox_material->GetLibrary().FindMaterialTemplate("SKYBOX", {0});
            pimpl->skybox.skybox_material->UpdateGPUInfo(*tpl, frame_in_flight);
        }

        if (!descriptor_writes.empty()) {
            pimpl->device.updateDescriptorSets(
                {descriptor_writes},
                {}
            );
        }
    }

    void SceneDataManager::FetchLightData() noexcept {
        for (auto p : pimpl->scene.bound_light_components) {
            // ...
        }
    }

    void SceneDataManager::DrawSkybox(
        vk::CommandBuffer cb, uint32_t frame_in_flight, glm::mat3 view_mat, glm::mat4 proj_mat
    ) const {
        if (!pimpl->skybox.skybox_material)  return;

        vk::Extent2D extent = m_system.GetSwapchain().GetExtent();
        vk::Rect2D scissor{{0, 0}, extent};
        vk::Viewport vp;
        vp.setWidth(extent.width).setHeight(extent.height);
        vp.setX(0.0f).setY(0.0f);
        vp.setMaxDepth(1.0f).setMinDepth(0.0f);
        cb.setViewport(0, 1, &vp);
        cb.setScissor(0, 1, &scissor);

        glm::mat4 pv = proj_mat * glm::mat4(view_mat);

        auto tpl = pimpl->skybox.skybox_material->GetLibrary().FindMaterialTemplate("SKYBOX", {0});
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, tpl->GetPipeline());
        const auto &sky_box_descriptor_set = pimpl->skybox.skybox_material->GetDescriptor(*tpl, frame_in_flight);
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            tpl->GetPipelineLayout(),
            2,
            { sky_box_descriptor_set },
            {}
        );
        // camera PV matrix is pushed directly.
        cb.pushConstants(
            tpl->GetPipelineLayout(),
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof (glm::mat4),
            { reinterpret_cast<const void *>(&pv) }
        );
        // Vertex info is embedded in the skybox.vert shader.
        cb.draw(36, 1, 0, 0);
    }

    vk::DescriptorSet SceneDataManager::GetLightDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->scene.scene_descriptor_sets.size());
        return pimpl->scene.scene_descriptor_sets[frame_in_flight];
    }

    vk::DescriptorSetLayout SceneDataManager::GetLightDescriptorSetLayout() const noexcept {
        return pimpl->scene.scene_descriptor_set_layout.get();
    }
} // namespace Engine::RenderSystemState
