#ifndef RENDER_PIPELINE_EXCUTOR_RENDEREXCUTOR_H
#define RENDER_PIPELINE_EXCUTOR_RENDEREXCUTOR_H

#include <memory>

namespace Engine {
    class RenderSystem;
    class RenderTargetTexture;

    class RenderExcutor {
    public:
        RenderExcutor(RenderSystem &system);
        virtual ~RenderExcutor() = default;

        virtual void Excute() = 0;
        virtual std::shared_ptr<RenderTargetTexture> GetColorRenderTarget() = 0;
        
    protected:
        RenderSystem &m_system;
    };
} // namespace Engine

#endif // RENDER_PIPELINE_EXCUTOR_RENDEREXCUTOR_H
