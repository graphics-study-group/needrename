#include "Render/UserInterface/GUISystem.h"

#include "Render/Pipeline/CommandBuffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/IPresentProvider.h"
#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Command/CommandEncoder.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Texture/Texture.h"

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_wgpu.h>
#include <webgpu/webgpu.h>

#include <cassert>
#include <memory>

namespace Engine {
    // Dear ImGui through its WebGPU backend.
    //
    // The shape mirrors the Vulkan one, with one difference the seam has to offer: ImGui's Vulkan
    // backend records into a `VkCommandBuffer`, which the encoder already names, while its WebGPU
    // backend records into a `WGPURenderPassEncoder`, which exists only while a pass is open. That
    // is what `CommandEncoder::GetNativeRenderPass` is for.
    //
    // The rest is the same contract: `Create` builds the context once per window, and
    // `CreateBackend` runs again whenever the attachment format changes - a resize can pick a
    // different surface format, and ImGui's pipelines are built for one.

    struct GUISystem::impl {
        ImGuiContext *m_context{nullptr};
        WGPUTextureFormat m_color_format{WGPUTextureFormat_Undefined};
        uint8_t m_samples{1};
        bool m_backend_initialized{false};

        void CleanUp() {
            if (m_context != nullptr) {
                ImGui_ImplSDL3_Shutdown();
                if (ImGui::GetIO().BackendRendererUserData != nullptr) {
                    ImGui_ImplWGPU_Shutdown();
                }
                ImGui::DestroyContext(m_context);
            }
            m_context = nullptr;
            m_backend_initialized = false;
        }
    };

    GUISystem::GUISystem() : pimpl(std::make_unique<impl>()) {
    }

    GUISystem::~GUISystem() {
        pimpl->CleanUp();
    }

    bool GUISystem::WantCaptureMouse() const {
        return pimpl->m_context != nullptr && ImGui::GetIO().WantCaptureMouse;
    }

    bool GUISystem::WantCaptureKeyboard() const {
        return pimpl->m_context != nullptr && ImGui::GetIO().WantCaptureKeyboard;
    }

    void GUISystem::ProcessEvent(SDL_Event *event) const {
        if (pimpl->m_context != nullptr && event != nullptr) {
            ImGui_ImplSDL3_ProcessEvent(event);
        }
    }

    void GUISystem::PrepareGUI() const {
        if (pimpl->m_context == nullptr) {
            return;
        }
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplWGPU_NewFrame();
        ImGui::NewFrame();
    }

    void GUISystem::DrawGUI(Rhi::CommandEncoder &encoder) const noexcept {
        if (pimpl->m_context == nullptr || !pimpl->m_backend_initialized) {
            return;
        }
        // Ended here rather than by the caller, and unconditionally: ImGui asserts at the next
        // `NewFrame` if a frame was prepared and never ended, which is exactly what a frame whose
        // pass was not open would do. The draw data is only handed to the backend when there is a
        // pass to record into.
        ImGui::Render();
        auto *pass = static_cast<WGPURenderPassEncoder>(encoder.GetNativeRenderPass());
        if (pass == nullptr) {
            return;
        }
        ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);
    }

    void GUISystem::DrawGUI(
        const AttachmentUtils::AttachmentDescription &attachment, Rhi::Extent2D extent, CommandBuffer &cb
    ) const {
        // Opens the pass here, as the Vulkan shape does, and draws into the one the command buffer
        // just opened - which is why the pass object has to be reachable from the encoder.
        cb.BeginRendering(attachment, {}, extent, "GUI rendering pass");
        DrawGUI(cb.GetEncoder());
        cb.EndRendering();
    }

    void GUISystem::Create(SDL_Window *window) {
        assert(pimpl->m_context == nullptr && "Re-creating GUI system.");
        pimpl->m_context = ImGui::CreateContext();
        assert(pimpl->m_context != nullptr && "Failed to create ImGui context.");
        ImGui_ImplSDL3_InitForOther(window);
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    void GUISystem::CreateBackend(RenderSystem &render_system, Rhi::ImageFormat color_format, uint8_t samples) {
        if (pimpl->m_context == nullptr) {
            return;
        }
        if (pimpl->m_backend_initialized) {
            ImGui_ImplWGPU_Shutdown();
            pimpl->m_backend_initialized = false;
        }

        pimpl->m_color_format = Rhi::ToWgpuTextureFormat(
            color_format == Rhi::ImageFormat::UNDEFINED ? render_system.GetPresentProvider().GetColorFormat()
                                                        : color_format
        );
        pimpl->m_samples = samples == 0 ? 1 : samples;

        ImGui_ImplWGPU_InitInfo info{};
        info.Device = Rhi::ToWgpuDevice(render_system.GetDeviceInterface().GetDevice());
        info.NumFramesInFlight = 3;
        info.RenderTargetFormat = pimpl->m_color_format;
        info.DepthStencilFormat = WGPUTextureFormat_Undefined;
        info.PipelineMultisampleState.count = pimpl->m_samples;
        info.PipelineMultisampleState.mask = 0xFFFFFFFFu;

        if (!ImGui_ImplWGPU_Init(&info)) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to initialize the WebGPU backend for ImGui.");
            return;
        }
        pimpl->m_backend_initialized = true;
    }

    ImGuiContext *GUISystem::GetCurrentContext() const {
        return pimpl->m_context;
    }

    void GUISystem::ResetColorAttachmentFormat(Rhi::ImageFormat format, uint8_t samples) noexcept {
        // The pipelines ImGui built are for that format; they are rebuilt by the `CreateBackend`
        // the caller makes after a resize.
        pimpl->m_color_format = Rhi::ToWgpuTextureFormat(format);
        pimpl->m_samples = samples == 0 ? 1 : samples;
    }

    ImTextureID GUISystem::AddTexture(const Rhi::Texture &texture) {
        // This backend's user-texture binding *is* a texture view: ImGui creates an image bind
        // group from it per draw and owns nothing, so the id borrows the texture's view.
        return reinterpret_cast<ImTextureID>(Rhi::ToWgpuTextureView(texture.GetImageView()));
    }

    void GUISystem::RemoveTexture(ImTextureID) {
        // Nothing was registered: the id names a view the texture owns.
    }
} // namespace Engine
