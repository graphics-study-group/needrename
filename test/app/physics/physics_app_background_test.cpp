#include <PhysicsApp.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace AppPhysics;

/**
 * @file physics_app_background_test.cpp
 *
 * @brief Offscreen assertion that the frame background is the configured solid
 * clear color.
 *
 * `MainClass` is a process singleton (a `std::once_flag` guards its creation),
 * so a single process can create exactly one `PhysicsApp`. The test therefore
 * takes the expected background as optional command-line arguments and CTest
 * registers it twice from this one executable:
 *
 * - no arguments → default creation must render black background pixels;
 * - `r g b`      → the configured linear rgb color must be rendered.
 *
 * The scene holds one box at the origin so "away from geometry" is meaningful:
 * the frame corners must show the background, and the box must actually cover
 * some pixels (proving the background is not the whole frame).
 */

namespace {
    int g_failures = 0;

    void Check(bool cond, const std::string &what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    /// Match a byte channel against the linear clear color with tolerance.
    bool ChannelNear(uint8_t actual, float expected, int tolerance) {
        return std::abs(static_cast<int>(actual) - static_cast<int>(std::lround(expected * 255.0f))) <= tolerance;
    }

    bool PixelNear(const uint8_t *px, const glm::vec3 &expected, int tolerance) {
        return ChannelNear(px[0], expected.r, tolerance) && ChannelNear(px[1], expected.g, tolerance)
               && ChannelNear(px[2], expected.b, tolerance);
    }
} // namespace

int main(int argc, char **argv) {
    constexpr uint32_t kWidth = 128;
    constexpr uint32_t kHeight = 128;

    glm::vec3 expected{0.0f, 0.0f, 0.0f};
    bool configured = false;
    if (argc == 4) {
        expected =
            glm::vec3(std::strtof(argv[1], nullptr), std::strtof(argv[2], nullptr), std::strtof(argv[3], nullptr));
        configured = true;
    } else if (argc != 1) {
        std::cerr << "usage: physics_app_background_test [r g b]" << std::endl;
        return 2;
    }

    CreateInfo info{};
    info.mode = AppMode::Offscreen;
    info.resol_x = kWidth;
    info.resol_y = kHeight;
    info.background_color = expected;

    auto app = PhysicsApp::Create(info);
    // One box at the origin: it covers a fraction of the frame, the rest is background.
    app->AddBox({.position = {0.0f, 0.0f, 0.0f}, .half_extents = {1.0f, 1.0f, 1.0f}, .mass = 0.0f, .kinematic = true});
    app->AddDirectionalLight({.direction = {0.0f, 0.0f, -1.0f}, .intensity = 1.5f});
    app->CommitScene();

    app->SetRenderReadbackEnabled(true);
    app->RenderNextFrame();

    const RenderOutput out = app->GetRenderOutput();
    Check(out.pixels != nullptr, "pixels non-null");
    Check(out.width == kWidth && out.height == kHeight, "output dimensions match resol");

    if (out.pixels == nullptr || out.width != kWidth || out.height != kHeight) {
        std::cerr << "physics_app_background_test FAILED (" << g_failures << " failures)." << std::endl;
        return 1;
    }

    const auto *px = static_cast<const uint8_t *>(out.pixels);
    const auto pixel_at = [px](uint32_t x, uint32_t y) { return px + (size_t(y) * kWidth + x) * 4u; };

    // Film is written top-to-bottom; the corners are the frame's extreme pixels
    // and are never covered by the origin-centred box.
    const uint32_t corners[4][2] = {{0, 0}, {kWidth - 1, 0}, {0, kHeight - 1}, {kWidth - 1, kHeight - 1}};
    for (const auto &c : corners) {
        Check(
            PixelNear(pixel_at(c[0], c[1]), expected, 2),
            std::string("corner pixel is the background color at (") + std::to_string(c[0]) + ", "
                + std::to_string(c[1]) + ")"
        );
    }

    // Edge midpoints: still background (the box is centred and small).
    const uint32_t edges[4][2] = {
        {0, kHeight / 2}, {kWidth - 1, kHeight / 2}, {kWidth / 2, 0}, {kWidth / 2, kHeight - 1}
    };
    for (const auto &e : edges) {
        Check(
            PixelNear(pixel_at(e[0], e[1]), expected, 2),
            std::string("edge pixel is the background color at (") + std::to_string(e[0]) + ", " + std::to_string(e[1])
                + ")"
        );
    }

    // The box must actually be rendered: some pixels differ from the background.
    size_t geometry_pixels = 0;
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            if (!PixelNear(pixel_at(x, y), expected, 2)) {
                geometry_pixels++;
            }
        }
    }
    Check(geometry_pixels > 200, "the origin box covers a visible part of the frame");

    // Alpha is opaque for background pixels.
    Check(pixel_at(0, 0)[3] == 255, "background alpha is opaque");

    if (g_failures == 0) {
        std::cout << "physics_app_background_test PASSED (" << (configured ? "configured" : "default")
                  << " background)." << std::endl;
        return 0;
    }
    std::cerr << "physics_app_background_test FAILED (" << g_failures << " failures)." << std::endl;
    return 1;
}
