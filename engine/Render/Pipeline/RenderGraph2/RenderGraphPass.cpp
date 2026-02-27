#include "RenderGraphPass.h"

#include "Render/RenderSystem.h"

namespace Engine {
    RenderGraphPassBuilder &RenderGraphPassBuilder::SetRasterizerPassFunction(
        std::function<void(GraphicsCommandBuffer &, const RenderGraph &)> fn
    ) noexcept {
        auto f = [system = &this->system,
                fn](vk::CommandBuffer cb, const RenderGraph & rg) {
            GraphicsCommandBuffer gcb{*system, cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(fn, std::ref(gcb), std::cref(rg));
        };
        pass.pass_function = f;
        pass.actual_type = RenderGraphPassAffinity::Graphics;
        pass.affinity = RenderGraphPassAffinity::Graphics;
        return *this;
    }

    RenderGraphPassBuilder &RenderGraphPassBuilder::SetComputePassFunction(
        std::function<void(ComputeCommandBuffer &, const RenderGraph &)> fn
    ) noexcept {
        auto f = [system = &this->system,
                fn](vk::CommandBuffer cb, const RenderGraph & rg) {
            ComputeCommandBuffer ccb{cb, system->GetFrameManager().GetFrameInFlight()};
            std::invoke(fn, std::ref(ccb), std::cref(rg));
        };
        pass.pass_function = f;
        pass.actual_type = RenderGraphPassAffinity::Compute;
        pass.affinity = RenderGraphPassAffinity::Compute;
        return *this;
    }
}
