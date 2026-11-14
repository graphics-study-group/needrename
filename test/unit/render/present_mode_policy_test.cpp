// The surface's present mode is a policy, not an accident of the order a surface happens to
// advertise its modes in (task 1.1).
//
// The policy is a pure function of the target and the advertised list, so it needs no device: what
// it decides is what `SurfacePresentProvider` configures, and the cases below are the ones the
// backend has to get right - a desktop surface with Mailbox, one without, a page canvas, and the
// fact that Immediate is never chosen for the caller.

#include "Render/Backend/WebGPU/RenderSystem/PresentModePolicy.h"

#include <array>
#include <cstdio>
#include <span>

using Engine::RenderSystemState::OffersPresentMode;
using Engine::RenderSystemState::PresentModeName;
using Engine::RenderSystemState::PresentTarget;
using Engine::RenderSystemState::SelectPresentMode;

namespace {
    int g_failures = 0;

    void Expect(WGPUPresentMode actual, WGPUPresentMode expected, const char *what) {
        if (actual == expected) {
            return;
        }
        std::fprintf(
            stderr, "FAILED: %s selected %s, expected %s\n", what, PresentModeName(actual), PresentModeName(expected)
        );
        ++g_failures;
    }
} // namespace

int main() {
    // A desktop window that offers both: Mailbox wins, because a present that does not queue
    // behind an unfinished frame is the point of choosing a mode at all.
    const std::array<WGPUPresentMode, 3> mailbox_and_fifo{
        WGPUPresentMode_Fifo, WGPUPresentMode_Mailbox, WGPUPresentMode_Immediate
    };
    Expect(
        SelectPresentMode(PresentTarget::Native, mailbox_and_fifo),
        WGPUPresentMode_Mailbox,
        "native surface offering Mailbox"
    );

    // The same list in the other order: the decision is about which modes exist, not where the
    // surface listed them.
    const std::array<WGPUPresentMode, 3> mailbox_last{
        WGPUPresentMode_Immediate, WGPUPresentMode_Fifo, WGPUPresentMode_Mailbox
    };
    Expect(
        SelectPresentMode(PresentTarget::Native, mailbox_last),
        WGPUPresentMode_Mailbox,
        "native surface listing Mailbox last"
    );

    // A FIFO-only surface: the fallback has to be FIFO, not Immediate, even though the surface
    // offers something other than FIFO in the list below.
    const std::array<WGPUPresentMode, 2> fifo_and_immediate{WGPUPresentMode_Fifo, WGPUPresentMode_Immediate};
    Expect(
        SelectPresentMode(PresentTarget::Native, fifo_and_immediate),
        WGPUPresentMode_Fifo,
        "native surface without Mailbox"
    );

    const std::array<WGPUPresentMode, 1> fifo_only{WGPUPresentMode_Fifo};
    Expect(SelectPresentMode(PresentTarget::Native, fifo_only), WGPUPresentMode_Fifo, "FIFO-only native surface");

    // A page canvas stays on FIFO even when the surface claims Mailbox: the browser composites
    // the canvas, and the backend does not get to change that.
    Expect(SelectPresentMode(PresentTarget::Browser, mailbox_and_fifo), WGPUPresentMode_Fifo, "browser canvas");
    Expect(SelectPresentMode(PresentTarget::Browser, fifo_only), WGPUPresentMode_Fifo, "browser FIFO-only canvas");

    // No list at all: FIFO, which is the one mode every surface must accept.
    Expect(SelectPresentMode(PresentTarget::Native, {}), WGPUPresentMode_Fifo, "native surface with no mode list");
    Expect(SelectPresentMode(PresentTarget::Browser, {}), WGPUPresentMode_Fifo, "browser surface with no mode list");

    // Immediate is reached only when a surface offers nothing else, because there is no other mode
    // to configure and a mode the surface does not advertise is a validation error.
    const std::array<WGPUPresentMode, 1> immediate_only{WGPUPresentMode_Immediate};
    Expect(
        SelectPresentMode(PresentTarget::Native, immediate_only),
        WGPUPresentMode_Immediate,
        "native surface offering Immediate alone"
    );

    // A surface that advertises Mailbox but not FIFO still gets Mailbox: the list is what is
    // configured against, and FIFO is only a preference, not a requirement.
    const std::array<WGPUPresentMode, 1> mailbox_only{WGPUPresentMode_Mailbox};
    Expect(
        SelectPresentMode(PresentTarget::Native, mailbox_only),
        WGPUPresentMode_Mailbox,
        "native surface offering Mailbox alone"
    );

    if (!OffersPresentMode(mailbox_and_fifo, WGPUPresentMode_Mailbox)) {
        std::fprintf(stderr, "FAILED: a mode the list holds was reported as absent\n");
        ++g_failures;
    }
    if (OffersPresentMode(fifo_only, WGPUPresentMode_Mailbox)) {
        std::fprintf(stderr, "FAILED: a mode the list does not hold was reported as present\n");
        ++g_failures;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "present_mode_policy_test FAILED with %d mismatch(es).\n", g_failures);
        return 1;
    }
    std::printf("present_mode_policy_test PASSED: Mailbox preferred on native, FIFO on the browser.\n");
    return 0;
}
