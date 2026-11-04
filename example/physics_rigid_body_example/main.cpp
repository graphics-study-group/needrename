// A rigid-body scene driven by the engine's physics app, on a desktop window or a browser canvas.
//
// The scene is the smallest one that makes motion obvious: a stack of boxes dropped onto a ground
// plane, plus two spheres rolling off the top of it. If the simulation is not running the stack
// hangs in the air where it was placed, which is exactly what a picture of the first frame shows;
// a later frame with the stack collapsed is the evidence that bodies moved.
//
// The loop is the same code in both builds and is driven differently: the browser calls it from its
// own animation loop, and the desktop build runs it in a `while`.

#include <PhysicsApp.h>

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <gtc/quaternion.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace AppPhysics;

namespace {
    constexpr uint32_t kScreenWidth = 1280;
    constexpr uint32_t kScreenHeight = 720;
    /// Frames after which a finite run reports and stops. A browser page has to end on its own.
    constexpr uint32_t kDefaultFrames = 600;
    /// How often a body's position is logged, so a run says whether anything moved.
    constexpr uint32_t kLogEveryFrames = 60;

    std::unique_ptr<PhysicsApp> g_app{};
    uint32_t g_frames{0};
    uint32_t g_frame_limit{kDefaultFrames};
    BodyId g_probe_body{INVALID_BODY_ID};
    glm::vec3 g_probe_start{};

    /// A pyramid of boxes with two spheres above it, on a large kinematic ground plane.
    void BuildScene(PhysicsApp &app) {
        // Z is up. The ground is a big thin box.
        app.AddBox({
            .position = {0.0f, 0.0f, -0.5f},
            .half_extents = {50.0f, 50.0f, 0.5f},
            .mass = 0.0f,
            .kinematic = true,
            .color = "white",
        });

        // A five-high stack, each layer a row of boxes. It collapses when the solver runs.
        constexpr int kLayers = 5;
        const glm::vec3 half_extents(0.25f, 0.25f, 0.25f);
        for (int layer = 0; layer < kLayers; ++layer) {
            const int count = kLayers - layer;
            for (int i = 0; i < count; ++i) {
                const glm::vec3 position(
                    -0.5f * static_cast<float>(count - 1) + static_cast<float>(i),
                    0.0f,
                    half_extents.z + 2.0f * half_extents.z * static_cast<float>(layer) + 0.05f
                );
                app.AddBox({
                    .position = position,
                    .half_extents = half_extents,
                    .mass = 0.5f,
                    .color = (layer % 2 == 0) ? "blue" : "red",
                });
            }
        }

        // Two spheres placed above the stack, so they fall and roll off it.
        g_probe_body = app.AddSphere({
            .position = {0.0f, 0.0f, 4.0f},
            .radius = 0.28f,
            .mass = 1.0f,
            .color = "green",
        });
        g_probe_start = glm::vec3(0.0f, 0.0f, 4.0f);
        app.AddSphere({
            .position = {0.9f, 0.4f, 5.0f},
            .radius = 0.28f,
            .mass = 1.0f,
            .color = "orange",
        });

        app.AddDirectionalLight({.direction = {0.0f, 0.0f, -1.0f}, .intensity = 1.0f, .cast_shadow = true});
        app.AddDirectionalLight({.direction = {1.0f, 1.0f, -1.0f}, .intensity = 0.6f, .cast_shadow = false});

        // Looking down at the stack from the front, close enough that the boxes fill the frame.
        app.SetCameraPose(glm::vec3(5.5f, -6.5f, 5.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    /// One frame: advance the simulation and draw it.
    void Frame() {
        SDL_Log("physics example: frame %u begins", g_frames);
        if (!g_app->IsPaused()) {
            g_app->Step();
        }
        SDL_Log("physics example: frame %u stepped", g_frames);
        g_app->RenderNextFrame();

        ++g_frames;
#if !defined(__EMSCRIPTEN__)
        // The readback probe is a desktop diagnostic: reading a body's state maps a readback buffer,
        // and a page cannot block on that map. The browser's evidence is the frame itself.
        if (g_probe_body != INVALID_BODY_ID && g_frames % kLogEveryFrames == 0) {
            const BodyState state = g_app->GetBodyState(g_probe_body);
            SDL_Log(
                "physics example: frame %u, probe z %.3f (started %.3f), moved %.3f m",
                g_frames,
                state.position.z,
                g_probe_start.z,
                glm::length(state.position - g_probe_start)
            );
        }
#else
        if (g_frames % kLogEveryFrames == 0) {
            SDL_Log("physics example: frame %u simulated", g_frames);
        }
#endif
    }

    bool ShouldStop() {
        return g_app->ShouldQuit() || g_frames >= g_frame_limit;
    }
} // namespace

#if defined(__EMSCRIPTEN__)
namespace {
    /// The engine resolves its data root by looking next to the executable. A page has no such
    /// place, so the tree is preloaded at `/` and the root is pointed there.
    struct DataRootForBrowser {
        DataRootForBrowser() {
            setenv("NEEDRENAME_DATA_ROOT", "/", 0);
        }
    };
    const DataRootForBrowser g_data_root_for_browser{};

} // namespace
#endif

int main(int argc, char **argv) {
    if (argc > 1) {
        g_frame_limit = static_cast<uint32_t>(std::stoul(argv[1]));
    }

    CreateInfo info{};
    info.mode = AppMode::Windowed;
    info.resol_x = kScreenWidth;
    info.resol_y = kScreenHeight;
    info.window_title = "Physics rigid body example";

    g_app = PhysicsApp::Create(info);
    if (g_app == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "physics example: the app could not be created");
        return 1;
    }

    BuildScene(*g_app);
    g_app->CommitScene();
    g_app->Resume();
    SDL_Log("physics example: scene committed, %u frames planned", g_frame_limit);

    // One loop for both targets, which is the shape the engine's own `MainClass::LoopFinite` uses in
    // a browser: a plain `while`, with a sleep on the web so each iteration hands control back to
    // the page. `emscripten_set_main_loop` is not needed for that and was the reason none of the
    // first attempts rendered a frame - the page returned to the browser and never called back in.
    while (!ShouldStop()) {
        Frame();
#if defined(__EMSCRIPTEN__)
        // The waits inside a frame are polls on wgpuInstanceProcessEvents, and this loop is what
        // lets the browser run the callbacks those polls deliver; without the yield they never
        // arrive and the first Step() spins for ever.
        emscripten_sleep(1);
#endif
    }
    SDL_Log("physics example: %u frames, done", g_frames);

    return 0;
}
