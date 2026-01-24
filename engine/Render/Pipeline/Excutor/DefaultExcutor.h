#ifndef RENDER_PIPELINE_EXCUTOR_DEFAULTEXCUTOR_H
#define RENDER_PIPELINE_EXCUTOR_DEFAULTEXCUTOR_H

#include "RenderExcutor.h"
#include <memory>

namespace Engine {
    class RenderGraph;

    class DefaultExcutor : public RenderExcutor {
        static const uint32_t SHADOWMAP_WIDTH = 2048;
        static const uint32_t SHADOWMAP_HEIGHT = 2048;

    public:
        DefaultExcutor(RenderSystem &system, uint32_t target_width, uint32_t target_height);
        virtual ~DefaultExcutor() = default;

        virtual void Excute() override;
        virtual std::shared_ptr<RenderTargetTexture> GetColorRenderTarget() override;

    protected:
        std::shared_ptr<RenderTargetTexture> m_color_target{};
        std::shared_ptr<RenderTargetTexture> m_depth_target{};

        std::shared_ptr<RenderGraph> m_render_graph{};
    };
} // namespace Engine

#endif // RENDER_PIPELINE_EXCUTOR_DEFAULTEXCUTOR_H
