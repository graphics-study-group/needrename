#ifndef RENDER_RENDERSYSTEM_DESCRIPTORPOOL_INCLUDED
#define RENDER_RENDERSYSTEM_DESCRIPTORPOOL_INCLUDED

#include "Render/ConstantData/PerCameraConstants.h"
#include "Render/ConstantData/PerSceneConstants.h"

namespace Engine {
    namespace RenderSystemState {
        /// @brief A descriptor pool and related infrastructures for per-view global constants like camera matrices
        class GlobalConstantDescriptorPool : public Engine::VkWrapperIndependent <vk::UniqueDescriptorPool> {
        protected:
            static constexpr uint32_t MAX_SET_SIZE = 64;
            static constexpr std::array <vk::DescriptorPoolSize, 2> DESCRIPTOR_POOL_SIZES = {
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 128},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 128}
            };

            // Per camera constant descriptor set layout
            Engine::ConstantData::PerCameraConstantLayout m_per_camera_constant_layout{};
            // Per camera constant uniform buffers. Per camera constants contain only uniform buffers.
            std::vector <Engine::Buffer> m_per_camera_buffers {};
            // Per camera constant descriptor sets. These sets don't need explicit freeing.
            std::vector <vk::DescriptorSet> m_per_camera_descriptor_sets {};
            // Mapped memories for buffers.
            std::vector <std::byte *> m_per_camera_memories {};

            // Per scene constant descriptor set layout
            Engine::ConstantData::PerSceneConstantLayout m_per_scene_constant_layout{};
            // Per scene constant uniform buffers. Per camera constants contain only uniform buffers.
            std::vector <Engine::Buffer> m_per_scene_buffers {};
            // Per scene constant descriptor sets. These sets don't need explicit freeing.
            std::vector <vk::DescriptorSet> m_per_scene_descriptor_sets {};
            // Mapped memories for buffers.
            std::vector <std::byte *> m_per_scene_memories {};

            // TODO: Per scene constant descriptors (e.g. ambient, sunlight, etc.)

            void CreateLayouts(std::shared_ptr <RenderSystem> system);
            void AllocateGlobalSets(std::shared_ptr <RenderSystem> system, uint32_t inflight_frame_count);

        public:
            ~GlobalConstantDescriptorPool() = default;
            
            void Create(std::weak_ptr <RenderSystem> system, uint32_t inflight_frame_count);
            auto GetPerCameraConstantLayout() const -> const decltype(m_per_camera_constant_layout) &;
            auto GetPerCameraConstantSet(uint32_t inflight) const -> const decltype(m_per_camera_descriptor_sets[inflight]) &;
            std::byte * GetPerCameraConstantMemory(uint32_t inflight) const;
            void FlushPerCameraConstantMemory(uint32_t inflight) const;

            auto GetPerSceneConstantLayout() const -> const decltype(m_per_scene_constant_layout) &;
            auto GetPerSceneConstantSet(uint32_t inflight) const -> const decltype(m_per_scene_descriptor_sets[inflight]) &;
            std::byte * GetPerSceneConstantMemory(uint32_t inflight) const;
            void FlushPerSceneConstantMemory(uint32_t inflight) const;
        };
    }
}

#endif // RENDER_RENDERSYSTEM_DESCRIPTORPOOL_INCLUDED
