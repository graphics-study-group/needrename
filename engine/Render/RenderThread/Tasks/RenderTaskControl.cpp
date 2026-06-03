#include "RenderTaskControl.h"

#include "Render/RenderThread/RenderThreadState.h"
#include "Render/Pipeline/RenderGraph2/RenderGraphBuilder2.h"

namespace Engine::RenderTasks {

    void RenderTaskInitialize::do_execute(RenderThreadState & rts) {
        assert(!rts.render_system);

        rts.render_system = std::make_unique<RenderSystem>(this->window);
        rts.render_system->Create();

        p.set_value();
    }

    void RenderTaskRenderOneFrameWithActiveGraph::do_execute(RenderThreadState & rts) {
        assert(rts.render_system);
        if (rts.active_render_graph) {
            rts.state.store(RenderThreadState::State::WAITING_FOR_GPU, std::memory_order::release);
            rts.state_cv.notify_all();

            rts.render_system->GetFrameManager().WaitForAvailableFrame();

            rts.state.store(RenderThreadState::State::RUNNING, std::memory_order::release);
            rts.state_cv.notify_all();

            auto fif = rts.render_system->GetFrameManager().GetFrameInFlight();
            rts.render_system->GetCameraManager().FetchCameraData();
            rts.render_system->GetCameraManager().UploadCameraData(fif);
            rts.render_system->GetSceneDataManager().FetchLightData();
            rts.render_system->GetSceneDataManager().UploadSceneData(fif);

            rts.active_render_graph->Execute(*rts.render_system);

            auto tx = rts.active_render_graph->GetInternalTextureResource(presenting_texture_);
            assert(tx);
            rts.render_system->CompleteFrame(
                *tx,
                tx->GetTextureDescription().width,
                tx->GetTextureDescription().height
            );
        }
        p.set_value();
    }

    void RenderTaskBuildActiveRenderGraph::do_execute(RenderThreadState & rts) {
        assert(rts.render_system);
        rts.active_render_graph = builder_->BuildRenderGraph(*rts.render_system);
        p.set_value();
    }
}
