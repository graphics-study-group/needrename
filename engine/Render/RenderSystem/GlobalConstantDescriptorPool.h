#ifndef RENDER_RENDERSYSTEM_GLOBALCONSTANTDESCRIPTORPOOL_INCLUDED
#define RENDER_RENDERSYSTEM_GLOBALCONSTANTDESCRIPTORPOOL_INCLUDED

#include "Render/ConstantData/PerCameraConstants.h"
#include "Render/ConstantData/PerSceneConstants.h"
#include "Render/Memory/IndexedBuffer.h"

namespace Engine {
    namespace RenderSystemState {
        /**
         * @brief A descriptor pool and related infrastructures for per-view global constants like camera
         * matrices.
         * 
         * As a rule of thumb, all resources that are frequently written by CPU and
         * read by GPU should be duplicated
         * for each frame in flight, so that when the resource is being
         * written by CPU, its GPU copy, which might be
         * being used for rendering, is left intact. Most of the
         * buffers and texture images are not frequently written
         * by CPU, and is therefore exempted from
         * duplication. However, uniform buffers do need duplication, and is
         * therefore managed by this
         * class.
         */
        class GlobalConstantDescriptorPool : public Engine::VkWrapperIndependent<vk::UniqueDescriptorPool> {
        protected:
            static constexpr uint32_t MAX_SET_SIZE = 128;
            static constexpr uint32_t MAX_CAMERAS = 16;
            static constexpr std::array<vk::DescriptorPoolSize, 5> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 128}
            };

            // Per camera constant descriptor set layout
            Engine::ConstantData::PerCameraConstantLayout m_per_camera_constant_layout{};
            // Per camera constant uniform buffers. Per camera constants contain only uniform buffers.
            std::vector<Engine::IndexedBuffer>
                m_per_camera_buffers{};
            // Per camera constant descriptor sets. These sets don't need explicit freeing.
            std::vector<vk::DescriptorSet> m_per_camera_descriptor_sets{};

            // Per scene constant descriptor set layout
            Engine::ConstantData::PerSceneConstantLayout m_per_scene_constant_layout{};
            // Per scene constant uniform buffers. Per scene constants contain only uniform buffers.
            std::vector<Engine::Buffer> m_per_scene_buffers{};
            // Per scene constant descriptor sets. These sets don't need explicit freeing.
            std::vector<vk::DescriptorSet> m_per_scene_descriptor_sets{};
            // Mapped memories for buffers.
            std::vector<std::byte *> m_per_scene_memories{};

            void CreateLayouts(std::shared_ptr<RenderSystem> system);
            void AllocateGlobalSets(std::shared_ptr<RenderSystem> system, uint32_t inflight_frame_count);

        public:
            ~GlobalConstantDescriptorPool() = default;

            void Create(std::weak_ptr<RenderSystem> system, uint32_t inflight_frame_count);
            auto GetPerCameraConstantLayout() const -> const decltype(m_per_camera_constant_layout) &;
            auto GetPerCameraConstantSet(uint32_t inflight) const -> const
                decltype(m_per_camera_descriptor_sets[inflight]) &;
            ConstantData::PerCameraStruct *GetPerCameraConstantMemory(uint32_t inflight, uint32_t camera_id) const;
            std::ptrdiff_t GetPerCameraDynamicOffset(uint32_t inflight, uint32_t camera_id) const;
            void FlushPerCameraConstantMemory(uint32_t inflight, uint32_t camera_id) const;

            auto GetPerSceneConstantLayout() const -> const decltype(m_per_scene_constant_layout) &;
            auto GetPerSceneConstantSet(uint32_t inflight) const -> const
                decltype(m_per_scene_descriptor_sets[inflight]) &;
            std::byte *GetPerSceneConstantMemory(uint32_t inflight) const;
            void FlushPerSceneConstantMemory(uint32_t inflight) const;
        };
    } // namespace RenderSystemState
} // namespace Engine

#endif // RENDER_RENDERSYSTEM_GLOBALCONSTANTDESCRIPTORPOOL_INCLUDED
