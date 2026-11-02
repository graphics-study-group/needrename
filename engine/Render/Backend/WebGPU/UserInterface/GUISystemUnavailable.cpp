#include "Render/UserInterface/GUISystem.h"

#include <stdexcept>

namespace Engine {
    // The shape that has no editor UI.
    //
    // GUISystem drives Dear ImGui through its backend, and the WebGPU backend of ImGui is written
    // against an older wgpu-native whose shader descriptor still had a `source` field. It does not
    // compile against the version this build uses, so there is no UI to drive.
    //
    // Everything that reports state answers as an idle UI would: no capture, nothing to prepare,
    // nothing to draw. `Create` and `CreateBackend` throw, because a caller that asks for a backend
    // has taken a path this build does not have, and a window that opens with no UI is worse than a
    // message that says why.

    // The impl is defined here so the unique_ptr has a complete type to destroy.
    struct GUISystem::impl {};

    GUISystem::GUISystem() : pimpl(std::make_unique<impl>()) {
    }

    GUISystem::~GUISystem() = default;

    bool GUISystem::WantCaptureMouse() const {
        return false;
    }

    bool GUISystem::WantCaptureKeyboard() const {
        return false;
    }

    void GUISystem::ProcessEvent(SDL_Event *) const {
        // No UI, so no event is for it.
    }

    void GUISystem::PrepareGUI() const {
    }

    void GUISystem::DrawGUI(Rhi::CommandEncoder &) const noexcept {
    }

void GUISystem::Create(SDL_Window *) {
        // No UI to create. An editor needs ImGui to draw its panels; an example does not,
        // and refusing here would stop the engine before it ever draws a frame.
    }

    void GUISystem::CreateBackend(RenderSystem &, Rhi::ImageFormat, uint8_t) {
        // Nothing to prepare without a context to draw with.
    }

    ImGuiContext *GUISystem::GetCurrentContext() const {
        return nullptr;
    }

    void GUISystem::ResetColorAttachmentFormat(Rhi::ImageFormat, uint8_t) noexcept {
    }
} // namespace Engine

