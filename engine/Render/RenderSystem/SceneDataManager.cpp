#include "SceneDataManager.h"

#include "Render/Resource/MaterialInstanceManager.h"
#include "Render/Resource/RenderResourceHandle.h"
#include "Render/Shadow/ShadowCascadeFit.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/DebugUtils.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Resource/DescriptorArena.h"
#include <Rhi/Buffer/IndexedBuffer.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <glm.hpp>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace {
    /// Smallest shadow distance accepted from a light component.
    constexpr float MIN_SHADOW_DISTANCE = 1e-3f;
} // namespace

namespace Engine::RenderSystemState {
    struct SceneDataManager::impl {
        vk::Device device{};

        struct Scene {
            /// One light's cascade count, padded to the std140 array stride of a
            /// scalar array so that it matches `interface.glsl`.
            struct alignas(16) CascadeCountSlot {
                uint32_t count{0};
                uint32_t padding[3]{0};
            };

            struct ShadowCastingLightUniformBuffer {
                /// Light source in world coordinate, could be position or direction.
                /// The last component is unused.
                alignas(16) glm::vec4 light_source[MAX_SHADOW_CASTING_LIGHTS];

                /// Light color in linear RGB multiplied by its strength.
                /// The last component is unused.
                alignas(16) glm::vec4 light_color[MAX_SHADOW_CASTING_LIGHTS];

                /// Light matrices used in shadow mapping.
                /// Precaculated projection * view matrices, flattened so that the
                /// cascade `c` of light `l` lives at `l * MAX_SHADOW_CASCADES + c`.
                alignas(16) glm::mat4 light_matrices[MAX_SHADOW_CASTING_LIGHTS * MAX_SHADOW_CASCADES];

                /// Cascade end distances along the camera's view axis. The
                /// component at `c + 1` holds the end of cascade `c`, so the last
                /// used component is the light's effective shadow distance.
                alignas(16) glm::vec4 cascade_splits[MAX_SHADOW_CASTING_LIGHTS];

                /// Number of valid cascades of each light; 0 means the light has no
                /// fitted cascades and is rendered without dynamic shadows.
                alignas(16) CascadeCountSlot light_cascade_count[MAX_SHADOW_CASTING_LIGHTS];
            };

            // The uniform block is consumed as std140 in `interface.glsl`, so the
            // host-side layout must agree with it exactly.
            static_assert(
                sizeof(ShadowCastingLightUniformBuffer) == 320,
                "ShadowCastingLightUniformBuffer must match the std140 layout of LightAttributeStruct"
            );
            static_assert(
                offsetof(ShadowCastingLightUniformBuffer, light_color) == 16
                    && offsetof(ShadowCastingLightUniformBuffer, light_matrices) == 32
                    && offsetof(ShadowCastingLightUniformBuffer, cascade_splits) == 288
                    && offsetof(ShadowCastingLightUniformBuffer, light_cascade_count) == 304,
                "ShadowCastingLightUniformBuffer member offsets must match std140"
            );

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

            static_assert(
                sizeof(LightUniformBuffer) == 848 && offsetof(LightUniformBuffer, shadow_casting) == 16
                    && offsetof(LightUniformBuffer, non_shadow_casting) == 336,
                "LightUniformBuffer must match the std140 layout of PerSceneUniform"
            );

            static constexpr std::array DESCRIPTOR_BINDINGS{
                // Uniform buffer for lights
                vk::DescriptorSetLayoutBinding{
                    0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
                },
                // Shadow maps binding
                vk::DescriptorSetLayoutBinding{
                    1,
                    vk::DescriptorType::eCombinedImageSampler,
                    MAX_SHADOW_CASTING_LIGHTS,
                    vk::ShaderStageFlagBits::eAllGraphics
                },
                // Model matrices storage buffer (readonly in graphics)
                vk::DescriptorSetLayoutBinding{
                    2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eAllGraphics
                }
            };

            // Light data
            LightUniformBuffer light_front_buffer{};
            std::unique_ptr<Rhi::IndexedBuffer> light_back_buffer{};
            std::array<std::weak_ptr<void>, MAX_SHADOW_CASTING_LIGHTS + MAX_NON_SHADOW_CASTING_LIGHTS>
                bound_light_components{};
            /// One shadow map array texture per budgeted slot, allocated once and
            /// never replaced. Render graphs only borrow these.
            std::array<std::unique_ptr<RenderTargetTexture>, MAX_SHADOW_CASTING_LIGHTS> shadow_maps{};
            std::shared_ptr<RenderTargetTexture> default_light_map;

            // Scene data
            Rhi::DescriptorSetLayoutRef scene_descriptor_set_layout{};
            vk::PipelineLayout scene_common_pipeline_layout{};
            std::array<vk::DescriptorSet, FrameManager::FRAMES_IN_FLIGHT> scene_descriptor_sets{};

            // The layout description, kept alive so the arena can allocate the
            // per-in-flight sets against the same cached layout object.
            std::vector<vk::DescriptorSetLayoutBinding> descriptor_bindings{};
            std::array<vk::Sampler, MAX_SHADOW_CASTING_LIGHTS> immutable_samplers{};

            // Model matrices storage buffer (owned here; for physics-driven objects)
            std::unique_ptr<Rhi::ComputeBuffer> model_matrices_buffer{};

            void Create(RenderSystem &system) {
                auto &allocator = system.GetAllocatorState();
                auto device = system.GetDevice();
                auto &arena = system.GetDeviceContext().GetDescriptorArena();

                // Create decriptor set layout
                {
                    descriptor_bindings.assign(DESCRIPTOR_BINDINGS.begin(), DESCRIPTOR_BINDINGS.end());
                    // Set up immutable samplers for shadow maps
                    std::fill(
                        immutable_samplers.begin(),
                        immutable_samplers.end(),
                        system.GetIRCache().GetSampler(
                            Rhi::SamplerDesc{
                                .min_filter = Rhi::SamplerDesc::FilterMode::Linear,
                                .max_filter = Rhi::SamplerDesc::FilterMode::Linear,
                                .mipmap_filter = Rhi::SamplerDesc::FilterMode::Point,
                                .u_address = Rhi::SamplerDesc::AddressMode::ClampToBorder_OpaqueWhite,
                                .v_address = Rhi::SamplerDesc::AddressMode::ClampToBorder_OpaqueWhite,
                                .w_address = Rhi::SamplerDesc::AddressMode::ClampToBorder_OpaqueWhite,
                                .comparator = Rhi::DSComparator::Less
                            }
                        )
                    );
                    descriptor_bindings[1].setImmutableSamplers(immutable_samplers);
                }
                vk::DescriptorSetLayoutCreateInfo dslci{vk::DescriptorSetLayoutCreateFlags{}, descriptor_bindings};
                scene_descriptor_set_layout = arena.ResolveLayout(dslci, "Scene Descriptor Set Layout");

                // Create common pipeline layout
                {
                    std::array pcr{RendererManager::GetPushConstantRange()};
                    vk::PipelineLayoutCreateInfo plci{
                        vk::PipelineLayoutCreateFlags{}, {scene_descriptor_set_layout.handle}, pcr
                    };
                    scene_common_pipeline_layout =
                        system.GetIRCache().GetPipelineLayout(plci, "Scene Common Pipeline Layout");
                }

                // The arena owns the pool; these sets live until it is destroyed.
                // Their contents change per frame, in place, so the caller carries
                // the obligation not to rewrite one while a command buffer that
                // binds it may still be executing — which the per-in-flight fence
                // FrameManager waits before resetting a slot's command buffer
                // already establishes.
                for (uint32_t i = 0; i < scene_descriptor_sets.size(); i++) {
                    scene_descriptor_sets[i] =
                        arena.AcquireRawSet(scene_descriptor_set_layout, std::format("Desc Set - Scene FIF {}", i));
                }
                // Allocate the back buffer for lights.
                light_back_buffer = Rhi::IndexedBuffer::CreateUnique(
                    allocator,
                    {Rhi::BufferTypeBits::HostAccessibleUniform},
                    sizeof(pimpl->scene.light_front_buffer),
                    system.GetDeviceInterface().QueryLimit(
                        Rhi::DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
                    ),
                    scene_descriptor_sets.size(),
                    "Scene Light Uniform Buffer"
                );
                assert(light_back_buffer);

                // Create the model matrices buffer
                model_matrices_buffer = Rhi::ComputeBuffer::CreateUnique(
                    allocator,
                    MAX_MODEL_MATRICES * sizeof(glm::mat4),
                    false, // No CPU access needed; content arrives via staging
                    false, // Not used as uniform buffer
                    false, // Not used as vertex buffer
                    false, // Not used as indirect draw buffer
                    "Scene Model Matrices Buffer"
                );
                assert(model_matrices_buffer);

                // Initialize it to identity matrices
                {
                    std::vector<glm::mat4> identity(MAX_MODEL_MATRICES, glm::mat4{1.0f});
                    auto &submission_helper = system.GetFrameManager().GetSubmissionHelper();
                    submission_helper.EnqueueBufferSubmission(
                        *model_matrices_buffer, std::as_bytes(std::span{identity})
                    );
                    submission_helper.ExecuteSubmissionImmediately();
                }

                // Prepare default depth map. It is an array texture with the same
                // layer count and format as a real shadow map, so that every view
                // bound to the shadow map binding has the same type.
                default_light_map = RenderTargetTexture::CreateUnique(
                    system.GetDeviceContext(),
                    RenderTargetTexture::RenderTargetTextureDesc{
                        .dimensions = 2,
                        .width = 16, .height = 16, .depth = 1,
                        .mipmap_levels = 1, .array_layers = MAX_SHADOW_CASCADES,
                        .format = RenderTargetTexture::RTTFormat::D32SFLOAT,
                        .multisample = 1,
                        .is_cube_map = false
                    },
                    Rhi::Texture::SamplerDesc{

                    },
                    "Default shadowmap"
                );
                system.GetFrameManager().GetSubmissionHelper().EnqueueTextureClear(*default_light_map, 1.0f);

                // Allocate the shadow map storage up front: every slot the budget
                // allows gets its array texture now, so that whether a shadow map
                // exists depends only on the compile-time budget and never on the
                // scene's lights, the command queue, or when a graph is built.
                for (uint32_t slot = 0; slot < shadow_maps.size(); slot++) {
                    shadow_maps[slot] = RenderTargetTexture::CreateUnique(
                        system.GetDeviceContext(),
                        RenderTargetTexture::RenderTargetTextureDesc{
                            .dimensions = 2,
                            .width = SHADOWMAP_SIZE,
                            .height = SHADOWMAP_SIZE,
                            .depth = 1,
                            .mipmap_levels = 1,
                            .array_layers = MAX_SHADOW_CASCADES,
                            .format = RenderTargetTexture::RTTFormat::D32SFLOAT,
                            .multisample = 1,
                            .is_cube_map = false
                        },
                        Rhi::Texture::SamplerDesc{},
                        "Shadow Map " + std::to_string(slot)
                    );
                    assert(shadow_maps[slot] && "Shadow map storage must exist for every budgeted slot.");
                }

                // Write out descriptors
                std::vector<vk::DescriptorBufferInfo> buffers(
                    scene_descriptor_sets.size(),
                    vk::DescriptorBufferInfo{light_back_buffer->GetBuffer(), 0, light_back_buffer->GetSliceSize()}
                );
                std::vector<vk::WriteDescriptorSet> writes(
                    scene_descriptor_sets.size(),
                    vk::WriteDescriptorSet{nullptr, 0, 0, vk::DescriptorType::eUniformBuffer, {}, {}, {}}
                );
                for (uint32_t i = 0; i < scene_descriptor_sets.size(); i++) {
                    buffers[i].offset = light_back_buffer->GetSliceOffset(i);
                    writes[i].dstSet = scene_descriptor_sets[i];
                    writes[i].descriptorCount = 1;
                    writes[i].pBufferInfo = &buffers[i];
                }
                device.updateDescriptorSets(writes, {});

                // Write model matrices buffer descriptor (binding 2)
                {
                    std::vector<vk::DescriptorBufferInfo> model_mat_buffers(
                        scene_descriptor_sets.size(),
                        vk::DescriptorBufferInfo{
                            model_matrices_buffer->GetBuffer(), 0, model_matrices_buffer->GetSize()
                        }
                    );
                    std::vector<vk::WriteDescriptorSet> mm_writes(
                        scene_descriptor_sets.size(),
                        vk::WriteDescriptorSet{nullptr, 2, 0, vk::DescriptorType::eStorageBuffer, {}, {}, {}}
                    );
                    for (uint32_t i = 0; i < scene_descriptor_sets.size(); i++) {
                        mm_writes[i].dstSet = scene_descriptor_sets[i];
                        mm_writes[i].descriptorCount = 1;
                        mm_writes[i].pBufferInfo = &model_mat_buffers[i];
                    }
                    device.updateDescriptorSets(mm_writes, {});
                }
            }
        } scene{};

        struct Skybox {
            MaterialInstanceHandle skybox_material{};
        } skybox{};

        void Create(RenderSystem &system) {
            device = system.GetDevice();
            scene.Create(system);
        }
    };
    SceneDataManager::SceneDataManager(RenderSystem &system) noexcept :
        m_system(system), pimpl(std::make_unique<impl>()) {
    }

    SceneDataManager::~SceneDataManager() noexcept = default;

    void SceneDataManager::Create() {
        pimpl->Create(m_system);
    }

    void SceneDataManager::SetLightDirectional(uint32_t index, glm::vec3 direction, glm::vec3 intensity) noexcept {
        if (index >= MAX_SHADOW_CASTING_LIGHTS) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Shadow casting light index %u is out of range (%u supported); the light is ignored.",
                index,
                MAX_SHADOW_CASTING_LIGHTS
            );
            return;
        }
        pimpl->scene.light_front_buffer.shadow_casting.light_source[index] = glm::vec4(direction, 0.0f);
        pimpl->scene.light_front_buffer.shadow_casting.light_color[index] = glm::vec4(intensity, 0.0f);
    }

    void SceneDataManager::SetLightDirectional(
        uint32_t index,
        glm::vec3 direction,
        glm::vec3 intensity,
        const glm::mat4 &camera_view,
        const glm::mat4 &camera_projection,
        float camera_near,
        float camera_far,
        float shadow_distance,
        uint32_t cascade_count
    ) noexcept {
        if (index >= MAX_SHADOW_CASTING_LIGHTS) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Shadow casting light index %u is out of range (%u supported); the light is ignored.",
                index,
                MAX_SHADOW_CASTING_LIGHTS
            );
            return;
        }
        SetLightDirectional(index, direction, intensity);

        // Clamp the configuration before it reaches the fit, so that a
        // hand-edited or stale serialized value cannot produce degenerate cascades.
        const float distance = (std::isfinite(shadow_distance) && shadow_distance > MIN_SHADOW_DISTANCE)
                                   ? shadow_distance
                                   : MIN_SHADOW_DISTANCE;
        const uint32_t cascades = std::clamp(cascade_count, 1u, MAX_SHADOW_CASCADES);

        const ShadowCascadeFit fitted = FitShadowCascades(
            ShadowCascadeFitParams{
                .direction = direction,
                .camera_view = camera_view,
                .camera_projection = camera_projection,
                .camera_near = camera_near,
                .camera_far = camera_far,
                .shadow_distance = distance,
                .cascade_count = cascades,
            }
        );
        if (!fitted.valid) {
            // Degenerate direction or slice: keep the previous frame's cascades
            // rather than uploading matrices that could not be inverted.
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Shadow cascade fit for light %u was rejected; the previous frame's cascades are kept.",
                index
            );
            return;
        }

        auto &light = pimpl->scene.light_front_buffer.shadow_casting;
        for (uint32_t cascade = 0; cascade < MAX_SHADOW_CASCADES; cascade++) {
            light.light_matrices[index * MAX_SHADOW_CASCADES + cascade] = fitted.matrices[cascade];
        }
        light.cascade_splits[index] = fitted.splits;
        light.light_cascade_count[index].count = fitted.count;
    }

    uint32_t SceneDataManager::GetLightCascadeCount(uint32_t index) const noexcept {
        if (index >= MAX_SHADOW_CASTING_LIGHTS) return 0;
        return pimpl->scene.light_front_buffer.shadow_casting.light_cascade_count[index].count;
    }

    glm::mat4 SceneDataManager::GetLightCascadeMatrix(uint32_t index, uint32_t cascade) const noexcept {
        if (index >= MAX_SHADOW_CASTING_LIGHTS || cascade >= MAX_SHADOW_CASCADES) return glm::mat4{1.0f};
        return pimpl->scene.light_front_buffer.shadow_casting.light_matrices[index * MAX_SHADOW_CASCADES + cascade];
    }

    void SceneDataManager::SetLightPoint(uint32_t, glm::vec3, glm::vec3, float) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::SetLightCone(uint32_t, glm::vec3, glm::vec3, float, float) noexcept {
        assert(!"Unimplemented");
    }

    void SceneDataManager::SetLightDirectionalNonShadowCasting(
        uint32_t index, glm::vec3 direction, glm::vec3 intensity
    ) noexcept {
        assert(index < MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.non_shadow_casting.light_source[index] =
            glm::vec4(direction, 0.0f); // w = 0.0f for directional light
        pimpl->scene.light_front_buffer.non_shadow_casting.light_color[index] = glm::vec4(intensity, 0.0f);
    }

    void SceneDataManager::SetLightPointNonShadowCasting(
        uint32_t index, glm::vec3 position, glm::vec3 intensity
    ) noexcept {
        assert(index < MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.non_shadow_casting.light_source[index] =
            glm::vec4(position, 1.0f); // w = 1.0f for point light
        pimpl->scene.light_front_buffer.non_shadow_casting.light_color[index] = glm::vec4(intensity, 0.0f);
    }

    RenderTargetTexture &SceneDataManager::GetShadowMapTexture(uint32_t index) noexcept {
        assert(index < MAX_SHADOW_CASTING_LIGHTS && "Only budgeted shadow map slots have storage.");
        auto &shadow_map = pimpl->scene.shadow_maps[index];
        assert(shadow_map && "Shadow map storage is allocated when the manager is created.");
        return *shadow_map;
    }

    void SceneDataManager::SetLight(uint32_t index, std::shared_ptr<void> light) noexcept {
        if (index >= MAX_SHADOW_CASTING_LIGHTS) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Shadow casting light index %u is out of range (%u supported); the binding is ignored.",
                index,
                MAX_SHADOW_CASTING_LIGHTS
            );
            return;
        }
        pimpl->scene.bound_light_components[index] = light;
    }

    void SceneDataManager::SetLightNonShadowCasting(uint32_t index, std::shared_ptr<void> light) noexcept {
        assert(index < MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.bound_light_components[MAX_SHADOW_CASTING_LIGHTS + index] = light;
    }

    void SceneDataManager::SetLightCount(uint32_t count) noexcept {
        if (count > MAX_SHADOW_CASTING_LIGHTS) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "%u shadow casting lights were reported, but only %u are supported; the extra ones are ignored.",
                count,
                MAX_SHADOW_CASTING_LIGHTS
            );
            count = MAX_SHADOW_CASTING_LIGHTS;
        }
        pimpl->scene.light_front_buffer.shadow_casting_light_count = count;
    }

    uint32_t SceneDataManager::GetNumShadowCastingLights() const noexcept {
        return pimpl->scene.light_front_buffer.shadow_casting_light_count;
    }

    void SceneDataManager::SetLightCountNonShadowCasting(uint32_t count) noexcept {
        // Exactly the maximum is valid: the last light still has a slot.
        assert(count <= MAX_NON_SHADOW_CASTING_LIGHTS);
        pimpl->scene.light_front_buffer.non_shadow_casting_light_count = count;
    }

    void SceneDataManager::SetSkyboxMaterial(MaterialInstanceHandle material) noexcept {
        auto *material_manager = m_system.GetRenderResourceManager<MaterialInstanceManager>();
        if (pimpl->skybox.skybox_material.IsValid()) {
            material_manager->Release(pimpl->skybox.skybox_material);
        }
        pimpl->skybox.skybox_material = material;
    }

    void SceneDataManager::UploadSceneData(uint32_t frame_in_flight) const noexcept {
        // TODO: use some dirty bit check to avoid memory write.
        std::memcpy(
            pimpl->scene.light_back_buffer->GetSlicePtr(frame_in_flight),
            &pimpl->scene.light_front_buffer,
            sizeof(pimpl->scene.light_front_buffer)
        );
        pimpl->scene.light_back_buffer->FlushSlice(frame_in_flight);

        std::vector<vk::WriteDescriptorSet> descriptor_writes;
        descriptor_writes.reserve(2);
        std::vector<vk::DescriptorImageInfo> shadowmap_image_descriptor_writes{
            MAX_SHADOW_CASTING_LIGHTS, vk::DescriptorImageInfo{}
        };

        const auto shadow_casting_light_count = pimpl->scene.light_front_buffer.shadow_casting_light_count;
        for (size_t i = 0; i < MAX_SHADOW_CASTING_LIGHTS; i++) {
            // The manager owns every slot, so a used slot always has storage. The
            // remaining slots keep sampling the fallback map; the shaders never
            // read a slot past the reported light count.
            const vk::ImageView image_view = i < shadow_casting_light_count
                                                 ? pimpl->scene.shadow_maps[i]->GetImageView()
                                                 : pimpl->scene.default_light_map->GetImageView();
            shadowmap_image_descriptor_writes[i] =
                vk::DescriptorImageInfo{nullptr, image_view, vk::ImageLayout::eReadOnlyOptimal};
        }

        descriptor_writes.push_back(
            vk::WriteDescriptorSet{
                pimpl->scene.scene_descriptor_sets[frame_in_flight],
                1,
                0,
                vk::DescriptorType::eCombinedImageSampler,
                shadowmap_image_descriptor_writes
            }
        );

        // Update model matrices buffer descriptor (binding 2).
        // Rewritten every frame so that the handle bound for the frame is the
        // buffer's current storage, even after it grew in place.
        {
            const auto &current_buffer = *pimpl->scene.model_matrices_buffer;
            vk::DescriptorBufferInfo mm_buffer_info{current_buffer.GetBuffer(), 0, current_buffer.GetSize()};
            vk::WriteDescriptorSet mm_write{
                pimpl->scene.scene_descriptor_sets[frame_in_flight],
                2,
                0,
                1,
                vk::DescriptorType::eStorageBuffer,
                nullptr,
                &mm_buffer_info,
                nullptr
            };
            descriptor_writes.push_back(mm_write);
        }

        if (!descriptor_writes.empty()) {
            pimpl->device.updateDescriptorSets({descriptor_writes}, {});
        }
    }

    void SceneDataManager::FetchLightData() noexcept {
        for (auto p : pimpl->scene.bound_light_components) {
            // ...
        }
    }

    void SceneDataManager::DrawSkybox(
        CommandBuffer &cb, uint32_t frame_in_flight, glm::mat4 pv_mat, const vk::Extent2D &extent
    ) const {
        if (!pimpl->skybox.skybox_material.IsValid()) return;

        auto *material =
            m_system.GetRenderResourceManager<MaterialInstanceManager>()->Resolve(pimpl->skybox.skybox_material);
        if (!material) return;

        vk::Rect2D scissor{{0, 0}, extent};
        cb.SetupViewport(extent.width, extent.height, scissor);

        auto tpl = material->GetLibrary().FindMaterialTemplate("SKYBOX", {{0}, cb.GetRenderingInfo()});
        if (!tpl) return;
        auto material_binding = material->UpdateGPUInfo(*tpl, frame_in_flight);

        auto rcb = cb.GetCommandBuffer();
        rcb.bindPipeline(vk::PipelineBindPoint::eGraphics, tpl->GetPipeline());
        rcb.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            tpl->GetPipelineLayout(),
            2,
            {material_binding.set},
            material_binding.dynamic_offsets
        );
        // camera PV matrix is pushed directly.
        rcb.pushConstants(
            tpl->GetPipelineLayout(),
            vk::ShaderStageFlagBits::eAllGraphics,
            0,
            sizeof(glm::mat4),
            reinterpret_cast<const void *>(&pv_mat)
        );
        // Vertex info is embedded in the skybox.vert shader.
        rcb.draw(36, 1, 0, 0);
    }

    vk::DescriptorSet SceneDataManager::GetLightDescriptorSet(uint32_t frame_in_flight) const noexcept {
        assert(frame_in_flight < pimpl->scene.scene_descriptor_sets.size());
        return pimpl->scene.scene_descriptor_sets[frame_in_flight];
    }

    vk::DescriptorSetLayout SceneDataManager::GetLightDescriptorSetLayout() const noexcept {
        return pimpl->scene.scene_descriptor_set_layout.handle;
    }
    vk::PipelineLayout SceneDataManager::GetCommonPipelineLayout() const noexcept {
        return pimpl->scene.scene_common_pipeline_layout;
    }

    void SceneDataManager::EnsureModelMatricesCapacity(uint32_t element_count) {
        const size_t required = static_cast<size_t>(std::max(1u, element_count)) * sizeof(glm::mat4);
        pimpl->scene.model_matrices_buffer->EnsureCapacity(m_system.GetAllocatorState(), required);
    }

    Rhi::ComputeBuffer &SceneDataManager::GetModelMatricesBuffer() noexcept {
        return *pimpl->scene.model_matrices_buffer;
    }
} // namespace Engine::RenderSystemState
