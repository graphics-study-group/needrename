#ifndef ENGINE_GUI_GUISYSTEM_INCLUDED
#define ENGINE_GUI_GUISYSTEM_INCLUDED

#include <SDL3/SDL.h>
#include <imgui.h>
#include <memory>

namespace vk {
    class Extent2D;
    enum class Format;
} // namespace vk

namespace Engine {
    class RenderSystem;
    namespace AttachmentUtils {
        class AttachmentDescription;
    }
    class GraphicsCommandBuffer;

    class GUISystem {
    protected:
        ImGuiContext *m_context{nullptr};

        void CleanUp();

    public:
        /// @brief Construct a GUI system with render system
        /// @param render_system
        /// Standard dependency injection pattern
        GUISystem();
        ~GUISystem();

        GUISystem(const GUISystem &) = delete;
        GUISystem(GUISystem &&) = delete;
        void operator=(const GUISystem &) = delete;
        void operator=(GUISystem &&) = delete;

        bool WantCaptureMouse() const;
        bool WantCaptureKeyboard() const;
        void ProcessEvent(SDL_Event *event) const;

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
            const AttachmentUtils::AttachmentDescription &attachment, vk::Extent2D extent, GraphicsCommandBuffer &cb
        ) const;

        /**
         * @brief Initialize GUISystem.
         * This method only initalizes ImGUI front-end states. To
         * actually render anything you need to create
         * the Vulkan backend by calling `CreateVulkanBackend()`
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
         */
        void CreateVulkanBackend(RenderSystem & render_system, vk::Format color_attachment_format);

        /**
         * @brief Get current ImGui context for drawing.
         * 
         * As the context is not shared across DLL boundaries, you might
         * have to manually call `ImGui::SetCurrentContext()` before
         * building a GUI.
         */
        ImGuiContext * GetCurrentContext() const;
    };
} // namespace Engine

#endif // ENGINE_GUI_GUISYSTEM_INCLUDED
