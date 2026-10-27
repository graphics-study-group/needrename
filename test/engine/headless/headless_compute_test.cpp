#include "Framework/MainClass.h"
#include "Render/Asset/Shader/ShaderCompiler.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <random>

using namespace Engine;
using namespace Engine::Rhi;

constexpr const char GLSL_CODE[] = {
    R"(
#version 450 core

layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) readonly buffer Input {
    float v[];
} input_buffer;
layout(set = 0, binding = 1) writeonly buffer Output {
    float v[];
} output_buffer;

void main() {
    uint p = gl_GlobalInvocationID.x;
    output_buffer.v[p] = input_buffer.v[p] + 1.0f;
}
)"
};

constexpr const char GLSL_CODE_PUSH[] = {
    R"(
#version 450 core

layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform Params {
    float offset;
} params;

layout(set = 0, binding = 0) readonly buffer Input {
    float v[];
} input_buffer;
layout(set = 0, binding = 1) writeonly buffer Output {
    float v[];
} output_buffer;

void main() {
    uint p = gl_GlobalInvocationID.x;
    output_buffer.v[p] = input_buffer.v[p] + params.offset;
}
)"
};

constexpr size_t BUFFER_SIZE = 32;

std::vector<uint32_t> GetSpirvBinaryFromGLSL(const std::string &glsl_code, EShLanguage shaderType) {
    std::vector<uint32_t> binary{};
    Engine::ShaderCompiler compiler;
    compiler.CompileGLSLtoSPV(binary, glsl_code, shaderType);
    return binary;
}

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Headless Compute Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_INFO);

    auto rsys = cmc->GetRenderSystem();
    assert(cmc->GetWindow() == nullptr && "Headless test should not create a window.");

    std::shared_ptr compbuf1 =
        ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    std::shared_ptr compbuf2 =
        ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    std::random_device seed_rd{};
    std::mt19937 mt{seed_rd()};
    std::uniform_real_distribution<float> urd{};
    for (auto &f : compbuf1->GetVMAddress()) {
        f = urd(mt);
    }

    auto spirv = GetSpirvBinaryFromGLSL(GLSL_CODE, EShLangCompute);
    Rhi::ComputeKernel &ckernel =
        rsys->GetDeviceContext().RequestComputeKernel("test/headless-compute", spirv, "Headless Test Compute Shader");

    RenderGraphBuilder rgb{*rsys};
    auto cbi1 = rgb.ImportExternalResource(compbuf1->GetComputeBuffer());
    auto cbi2 = rgb.ImportExternalResource(compbuf2->GetComputeBuffer());

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Compute")
            .UseBuffer(cbi1, {Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead})
            .UseBuffer(cbi2, {Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite})
            .SetAffinity(RenderGraphPassAffinity::Compute)
            .SetPassFunction([&ckernel, &compbuf1, &compbuf2](CommandBuffer &cb, const RenderGraph &) -> void {
                ckernel.Dispatch(
                    cb.GetCommandBuffer(),
                    {{"Input", Rhi::ComputeKernelResource::Buffer(compbuf1->GetComputeBuffer())},
                     {"Output", Rhi::ComputeKernelResource::Buffer(compbuf2->GetComputeBuffer())}},
                    BUFFER_SIZE / 16 + 1,
                    1,
                    1
                );
            })
            .Get()
    );
    auto rg{rgb.BuildRenderGraph()};

    // --- Push-constant compute pass: output = input + offset ---
    constexpr float PUSH_OFFSET = 3.5f;
    std::shared_ptr compbuf3 =
        ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    auto spirv_push = GetSpirvBinaryFromGLSL(GLSL_CODE_PUSH, EShLangCompute);
    Rhi::ComputeKernel &ckernel_push = rsys->GetDeviceContext().RequestComputeKernel(
        "test/headless-compute-push", spirv_push, "Headless Push-Constant Compute Shader"
    );
    assert(ckernel_push.GetPushConstantSize() == 4u && "Push block must reflect 4 bytes");

    RenderGraphBuilder rgb_push{*rsys};
    auto cbi3 = rgb_push.ImportExternalResource(compbuf3->GetComputeBuffer());
    rgb_push.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("PushConstantCompute")
            .UseBuffer(cbi1, {Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead})
            .UseBuffer(cbi3, {Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite})
            .SetAffinity(RenderGraphPassAffinity::Compute)
            .SetPassFunction(
                [&ckernel_push, &compbuf1, &compbuf3, PUSH_OFFSET](CommandBuffer &cb, const RenderGraph &) -> void {
                    ckernel_push.Dispatch(
                        cb.GetCommandBuffer(),
                        {{"Input", Rhi::ComputeKernelResource::Buffer(compbuf1->GetComputeBuffer())},
                         {"Output", Rhi::ComputeKernelResource::Buffer(compbuf3->GetComputeBuffer())}},
                        BUFFER_SIZE / 16 + 1,
                        1,
                        1,
                        PUSH_OFFSET
                    );
                }
            )
            .Get()
    );
    auto rg_push{rgb_push.BuildRenderGraph()};

    const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
    auto cbai = vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1};
    auto cb = rsys->GetDevice().allocateCommandBuffers(cbai);
    cb[0].begin(vk::CommandBufferBeginInfo{});
    rg->RecordAllPasses(cb[0]);
    cb[0].end();

    auto si = vk::SubmitInfo{{}, {}, {cb}, {}};
    queues.graphicsQueue.submit(si);
    rsys->WaitForIdle();

    // Record and submit the push-constant pass.
    auto cb2 = rsys->GetDevice().allocateCommandBuffers(cbai);
    cb2[0].begin(vk::CommandBufferBeginInfo{});
    rg_push->RecordAllPasses(cb2[0]);
    cb2[0].end();
    auto si2 = vk::SubmitInfo{{}, {}, {cb2}, {}};
    queues.graphicsQueue.submit(si2);
    rsys->WaitForIdle();

    // Verify output == input + 1
    bool pass = true;
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        const auto expected = compbuf1->GetVMAddress()[i] + 1.0f;
        const auto actual = compbuf2->GetVMAddress()[i];
        if (std::abs(actual - expected) > 1e-4f) {
            std::cerr << "Mismatch at index " << i << ": expected " << expected << ", got " << actual << std::endl;
            pass = false;
        }
    }

    // Verify push-constant pass: output == input + PUSH_OFFSET
    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        const auto expected = compbuf1->GetVMAddress()[i] + PUSH_OFFSET;
        const auto actual = compbuf3->GetVMAddress()[i];
        if (std::abs(actual - expected) > 1e-4f) {
            std::cerr << "Push-constant mismatch at index " << i << ": expected " << expected << ", got " << actual
                      << std::endl;
            pass = false;
        }
    }

    rsys->WaitForIdle();
    if (!pass) {
        std::cerr << "Headless compute test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Headless compute test PASSED." << std::endl;
    return 0;
}
