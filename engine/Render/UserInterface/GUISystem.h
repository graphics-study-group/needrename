#ifndef RENDER_USERINTERFACE_GUISYSTEM_INCLUDED
#define RENDER_USERINTERFACE_GUISYSTEM_INCLUDED

#include "Render/render_export.h"
#include "Rhi/RhiTypes.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <memory>

namespace Engine {
    class RenderSystem;
    namespace AttachmentUtils {
        struct AttachmentDescription;
    }
    class CommandBuffer;
    namespace Rhi {
        class Texture;
    }

    class RENDER_API GUISystem {
    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        GUISystem();
        ~GUISystem();

        GUISystem(const GUISystem &) = delete;
        GUISystem(GUISystem &&) = delete;
        void operator=(const GUISystem &) = delete;
        void operator=(GUISystem &&) = delete;

        /**
         * @brief Check whether the GUI system will capture mouse events instead
         * of the main application.
         *
         * @retval true If the mouse event should be intercepted by the GUI system
         */
        bool WantCaptureMouse() const;

        /**
         * @brief Check whether the GUI system will capture key events instead
         * of the main application.
         *
         * @retval true If the keyboard event should be intercepted by the GUI system
         */
        bool WantCaptureKeyboard() const;

        /**
         * @brief Let the GUI system process an event.
         */
        void ProcessEvent(SDL_Event *event) const;

        /**
         * @brief Prepare the GUI for state updates and rendering.
         *
         * Call this function once before any operations are performed via ImGUI
         */
        void PrepareGUI() const;

        /**
         * @brief Record GUI rendering code on the given command buffer.
         * This member records a
         * new render pass on the given command buffer with a single color attachment
         * specified by the
         * `color_attachment_format` parameter of `CreateVulkanBackend()` call. If a previously
         * used
         * attachment is used again, synchronization barriers must be set up correctly.
         */
        void DrawGUI(
            const AttachmentUtils::AttachmentDescription &attachment, Rhi::Extent2D extent, CommandBuffer &cb
        ) const;

        /**
         * @brief Record GUI rendering code on the given command buffer.
         *
         * This method effectively only records draw calls onto the command buffer.
         */
        void DrawGUI(Rhi::CommandEncoder &encoder) const noexcept;

        /**
         * @brief Initialize GUISystem.
         * This method only initalizes ImGUI front-end states. To
         * actually render anything you need to create
         * the backend by calling `CreateBackend()`
         * method with attachment formats.
         */
        void Create(SDL_Window *window);

        /**
         * @brief Update the attachment formats for GUI rendering pipeline
         * by re-initializing the
         * ImGUI Vulkan backend.
         *
         * @param color_attachment_format Format of the color attachment.
         * If UNDEFINED will use swapchain format.
         * @param samples Multisample state. Reserved for future use.
         */
        void CreateBackend(RenderSystem &render_system, Rhi::ImageFormat color_format, uint8_t samples = 1);

        /**
         * @brief Get current ImGui context for drawing.
         *
         * As the context is not shared across DLL boundaries, you might
         * have to manually call `ImGui::SetCurrentContext()` before
         * building a GUI.
         */
        ImGuiContext *GetCurrentContext() const;

        /**
         * @brief Reset the color attachment format.
         *
         * This will recreate the rendering pipeline used by the Dear ImGui backend.
         */
        void ResetColorAttachmentFormat(Rhi::ImageFormat format, uint8_t samples = 1) noexcept;

        /**
         * @brief Register a texture as an image ImGui can draw.
         *
         * The returned id is what `ImGui::Image` takes. What it names depends on the backend: on
         * Vulkan a descriptor set built from the texture's view and sampler, on WebGPU the texture
         * view itself (which is what that backend's user-texture binding is). The texture must
         * outlive the id; the caller releases the id with `RemoveTexture` before the texture goes
         * away.
         *
         * @param texture The rendered texture to display; its sampled view is what gets bound.
         * @return An id usable as `ImGui::Image`'s texture id.
         */
        ImTextureID AddTexture(const Rhi::Texture &texture);

        /**
         * @brief Release an id returned by `AddTexture`.
         *
         * A backend whose id is a borrowed view does nothing here; one that registered a
         * descriptor set frees it.
         *
         * @param texture_id The id to release. `ImTextureID_Invalid` is ignored.
         */
        void RemoveTexture(ImTextureID texture_id);
    };
} // namespace Engine

#endif // RENDER_USERINTERFACE_GUISYSTEM_INCLUDED
