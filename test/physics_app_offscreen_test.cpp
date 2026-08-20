#include <PhysicsApp.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace AppPhysics;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    template <typename Fn>
    bool ThrowsLogic(Fn fn) {
        try {
            fn();
        } catch (const std::logic_error &) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    }
} // namespace

int main() {
    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    CreateInfo info{};
    info.mode = AppMode::Offscreen;
    info.resol_x = kWidth;
    info.resol_y = kHeight;

    auto app = PhysicsApp::Create(info);

    // Build a visible scene: body + light over the sky.
    app->AddBox({.position = {0.0f, 0.0f, -0.5f}, .half_extents = {5.0f, 5.0f, 0.5f}, .mass = 0.0f, .kinematic = true});
    app->AddSphere({.position = {0.0f, 0.0f, 0.5f}, .radius = 0.5f, .mass = 1.0f});
    app->AddDirectionalLight({.direction = {0.0f, 0.0f, -1.0f}, .intensity = 1.5f});
    app->CommitScene();

    // GetRenderOutput while disabled throws.
    Check(ThrowsLogic([&] { (void)app->GetRenderOutput(); }), "GetRenderOutput while disabled throws");

    // Enable readback and render.
    app->SetRenderReadbackEnabled(true);

    // No frame captured yet -> still throws.
    Check(ThrowsLogic([&] { (void)app->GetRenderOutput(); }), "GetRenderOutput before any capture throws");

    app->RenderNextFrame();
    RenderOutput out = app->GetRenderOutput();
    Check(out.pixels != nullptr, "pixels non-null");
    Check(out.width == kWidth && out.height == kHeight, "output dimensions match present extent");

    // Pixels are not all zero (sky + scene render).
    const auto *px = static_cast<const uint8_t *>(out.pixels);
    size_t nonzero = 0;
    for (size_t i = 0; i < size_t(kWidth) * kHeight * 4u; i++) {
        if (px[i] != 0) {
            nonzero++;
        }
    }
    Check(nonzero > 0, "pixels not all zero");

    // Second frame advances frame_id monotonically.
    uint64_t first_id = out.frame_id;
    app->RenderNextFrame();
    RenderOutput out2 = app->GetRenderOutput();
    Check(out2.frame_id > first_id, "frame_id increases across frames");
    Check(out2.width == kWidth && out2.height == kHeight, "dimensions stable");

    // No window close -> never quit.
    Check(!app->ShouldQuit(), "ShouldQuit false in offscreen mode");

    if (g_failures == 0) {
        std::cout << "physics_app_offscreen_test PASSED." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_offscreen_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
