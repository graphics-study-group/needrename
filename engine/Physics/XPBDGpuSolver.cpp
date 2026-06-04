#include "XPBDGpuSolver.h"

#include "PhysicsScene.h"

#include <Asset/Shader/ShaderCompiler.h>
#include <Render/Memory/ComputeBuffer.h>
#include <Render/Memory/DeviceBuffer.h>
#include <Render/Memory/ShaderParameters/ShaderResourceBinding.h>
#include <Render/Pipeline/CommandBuffer/ComputeCommandBuffer.h>
#include <Render/Pipeline/Compute/ComputeResourceBinding.h>
#include <Render/Pipeline/Compute/ComputeStage.h>
#include <Render/RenderSystem.h>
#include <Render/RenderSystem/DeviceInterface.h>
#include <Render/RenderSystem/FrameManager.h>
#include <Render/RenderSystem/Structs.h>

#include <SDL3/SDL.h>

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
    void XPBDGpuSolver::Step(RenderSystem &render_system, PhysicsScene &physics_scene) {
        const ComputeBuffer *rigid_body_alive = physics_scene.GetGpuRigidBodyAliveBuffer();
        const ComputeBuffer *rigid_body_center_position = physics_scene.GetGpuRigidBodyCenterPositionBuffer();
        const uint32_t rigid_body_count = physics_scene.GetGpuRigidBodySlotCount();

        if (rigid_body_alive == nullptr || rigid_body_center_position == nullptr || rigid_body_count == 0u) {
            return;
        }

        ShaderCompiler compiler{};
        std::vector<uint32_t> spirv{};
        compiler.CompileGLSLtoSPV(spirv, kPlaceholderXpbdStepShader, EShLangCompute);

        ComputeStage compute_stage{render_system};
        compute_stage.Instantiate(spirv, "Physics XPBD Placeholder Step");

        auto &resource_binding = compute_stage.AllocateResourceBinding();
        resource_binding.GetShaderResourceBinding().BindBuffer("RigidBodyAlive", *rigid_body_alive);
        resource_binding.GetShaderResourceBinding().BindBuffer("RigidBodyCenterPosition", *rigid_body_center_position);

        const auto &queue_info = render_system.GetDeviceInterface().GetQueueInfo();
        vk::CommandBufferAllocateInfo command_buffer_allocate_info{
            queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
        };
        auto command_buffers = render_system.GetDevice().allocateCommandBuffers(command_buffer_allocate_info);
        vk::CommandBuffer command_buffer = command_buffers[0];

        command_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

        const vk::MemoryBarrier2 pre_barrier{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {pre_barrier}, {}, {}});

        {
            ComputeCommandBuffer compute_command_buffer{
                command_buffer, render_system.GetFrameManager().GetFrameInFlight()
            };
            compute_command_buffer.BindComputeStage(compute_stage);
            compute_command_buffer.BindComputeResource(resource_binding);
            compute_command_buffer.DispatchCompute((rigid_body_count + 63u) / 64u, 1, 1);
        }

        const vk::MemoryBarrier2 post_barrier{
            vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderStorageWrite,
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {post_barrier}, {}, {}});
        command_buffer.end();

        const vk::SubmitInfo submit_info{{}, {}, {command_buffer}, {}};
        queue_info.graphicsQueue.submit(submit_info);
        queue_info.graphicsQueue.waitIdle();

        // physics_scene.DebugApplyPlaceholderCenterShift(glm::vec4(0.0f, -0.01f, 0.0f, 0.0f));

        SDL_LogWarn(
            SDL_LOG_CATEGORY_APPLICATION,
            "XPBDGpuSolver::Step is using placeholder logic: alive rigid body center.y -= 0.01"
        );
    }
} // namespace Engine