// PhysicsSystem::GPUCalcModelMatrices is scene-scoped (task 4.2).
//
// A target buffer supplied by the caller must be written only by the solvers
// registered for the named scene, in registration order; another scene's
// solvers must not receive the call, and a scene with no solvers must be
// skipped without touching the target.
//
// The solvers are stubs: they record the call instead of dispatching, so the
// only GPU work in this test is creating the target buffer.

#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/Solver/ISolver.h"
#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>
#include <glm.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

using namespace Engine;

namespace {
    int g_failures = 0;

    void Check(bool cond, const char *what) {
        if (!cond) {
            std::cerr << "FAIL: " << what << std::endl;
            g_failures++;
        }
    }

    /// @brief A solver that records every model matrix production it receives,
    /// instead of recording a dispatch.
    class RecordingSolver final : public ISolver {
    public:
        explicit RecordingSolver(std::string name, std::vector<std::string> *call_log) :
            m_name(std::move(name)), m_call_log(call_log) {
        }

        void GPUStep(vk::CommandBuffer) override {
        }

        [[nodiscard]]
        bool IsInitialized() const noexcept override {
            return true;
        }

        void GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) override {
            m_calls++;
            m_last_command_buffer = cb;
            m_last_target = &target;
            m_call_log->push_back(m_name);
        }

        int GetCallCount() const noexcept {
            return m_calls;
        }
        Rhi::ComputeBuffer *GetLastTarget() const noexcept {
            return m_last_target;
        }

    private:
        std::string m_name;
        std::vector<std::string> *m_call_log;
        int m_calls{0};
        vk::CommandBuffer m_last_command_buffer{};
        Rhi::ComputeBuffer *m_last_target{nullptr};
    };
} // namespace

int main() {
    SDL_Init(SDL_INIT_VIDEO);

    Rhi::DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "PhysicsSystem model matrices test",
        .application_version = 0,
        .dynamic_dispatcher = nullptr,
    };
    Rhi::DeviceInterface gpu_device{cfg};
    Rhi::AllocatorState allocator{gpu_device};

    auto target = Rhi::ComputeBuffer::CreateUnique(
        allocator, 4u * sizeof(glm::mat4), true, false, false, false, "Model matrices target"
    );
    Check(target != nullptr, "the target buffer must be creatable");

    const vk::CommandBuffer cb{};

    PhysicsSystem physics;
    PhysicsScene &scene1 = physics.CreateScene(1u);
    PhysicsScene &scene2 = physics.CreateScene(2u);
    PhysicsScene &empty_scene = physics.CreateScene(3u);

    std::vector<std::string> call_log;
    auto first = std::make_unique<RecordingSolver>("scene1-first", &call_log);
    auto second = std::make_unique<RecordingSolver>("scene1-second", &call_log);
    auto other = std::make_unique<RecordingSolver>("scene2-solver", &call_log);
    RecordingSolver *const first_ptr = first.get();
    RecordingSolver *const second_ptr = second.get();
    RecordingSolver *const other_ptr = other.get();
    physics.RegisterSolver(1u, std::move(first));
    physics.RegisterSolver(1u, std::move(second));
    physics.RegisterSolver(2u, std::move(other));

    // ── The call reaches exactly the named scene's solvers, in order ────────

    physics.GPUCalcModelMatrices(scene1, cb, *target);
    Check(call_log.size() == 2u, "only the named scene's two solvers must be invoked");
    Check(
        call_log.size() >= 2u && call_log[0] == "scene1-first" && call_log[1] == "scene1-second",
        "the solvers of a scene must be invoked in registration order"
    );
    Check(other_ptr->GetCallCount() == 0, "another scene's solver must not receive the call");
    Check(
        first_ptr->GetLastTarget() == target.get() && second_ptr->GetLastTarget() == target.get(),
        "the supplied target must be handed to each solver"
    );

    // ── Another scene is a separate call that reaches only its own solver ───

    call_log.clear();
    physics.GPUCalcModelMatrices(scene2, cb, *target);
    Check(call_log.size() == 1u && call_log[0] == "scene2-solver", "scene 2's own solver must be invoked");
    Check(
        first_ptr->GetCallCount() == 1 && second_ptr->GetCallCount() == 1,
        "scene 1's solvers must not be invoked for scene 2"
    );

    // ── A scene with no solvers is skipped ──────────────────────────────────

    call_log.clear();
    physics.GPUCalcModelMatrices(empty_scene, cb, *target);
    Check(call_log.empty(), "a scene with no registered solvers must be skipped");
    Check(
        first_ptr->GetCallCount() == 1 && second_ptr->GetCallCount() == 1 && other_ptr->GetCallCount() == 1,
        "no solver may receive an additional call for a solver-less scene"
    );

    if (g_failures != 0) {
        std::cerr << "PhysicsSystem model matrices test FAILED (" << g_failures << " checks)." << std::endl;
        return 1;
    }
    std::cout << "PhysicsSystem model matrices test PASSED." << std::endl;
    return 0;
}
