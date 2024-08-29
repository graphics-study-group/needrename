#ifndef RENDER_RENDERSYSTEM_INSTANCE_INCLUDED
#define RENDER_RENDERSYSTEM_INSTANCE_INCLUDED

#include "Render/VkWrapper.tcc"

namespace Engine {
    namespace RenderSystemState {
        class Instance : public Engine::VkWrapperIndependent <vk::UniqueInstance> {
        public:
            Instance() = default;
            void Create(const char * instance_name, const char * engine_name);
        protected:
            static bool CheckValidationLayer();
            static constexpr const char * VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";
        };
    }
}

#endif // RENDER_RENDERSYSTEM_INSTANCE_INCLUDED
