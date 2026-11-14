#ifndef RENDER_BACKEND_WEBGPU_PRESENTMODEPOLICY_INCLUDED
#define RENDER_BACKEND_WEBGPU_PRESENTMODEPOLICY_INCLUDED

#include <cstdint>
#include <span>
#include <webgpu/webgpu.h>

namespace Engine::RenderSystemState {
    /// @brief Which target a surface is configured for.
    enum class PresentTarget : uint8_t {
        /// @brief A desktop window: Mailbox is preferred when the surface offers it.
        Native,
        /// @brief A page canvas: FIFO, which is what a browser composites.
        Browser
    };

    /// @brief Whether a surface advertises a present mode.
    inline bool OffersPresentMode(std::span<const WGPUPresentMode> offered, WGPUPresentMode mode) noexcept {
        for (const WGPUPresentMode candidate : offered) {
            if (candidate == mode) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Pick the present mode for a surface from what it says it accepts.
     *
     * The preference is Mailbox on native - a present that does not queue behind an
     * unfinished frame is the difference between a frame-latency problem and an idle
     * queue - falling back to FIFO, which every surface offers. The browser stays on
     * FIFO: a page's surface is composited by the browser, and the modes a page can
     * request are the ones it already uses.
     *
     * Immediate is deliberately not part of the preference chain. It permits tearing and
     * changes what the window shows, which is a presentation decision this backend does
     * not get to make on a caller's behalf; it is selected only when a surface offers
     * nothing else, because configuring a surface with a mode it does not advertise is a
     * validation error rather than a fallback.
     *
     * @param target The kind of surface being configured.
     * @param offered The modes the surface reports, in its own order.
     * @return The mode to configure, which is one of `offered` when that list is not empty.
     */
    inline WGPUPresentMode SelectPresentMode(PresentTarget target, std::span<const WGPUPresentMode> offered) noexcept {
        const auto pick = [offered](WGPUPresentMode mode) -> bool { return OffersPresentMode(offered, mode); };

        if (target == PresentTarget::Native && pick(WGPUPresentMode_Mailbox)) {
            return WGPUPresentMode_Mailbox;
        }
        if (pick(WGPUPresentMode_Fifo)) {
            return WGPUPresentMode_Fifo;
        }
        // A surface with no list at all is configured for FIFO: it is the one mode the
        // standard interface requires every surface to accept.
        return offered.empty() ? WGPUPresentMode_Fifo : offered.front();
    }

    /// @brief The name of a present mode, for the line that reports what was configured.
    inline const char *PresentModeName(WGPUPresentMode mode) noexcept {
        switch (mode) {
        case WGPUPresentMode_Fifo:
            return "FIFO";
        case WGPUPresentMode_FifoRelaxed:
            return "FIFO-relaxed";
        case WGPUPresentMode_Immediate:
            return "Immediate";
        case WGPUPresentMode_Mailbox:
            return "Mailbox";
        default:
            return "unknown";
        }
    }
} // namespace Engine::RenderSystemState

#endif
