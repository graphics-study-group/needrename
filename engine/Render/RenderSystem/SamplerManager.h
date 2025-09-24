#ifndef RENDER_RENDERSYSTEM_SAMPLERMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_SAMPLERMANAGER_INCLUDED

#include <memory>
#include "Render/ImageUtils.h"

namespace vk {
    class Sampler;
}

namespace Engine {
    class RenderSystem;

    namespace RenderSystemState {
        class SamplerManager {
            RenderSystem & m_system;
            struct impl;
            std::unique_ptr <impl> pimpl;
        public:

            SamplerManager(RenderSystem & system);
            ~SamplerManager();

            /**
             * @brief Get a sampler from the a sampler description.
             * If the sampler matching the current description does not exist,
             * it will be created. Samplers created and managed by this class
             * will not be destroyed until the deconstruction of this class.
             */
            vk::Sampler GetSampler(ImageUtils::SamplerDesc desc);
        };
    }
}

#endif // RENDER_RENDERSYSTEM_SAMPLERMANAGER_INCLUDED
