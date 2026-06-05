#include "XPBDGpuSolver.h"

#include "PhysicsScene.h"

#include <Asset/Shader/ShaderCompiler.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/MemoryAccessTypes.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/Pipeline/RenderGraph/RGAttachmentDesc.h>
#include <Render/Pipeline/RenderGraph/RenderGraphBuilder.h>
#include <Render/Pipeline/RenderGraph/RenderGraphPass.h>
#include <Render/RenderSystem.h>

namespace {
    constexpr const char kPlaceholderXpbdStepShader[] = R"(
#version 450 core
#extension GL_EXT_debug_printf : enable

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) readonly buffer RigidBodyAlive {
    uint v[];
} rigid_body_alive;

layout(set = 0, binding = 1) buffer RigidBodyCenterPosition {
    vec4 v[];
} rigid_body_center_position;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= rigid_body_alive.v.length()) {
        return;
    }
    if (rigid_body_alive.v[index] == 0u) {
        return;
    }

    debugPrintfEXT("XPBD step: rigid_body[%u] before.y=%.2f", index, rigid_body_center_position.v[index].y);

    rigid_body_center_position.v[index].y -= 0.01;

    debugPrintfEXT("XPBD step: rigid_body[%u] after.y=%.2f", index, rigid_body_center_position.v[index].y);
}
)";
} // namespace

namespace Engine {

    struct XPBDGpuSolver::Impl {
        RenderSystem &render_system;

        bool initialized = false;

        // Owned compute pipeline — created once by EnsureInitialized().
        std::unique_ptr<ComputeStage> compute_stage{};

        // Non-owning pointer into compute_stage.
        ComputeResourceBinding *resource_binding = nullptr;

        // Cached compiled SPIR-V so we never recompile.
        std::vector<uint32_t> cached_spirv{};

        explicit Impl(RenderSystem &rs) : render_system(rs) {
        }

        Impl(const Impl &) = delete;
        Impl &operator=(const Impl &) = delete;
        Impl(Impl &&) = delete;
        Impl &operator=(Impl &&) = delete;

        /**
         * @brief Lazily compile the compute shader and create the pipeline.
         *
         * Idempotent — only does work on the first call.
         */
        void EnsureInitialized() {
            if (initialized) return;
            initialized = true;

            ShaderCompiler compiler{};
            compiler.CompileGLSLtoSPV(cached_spirv, kPlaceholderXpbdStepShader, EShLangCompute);

            compute_stage = std::make_unique<ComputeStage>(render_system);
            compute_stage->Instantiate(cached_spirv, "Physics XPBD Placeholder Step");

            resource_binding = &compute_stage->AllocateResourceBinding();
        }
    };

    XPBDGpuSolver::XPBDGpuSolver(RenderSystem &render_system) : m_impl(std::make_unique<Impl>(render_system)) {
    }

    XPBDGpuSolver::~XPBDGpuSolver() = default;

    bool XPBDGpuSolver::IsInitialized() const noexcept {
        return m_impl->initialized;
    }

    void XPBDGpuSolver::Step(RenderGraphBuilder &builder, PhysicsScene &physics_scene) {
        const auto gpu = physics_scene.GetGpuBuffers();

        // Bail if essential buffers are not yet created.
        if (gpu.rigid_body_alive == nullptr || gpu.rigid_body_center_world_position == nullptr
            || gpu.rigid_body_slot_count == 0u) {
            return;
        }

        // --- Lazy initialization (first call only) ---
        m_impl->EnsureInitialized();

        // --- Refresh shader resource bindings ---
        // PhysicsScene may recreate GPU buffers between calls (e.g. via
        // RefreshGpuBuffers()), so we rebind every Step().  Descriptor sets are
        // pooled by ComputeStage so rebinding is cheap — no reallocation.
        auto &srb = m_impl->resource_binding->GetShaderResourceBinding();
        srb.BindBuffer("RigidBodyAlive", *gpu.rigid_body_alive);
        srb.BindBuffer("RigidBodyCenterPosition", *gpu.rigid_body_center_world_position);

        // --- Import external resources into the render graph ---
        // ComputeBuffer inherits DeviceBuffer, so the const DeviceBuffer& overload
        // of ImportExternalResource matches directly.
        auto alive_handle = builder.ImportExternalResource(*gpu.rigid_body_alive, {MemoryAccessTypeBufferBits::None});
        auto position_handle =
            builder.ImportExternalResource(*gpu.rigid_body_center_world_position, {MemoryAccessTypeBufferBits::None});

        // --- Add the XPBD compute pass ---
        // Capture raw pointers (not references) — the solver owns compute_stage
        // so these outlive the lambda stored in the render graph pass.
        auto *cstage = m_impl->compute_stage.get();
        auto *cbinding = m_impl->resource_binding;

        // Capture slot_count by value.  For the transitional placeholder this is
        // acceptable; the test has a fixed rigid body count after initialization.
        // TODO: replace with indirect dispatch or per-frame UBO update.
        uint32_t slot_count = gpu.rigid_body_slot_count;

        builder.AddPass(
            RenderGraphPassBuilder{m_impl->render_system}
                .SetName("XPBD Step")
                .UseBuffer(alive_handle, {MemoryAccessTypeBufferBits::ShaderRandomRead})
                .UseBuffer(
                    position_handle,
                    {MemoryAccessTypeBufferBits::ShaderRandomRead, MemoryAccessTypeBufferBits::ShaderRandomWrite}
                )
                .SetAffinity(RenderGraphPassAffinity::Compute)
                .SetPassFunction([cstage, cbinding, slot_count](CommandBuffer &cb, const RenderGraph &) -> void {
                    cb.BindComputeStage(*cstage);
                    cb.BindComputeResource(*cbinding);
                    cb.DispatchCompute((slot_count + 63u) / 64u, 1, 1);
                })
                .Get()
        );
    }
} // namespace Engine
