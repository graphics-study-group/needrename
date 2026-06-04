#include "Core/Functional/SDLWindow.h"
#include "Framework/component/physics/CollisionShapeComponent.h"
#include "Framework/component/physics/RigidBodyComponent.h"
#include "Framework/object/GameObject.h"
#include "Framework/world/Scene.h"
#include "Framework/world/WorldSystem.h"
#include "MainClass.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/XPBDGpuSolver.h"
#include "Render/FullRenderSystem.h"
#include "Render/Memory/ComputeBuffer.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/Pipeline/RenderGraph/ComplexRenderGraphBuilder.h"
#include "cmake_config.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>

using namespace Engine;

namespace {
    std::vector<uint32_t> ReadbackU32Buffer(RenderSystem &render_system, const ComputeBuffer &source, uint32_t count) {
        const size_t readback_size = std::max<size_t>(1, count) * sizeof(uint32_t);
        std::vector<uint32_t> values(count, 0u);

        auto staging = DeviceBuffer::CreateUnique(
            render_system.GetAllocatorState(),
            {BufferTypeBits::ReadbackFromDevice},
            readback_size,
            "Physics Test U32 Readback"
        );

        const auto &queue_info = render_system.GetDeviceInterface().GetQueueInfo();
        vk::CommandBufferAllocateInfo command_buffer_allocate_info{
            queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
        };
        auto command_buffers = render_system.GetDevice().allocateCommandBuffers(command_buffer_allocate_info);
        vk::CommandBuffer command_buffer = command_buffers[0];

        command_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        const vk::BufferMemoryBarrier2 pre_copy_barrier{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            source.GetBuffer(),
            0,
            readback_size
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {pre_copy_barrier}, {}});
        command_buffer.copyBuffer(source.GetBuffer(), staging->GetBuffer(), {vk::BufferCopy{0, 0, readback_size}});

        const vk::BufferMemoryBarrier2 post_copy_barrier{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            staging->GetBuffer(),
            0,
            readback_size
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {post_copy_barrier}, {}});
        command_buffer.end();

        queue_info.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {command_buffer}, {}});
        queue_info.graphicsQueue.waitIdle();

        staging->Invalidate();
        if (count > 0u) {
            std::memcpy(values.data(), staging->GetVMAddress(), count * sizeof(uint32_t));
        }
        return values;
    }

    std::vector<glm::vec4> ReadbackVec4Buffer(
        RenderSystem &render_system, const ComputeBuffer &source, uint32_t count
    ) {
        const size_t readback_size = std::max<size_t>(1, count) * sizeof(glm::vec4);
        std::vector<glm::vec4> values(count, glm::vec4(0.0f));

        auto staging = DeviceBuffer::CreateUnique(
            render_system.GetAllocatorState(),
            {BufferTypeBits::ReadbackFromDevice},
            readback_size,
            "Physics Test Vec4 Readback"
        );

        const auto &queue_info = render_system.GetDeviceInterface().GetQueueInfo();
        vk::CommandBufferAllocateInfo command_buffer_allocate_info{
            queue_info.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1
        };
        auto command_buffers = render_system.GetDevice().allocateCommandBuffers(command_buffer_allocate_info);
        vk::CommandBuffer command_buffer = command_buffers[0];

        command_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        const vk::BufferMemoryBarrier2 pre_copy_barrier{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferRead,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            source.GetBuffer(),
            0,
            readback_size
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {pre_copy_barrier}, {}});
        command_buffer.copyBuffer(source.GetBuffer(), staging->GetBuffer(), {vk::BufferCopy{0, 0, readback_size}});

        const vk::BufferMemoryBarrier2 post_copy_barrier{
            vk::PipelineStageFlagBits2::eAllTransfer,
            vk::AccessFlagBits2::eTransferWrite,
            vk::PipelineStageFlagBits2::eHost,
            vk::AccessFlagBits2::eHostRead,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            staging->GetBuffer(),
            0,
            readback_size
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {post_copy_barrier}, {}});
        command_buffer.end();

        queue_info.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {command_buffer}, {}});
        queue_info.graphicsQueue.waitIdle();

        staging->Invalidate();
        if (count > 0u) {
            std::memcpy(values.data(), staging->GetVMAddress(), count * sizeof(glm::vec4));
        }
        return values;
    }
} // namespace

int main(int argc, char **argv) {
    int64_t max_frame_count = 5;
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    std::filesystem::path project_path(ENGINE_PROJECTS_DIR);
    project_path = project_path / "test_project";

    SDL_Init(SDL_INIT_VIDEO);

    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Physics Test"};

    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);
    cmc->LoadBuiltinAssets(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR));

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Loading project");
    cmc->LoadProject(project_path);
    auto rgb = std::make_unique<ComplexRenderGraphBuilder>(*cmc->GetRenderSystem());
    auto [w, h] = cmc->GetWindow()->GetSize();
    int32_t final_color_id;
    auto rg = rgb->BuildDefaultRenderGraph(w, h, final_color_id);
    cmc->SetRenderGraph(rg, final_color_id);
    auto &world_system = *cmc->GetWorldSystem();
    Scene &scene = world_system.GetMainSceneRef();

    GameObject &root = scene.CreateGameObject();
    GameObject &child = scene.CreateGameObject();
    GameObject &child_rigidbody_root = scene.CreateGameObject();
    GameObject &child_rigidbody_grandchild = scene.CreateGameObject();
    GameObject &loose = scene.CreateGameObject();

    child.SetParent(root.GetHandle());
    child_rigidbody_root.SetParent(root.GetHandle());
    child_rigidbody_grandchild.SetParent(child_rigidbody_root.GetHandle());

    {
        Transform t;
        t.SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        root.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(2.0f, 0.0f, 0.0f));
        child.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(4.0f, 0.0f, 0.0f));
        child_rigidbody_root.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(5.0f, 0.0f, 0.0f));
        child_rigidbody_grandchild.SetTransform(t);
    }
    {
        Transform t;
        t.SetPosition(glm::vec3(10.0f, 0.0f, 0.0f));
        loose.SetTransform(t);
    }

    auto &root_rigidbody = root.AddComponent<RigidBodyComponent>();
    auto &root_shape = root.AddComponent<CollisionShapeComponent>();
    root_shape.m_box_size = glm::vec3(2.0f, 2.0f, 2.0f);

    auto &child_shape = child.AddComponent<CollisionShapeComponent>();
    child_shape.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &nested_rigidbody = child_rigidbody_root.AddComponent<RigidBodyComponent>();
    auto &nested_shape_root = child_rigidbody_root.AddComponent<CollisionShapeComponent>();
    nested_shape_root.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &nested_shape_grandchild = child_rigidbody_grandchild.AddComponent<CollisionShapeComponent>();
    nested_shape_grandchild.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);

    auto &loose_shape = loose.AddComponent<CollisionShapeComponent>();
    loose_shape.m_box_size = glm::vec3(1.0f, 1.0f, 1.0f);
    loose_shape.m_box_center = glm::vec3(1.0f, 0.0f, 0.0f);

    scene.FlushCmdQueue();

    PhysicsScene *physics_scene = scene.GetPhysicsScene();
    if (physics_scene == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene is null.");
        return -1;
    }

    physics_scene->InitializePendingRigidBodies(*cmc->GetRenderSystem());

    physics_scene->DebugPrint();

    const ComputeBuffer *rigid_body_alive = physics_scene->GetGpuRigidBodyAliveBuffer();
    const ComputeBuffer *rigid_body_center_position = physics_scene->GetGpuRigidBodyCenterPositionBuffer();
    const uint32_t rigid_body_slot_count = physics_scene->GetGpuRigidBodySlotCount();
    if (rigid_body_alive == nullptr || rigid_body_center_position == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "PhysicsScene GPU buffers are not initialized.");
        return -1;
    }

    for (int64_t frame = 0; frame < max_frame_count; frame++) {
        XPBDGpuSolver::Step(*cmc->GetRenderSystem(), *physics_scene);

        const auto alive_values = ReadbackU32Buffer(*cmc->GetRenderSystem(), *rigid_body_alive, rigid_body_slot_count);
        const auto position_values =
            ReadbackVec4Buffer(*cmc->GetRenderSystem(), *rigid_body_center_position, rigid_body_slot_count);
        const bool gpu_readback_valid =
            std::any_of(alive_values.begin(), alive_values.end(), [](uint32_t alive) { return alive != 0u; });

        std::cout << "Frame " << frame << " solver output:" << std::endl;
        if (!gpu_readback_valid) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_APPLICATION,
                "Physics registration test GPU readback returned zeroed debug data. Falling back to CPU mirror output."
            );
            const uint32_t cpu_rigid_body_slot_count = physics_scene->GetRigidBodySlotCount();
            for (uint32_t rigid_body_index = 0; rigid_body_index < cpu_rigid_body_slot_count; rigid_body_index++) {
                if (!physics_scene->IsRigidBodyIndexValid(rigid_body_index)) {
                    continue;
                }

                const glm::vec4 &position = physics_scene->GetRigidBodyCenterWorldPosition(rigid_body_index);
                std::cout << "  rigid_body[" << rigid_body_index << "] center = (" << position.x << ", " << position.y
                          << ", " << position.z << ") [cpu-mirror]" << std::endl;
            }
            continue;
        }

        for (uint32_t rigid_body_index = 0; rigid_body_index < rigid_body_slot_count; rigid_body_index++) {
            const glm::vec4 &position = position_values[rigid_body_index];
            std::cout << "  rigid_body[" << rigid_body_index << "] alive=" << alive_values[rigid_body_index]
                      << " center = (" << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
        }
    }

    return 0;
}
