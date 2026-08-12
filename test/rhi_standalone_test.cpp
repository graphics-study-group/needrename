#include "Render/Shader/ShaderCompiler.h"
#include "Rhi/Device/AllocatorState.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Device/MemoryTypes.h"
#include "Rhi/Device/Structs.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <iostream>
#include <vulkan/vulkan.hpp>

using namespace Engine;

constexpr const char GLSL_CODE[] = {
    R"(
#version 450 core

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) writeonly buffer Output {
    uint v[];
} output_buffer;

void main() {
    uint p = gl_GlobalInvocationID.x;
    output_buffer.v[p] = p * 2u;
}
)"
};

constexpr size_t ELEMENT_COUNT = 32;

int main() {
    // Requires the SDL video subsystem for SDL_Vulkan_LoadLibrary(nullptr).
    SDL_Init(SDL_INIT_VIDEO);

    // Standalone headless facilities: no SDLWindow, no RenderSystem, no surface.
    Rhi::DeviceInterface::DeviceConfiguration cfg{
        .window = nullptr,
        .application_name = "Rhi Standalone Test",
        .application_version = 0,
        .dynamic_dispatcher = nullptr,
    };
    Rhi::DeviceInterface gpu_device{cfg};
    Rhi::AllocatorState allocator{gpu_device};
    const auto device = gpu_device.GetDevice();
    const auto &queues = gpu_device.GetQueueInfo();
    assert(device && "Rhi must create a Vulkan device headlessly.");
    assert(queues.graphicsQueue && "Headless device must expose a graphics queue.");

    // Initialize this module's copy of the dynamic dispatch loader (instance
    // first, then device — init(device) alone crashes with a null
    // vkGetDeviceProcAddr DEP violation). Same pattern as RenderSystem::Create.
    VULKAN_HPP_DEFAULT_DISPATCHER.init(gpu_device.GetInstance(), ::vkGetInstanceProcAddr);
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

    // ── Compile the compute shader (ShaderCompiler has no Render dependency) ──
    ShaderCompiler compiler;
    std::vector<uint32_t> spirv{};
    compiler.CompileGLSLtoSPV(spirv, GLSL_CODE, EShLangCompute);
    auto shader_module = device.createShaderModuleUnique(vk::ShaderModuleCreateInfo{{}, spirv});

    // ── Minimal compute pipeline (raw Vulkan, no Render helpers) ──
    vk::DescriptorSetLayoutBinding dslb{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute};
    auto descriptor_set_layout = device.createDescriptorSetLayoutUnique({{}, {dslb}});
    auto pipeline_layout = device.createPipelineLayoutUnique({{}, {descriptor_set_layout.get()}});

    vk::PipelineShaderStageCreateInfo stage{{}, vk::ShaderStageFlagBits::eCompute, shader_module.get(), "main"};
    vk::ComputePipelineCreateInfo pipeline_info{{}, stage, pipeline_layout.get()};
    auto pipeline = device.createComputePipelineUnique(nullptr, pipeline_info);

    vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, 1};
    auto descriptor_pool = device.createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{{}, 1, {pool_size}});
    auto descriptor_set = device.allocateDescriptorSetsUnique(
        vk::DescriptorSetAllocateInfo{descriptor_pool.get(), 1, &descriptor_set_layout.get()}
    );

    // ── Allocate the output buffer via Rhi::AllocatorState (standalone) ──
    // ReadbackFromDevice = CopyTo | HostRandomAccess: shader-writable,
    // automatically host-mapped, invalidatable.
    const size_t buffer_size = ELEMENT_COUNT * sizeof(uint32_t);
    auto buffer = allocator.AllocateBuffer(
        Rhi::BufferType{Rhi::BufferTypeBits::ReadbackFromDevice}, buffer_size, "Standalone output buffer"
    );

    vk::DescriptorBufferInfo descriptor_buffer_info{buffer.GetBuffer(), 0, buffer_size};
    vk::WriteDescriptorSet write_descriptor{
        descriptor_set[0].get(), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_info
    };
    device.updateDescriptorSets({write_descriptor}, {});

    // ── Record and dispatch ──
    auto cb = device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1}
    )[0];
    cb.begin(vk::CommandBufferBeginInfo{});
    cb.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline->get());
    cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline_layout.get(), 0, {descriptor_set[0].get()}, {});
    cb.dispatch(ELEMENT_COUNT / 32, 1, 1);
    cb.end();

    queues.graphicsQueue.submit(vk::SubmitInfo{{}, {}, {cb}, {}});
    queues.graphicsQueue.waitIdle();

    // ── Read back and verify ──
    buffer.InvalidateMemory(0, buffer_size);
    const auto *result = reinterpret_cast<const uint32_t *>(buffer.GetVMAddress());
    bool pass = true;
    for (size_t i = 0; i < ELEMENT_COUNT; i++) {
        if (result[i] != i * 2u) {
            std::cerr << "Mismatch at index " << i << ": expected " << i * 2u << ", got " << result[i] << std::endl;
            pass = false;
            break;
        }
    }

    device.waitIdle();
    if (!pass) {
        std::cerr << "Rhi standalone test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Rhi standalone test PASSED." << std::endl;
    return 0;
}
