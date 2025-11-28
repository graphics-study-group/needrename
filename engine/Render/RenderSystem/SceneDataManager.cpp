#include "SceneDataManager.h"

#include "Render/Memory/IndexedBuffer.h"
#include "Render/DebugUtils.h"
#include <vulkan/vulkan.hpp>
#include <ext/matrix_transform.hpp>
#include <ext/matrix_clip_space.hpp>
#include <fstream>

#include "cmake_config.h"

namespace {
    std::vector <std::byte> read_spirv_file(std::filesystem::path path) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }

        std::vector<std::byte> buffer(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        file.close();

        return buffer;
    }
}

namespace Engine::RenderSystemState {
    struct SceneDataManager::impl {
        vk::Device device;
        vk::UniqueDescriptorPool scene_descriptor_pool;

        static constexpr std::array SCENE_DESCRIPTOR_POOL_SIZE {
            vk::DescriptorPoolSize{
                vk::DescriptorType::eUniformBuffer, 1 * FrameManager::FRAMES_IN_FLIGHT
            },
            vk::DescriptorPoolSize{
                // Shadowmaps + skybox cubemap
                vk::DescriptorType::eCombinedImageSampler, (MAX_SHADOW_CASTING_LIGHTS + 1) * FrameManager::FRAMES_IN_FLIGHT
            }
        };

        struct Light {
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

            LightUniformBuffer front_buffer;
            std::unique_ptr <IndexedBuffer> back_buffer;

            vk::UniqueDescriptorSetLayout descriptor_set_layout;
            std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> light_descriptors{};

            std::array <std::weak_ptr<void>, MAX_SHADOW_CASTING_LIGHTS/* + MAX_NON_SHADOW_CASTING_LIGHTS*/> bound_light_components;
            std::array <std::weak_ptr<RenderTargetTexture>, MAX_SHADOW_CASTING_LIGHTS> bound_shadow_maps;

            void Create(std::shared_ptr<RenderSystem> system, vk::DescriptorPool pool) {
                auto & allocator = system->GetAllocatorState();
                auto device = system->GetDevice();

                // Create decriptor set layout and allocate descriptors for lighting
                auto scene_descriptor_bindings = DESCRIPTOR_BINDINGS;
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
                descriptor_set_layout = device.createDescriptorSetLayoutUnique(dslci);

                std::vector <vk::DescriptorSetLayout> layouts(light_descriptors.size(), descriptor_set_layout.get());
                vk::DescriptorSetAllocateInfo dsai {pool, layouts};
                auto ret = device.allocateDescriptorSets(dsai);
                std::copy_n(ret.begin(), light_descriptors.size(), light_descriptors.begin());

#ifndef NDEBUG
                for (uint32_t i = 0; i < light_descriptors.size(); i++) {
                    DEBUG_SET_NAME_TEMPLATE(
                        device, light_descriptors[i], std::format("Desc Set - Scene FIF {}", i)
                    );
                }
#endif

                // Allocate the back buffer for lights.
                back_buffer = IndexedBuffer::CreateUnique(
                    allocator,
                    Buffer::BufferType::Uniform,
                    sizeof(pimpl->lights.front_buffer),
                    system->GetDeviceInterface().QueryLimit(DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment),
                    light_descriptors.size(),
                    "Scene Light Uniform Buffer"
                );
                assert(back_buffer);

                // Write out descriptors
                std::vector <vk::DescriptorBufferInfo> buffers(
                    light_descriptors.size(),
                    vk::DescriptorBufferInfo{
                        back_buffer->GetBuffer(),
                        0,
                        back_buffer->GetSliceSize()
                    }
                );
                std::vector <vk::WriteDescriptorSet> writes(
                    light_descriptors.size(),
                    vk::WriteDescriptorSet{
                        nullptr, 0, 0, vk::DescriptorType::eUniformBuffer, {}, {}, {}
                    }
                );
                for (uint32_t i = 0; i < light_descriptors.size(); i++) {
                    buffers[i].offset = back_buffer->GetSliceOffset(i);
                    writes[i].dstSet = light_descriptors[i];
                    writes[i].descriptorCount = 1;
                    writes[i].pBufferInfo = &buffers[i];
                }
                device.updateDescriptorSets(writes, {});
            }
        } lights;

        struct Skybox {
            static constexpr std::array DESCRIPTOR_BINDINGS {
                // cubemap binding
                vk::DescriptorSetLayoutBinding{
                    0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eAllGraphics
                }
            };

            static constexpr std::array PIPELINE_PUSH_CONSTANT_RANGE {
                vk::PushConstantRange{
                    vk::ShaderStageFlagBits::eAllGraphics, 0, sizeof(glm::mat4)
                }
            };

            std::array <vk::Sampler, 1> immutable_sampler;
            vk::UniqueDescriptorSetLayout descriptor_set_layout;
            vk::UniquePipelineLayout pipeline_layout;
            vk::UniquePipeline pipeline;
            std::array <vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> descriptors{};

            std::shared_ptr <Texture> skybox_texture;

            void CreatePipeline(std::shared_ptr <RenderSystem> system) {
                // Get shader modules
                // TODO: We maybe really need to use the asset system here.
                auto buffer = read_spirv_file(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders" / "skybox.vert.0.spv");
                vk::ShaderModuleCreateInfo smci{
                    vk::ShaderModuleCreateFlags{},
                    buffer.size(), reinterpret_cast<uint32_t *>(buffer.data())
                };
                // these two modules will be destroyed automatically when out of the scope.
                auto shdrv = system->GetDevice().createShaderModuleUnique(smci);
                buffer = read_spirv_file(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders" / "skybox.frag.0.spv");
                smci.codeSize = buffer.size();
                smci.pCode = reinterpret_cast<uint32_t *>(buffer.data());
                auto shdrf = system->GetDevice().createShaderModuleUnique(smci);

                // Create the pipeline
                std::array <vk::PipelineShaderStageCreateInfo, 2> pssci {
                    vk::PipelineShaderStageCreateInfo{
                        vk::PipelineShaderStageCreateFlags{},
                        vk::ShaderStageFlagBits::eVertex,
                        shdrv.get(),
                        "main"
                    },
                    vk::PipelineShaderStageCreateInfo{
                        vk::PipelineShaderStageCreateFlags{},
                        vk::ShaderStageFlagBits::eFragment,
                        shdrf.get(),
                        "main"
                    }
                };

                // vertex data are hardcoded
                vk::PipelineVertexInputStateCreateInfo pvisci {};
                vk::PipelineInputAssemblyStateCreateInfo piasci {
                    vk::PipelineInputAssemblyStateCreateFlags{},
                    vk::PrimitiveTopology::eTriangleList
                };
                // No tessellation
                vk::PipelineTessellationStateCreateInfo ptsci {};
                // Viewport is dynamically set
                vk::PipelineViewportStateCreateInfo pvsci {{}, 1, {}, 1, {}};

                vk::PipelineRasterizationStateCreateInfo prsci {
                    vk::PipelineRasterizationStateCreateFlags{},
                    false, false, vk::PolygonMode::eFill,
                    vk::CullModeFlagBits::eNone,
                    vk::FrontFace::eCounterClockwise,
                    false, 0.0f, 0.0f, 0.0f, 1.0f
                };
                // No multisampling
                vk::PipelineMultisampleStateCreateInfo pmssci {
                    vk::PipelineMultisampleStateCreateFlags{},
                    vk::SampleCountFlagBits::e1,
                    false
                };

                vk::PipelineDepthStencilStateCreateInfo pdssci {
                    vk::PipelineDepthStencilStateCreateFlags{},
                    true, false, vk::CompareOp::eLessOrEqual, false, false
                };

                std::array color_blend_attachment_states {
                    vk::PipelineColorBlendAttachmentState{
                        false,
                        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                    }
                };
                vk::PipelineColorBlendStateCreateInfo pcbsci {
                    vk::PipelineColorBlendStateCreateFlags{},
                    false, vk::LogicOp::eNoOp, color_blend_attachment_states
                };
                
                std::array <vk::DynamicState, 2> dynamic_states {
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor
                };
                vk::PipelineDynamicStateCreateInfo pdsci {
                    vk::PipelineDynamicStateCreateFlags{},
                    dynamic_states
                };

                // TODO: need a way to change these dynamically.
                std::array color_attachment_formats {
                    vk::Format::eR8G8B8A8Unorm
                };
                vk::PipelineRenderingCreateInfo prci {
                    0,
                    color_attachment_formats,
                    vk::Format::eD32Sfloat,
                    vk::Format::eUndefined
                };
                
                vk::GraphicsPipelineCreateInfo gpci {
                    vk::PipelineCreateFlags{},
                    pssci, &pvisci, &piasci, &ptsci,
                    &pvsci, &prsci, &pmssci, &pdssci,
                    &pcbsci, &pdsci, pipeline_layout.get(),
                    nullptr, 0
                };
                gpci.pNext = &prci;

                pipeline = system->GetDevice().createGraphicsPipelineUnique(nullptr, gpci).value;
                DEBUG_SET_NAME_TEMPLATE(
                    system->GetDevice(), pipeline.get(), "Skybox Pipeline"
                );
            }

            void Create(std::shared_ptr<RenderSystem> system, vk::DescriptorPool pool) {
                auto device = system->GetDevice();
                // Create descriptor set layout
                auto descriptor_bindings = DESCRIPTOR_BINDINGS;
                immutable_sampler[0] = system->GetSamplerManager().GetSampler(
                    ImageUtils::SamplerDesc{
                        .u_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge,
                        .v_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge,
                        .w_address = ImageUtils::SamplerDesc::AddressMode::ClampToEdge
                    }
                );
                descriptor_bindings[0].setImmutableSamplers(immutable_sampler);
                vk::DescriptorSetLayoutCreateInfo dslci {
                    vk::DescriptorSetLayoutCreateFlags{},
                    descriptor_bindings
                };
                descriptor_set_layout = device.createDescriptorSetLayoutUnique(dslci);

                // Create pipeline layout for skybox rendering pipeline
                vk::PipelineLayoutCreateInfo plci{
                    vk::PipelineLayoutCreateFlags{},
                    {descriptor_set_layout.get()},
                    PIPELINE_PUSH_CONSTANT_RANGE
                };
                pipeline_layout = device.createPipelineLayoutUnique(plci);

                // Allocate descriptors
                std::vector <vk::DescriptorSetLayout> layouts(descriptors.size(), descriptor_set_layout.get());
                vk::DescriptorSetAllocateInfo dsai {pool, layouts};
                auto ret = device.allocateDescriptorSets(dsai);
                std::copy_n(ret.begin(), descriptors.size(), descriptors.begin());

#ifndef NDEBUG
                for (uint32_t i = 0; i < descriptors.size(); i++) {
                    DEBUG_SET_NAME_TEMPLATE(
                        device, descriptors[i], std::format("Desc Set - Skybox {}", i)
                    );
                }
#endif
                CreatePipeline(system);
            }
        } skybox;

        void Create(std::shared_ptr<RenderSystem> system) {
            device = system->GetDevice();

            // Create dedicated descriptor pool
            vk::DescriptorPoolCreateInfo dpci {
                vk::DescriptorPoolCreateFlagBits{},
                lights.light_descriptors.size() + skybox.descriptors.size(),
                impl::SCENE_DESCRIPTOR_POOL_SIZE
            };
            scene_descriptor_pool = device.createDescriptorPoolUnique(dpci);
            DEBUG_SET_NAME_TEMPLATE(
                device, scene_descriptor_pool.get(), "Scene Descriptor Pool"
            );

            lights.Create(system, scene_descriptor_pool.get());
            skybox.Create(system, scene_descriptor_pool.get());
        }
    };
    SceneDataManager::SceneDataManager() noexcept : pimpl(std::make_unique<impl>()) {
    }
    SceneDataManager::~SceneDataManager() noexcept = default;

    void SceneDataManager::Create(std::shared_ptr<RenderSystem> system) {
        pimpl->Create(system);
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

    void SceneDataManager::SetSkyboxCubemap(std::shared_ptr<Texture> texture) noexcept {
        pimpl->skybox.skybox_texture = texture;
    }

    void SceneDataManager::UploadSceneData(uint32_t frame_in_flight) const noexcept {
        // TODO: use some dirty bit check to avoid memory write.
        std::memcpy(
            pimpl->lights.back_buffer->GetSlicePtr(frame_in_flight), 
            &pimpl->lights.front_buffer,
            sizeof (pimpl->lights.front_buffer)
        );
        pimpl->lights.back_buffer->FlushSlice(frame_in_flight);
        
        std::vector <vk::WriteDescriptorSet> descriptor_writes;
        descriptor_writes.reserve(2);
        std::vector <vk::DescriptorImageInfo> shadowmap_image_descriptor_writes{};

        const auto shadow_casting_light_count = pimpl->lights.front_buffer.shadow_casting.light_count;
        if (shadow_casting_light_count > 0) {
            shadowmap_image_descriptor_writes.resize(shadow_casting_light_count);

            for (size_t i = 0; i < shadow_casting_light_count; i++) {
                assert(!pimpl->lights.bound_shadow_maps[i].expired());
                shadowmap_image_descriptor_writes[i] = vk::DescriptorImageInfo{
                    nullptr, pimpl->lights.bound_shadow_maps[i].lock()->GetImageView(), vk::ImageLayout::eReadOnlyOptimal
                };
            }

            descriptor_writes.push_back(
                vk::WriteDescriptorSet{
                    pimpl->lights.light_descriptors[frame_in_flight], 1, 0,
                    vk::DescriptorType::eCombinedImageSampler,
                    shadowmap_image_descriptor_writes
                }
            );
        }

        std::array <vk::DescriptorImageInfo, 1> cubemap_image_descriptor_writes{};
        if (pimpl->skybox.skybox_texture) {
            auto p = pimpl->skybox.skybox_texture;
            cubemap_image_descriptor_writes[0] = vk::DescriptorImageInfo{
                nullptr, p->GetImageView(), vk::ImageLayout::eReadOnlyOptimal
            };
            descriptor_writes.push_back(vk::WriteDescriptorSet{
                pimpl->skybox.descriptors[frame_in_flight], 0, 0,
                vk::DescriptorType::eCombinedImageSampler,
                cubemap_image_descriptor_writes
            });
        }

        if (!descriptor_writes.empty()) {
            pimpl->device.updateDescriptorSets(
                {descriptor_writes},
                {}
            );
        }
    }

    void SceneDataManager::FetchLightData() noexcept {
        for (auto p : pimpl->lights.bound_light_components) {
            // ...
        }
    }

    void SceneDataManager::DrawSkybox(vk::CommandBuffer cb, uint32_t frame_in_flight, glm::mat4 pv) const {
        assert(frame_in_flight < pimpl->skybox.descriptors.size());
        if (!pimpl->skybox.skybox_texture)  return;

        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pimpl->skybox.pipeline.get());
        cb.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pimpl->skybox.pipeline_layout.get(),
            0,
            { pimpl->skybox.descriptors[frame_in_flight] },
            {}
        );
        // camera PV matrix is pushed directly.
        cb.pushConstants(
            pimpl->skybox.pipeline_layout.get(),
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof (glm::mat4),
            { reinterpret_cast<const void *>(&pv) }
        );
        // Vertex info is embedded in the skybox.vert shader.
        cb.draw(36, 1, 0, 0);
    }

    vk::DescriptorSet SceneDataManager::GetLightDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->lights.light_descriptors.size());
        return pimpl->lights.light_descriptors[frame_in_flight];
    }

    vk::DescriptorSetLayout SceneDataManager::GetLightDescriptorSetLayout() const noexcept {
        return pimpl->lights.descriptor_set_layout.get();
    }

    vk::DescriptorSet SceneDataManager::GetSkyboxDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->skybox.descriptors.size());
        return pimpl->skybox.descriptors[frame_in_flight];
    }

    vk::DescriptorSetLayout SceneDataManager::GetSkyboxDescriptorSetLayout() const noexcept {
        return pimpl->skybox.descriptor_set_layout.get();
    }

} // namespace Engine::RenderSystemState
