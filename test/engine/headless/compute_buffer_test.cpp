#include "Framework/MainClass.h"
#include "Render/Asset/Shader/ShaderCompiler.h"
#include "Render/FullRenderSystem.h"
#include "Rhi/Pipeline/ComputeKernel.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <cmath>
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

constexpr size_t BUFFER_SIZE = 32;

std::vector<uint32_t> GetSpirvBinaryFromGLSL(const std::string &glsl_code, EShLanguage shaderType) {
    std::vector<uint32_t> binary{};
    Engine::ShaderCompiler compiler;
    compiler.CompileGLSLtoSPV(binary, glsl_code, shaderType);
    return binary;
}

int main(int argc, char *argv[]) {
    int64_t max_frame_count = std::numeric_limits<int64_t>::max();
    if (argc > 1) {
        max_frame_count = std::atoll(argv[1]);
        if (max_frame_count == 0) return -1;
    }

    SDL_Init(SDL_INIT_VIDEO);
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .headless = true, .title = "Compute Buffer Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rsys = cmc->GetRenderSystem();

    std::shared_ptr compbuf1 =
        ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    std::shared_ptr compbuf2 =
        ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    std::random_device seed_rd{};
    std::mt19937 mt{seed_rd()};
    std::uniform_real_distribution<float> urd{};

    // The mapped window keeps one address while it stays open. A caller holds the
    // span across a frame; if the address moved per call, every cached pointer
    // would dangle. Stated as a check rather than assumed, because the contract
    // says it and nothing was testing it.
    {
        float *first = compbuf1->GetVMAddress().data();
        float *second = compbuf1->GetVMAddress().data();
        if (first != second) {
            std::cerr << "Compute buffer test FAILED: the mapped window moved between calls." << std::endl;
            return 1;
        }
        // And an explicit flush over a range is accepted.
        compbuf1->GetComputeBuffer().Flush(0, sizeof(float));
    }
    std::vector<float> written{};

    for (auto &f : compbuf1->GetVMAddress()) {
        f = urd(mt);
        std::cout << f << " ";
        written.push_back(f);
    }
    std::cout << std::endl;

    auto spirv = GetSpirvBinaryFromGLSL(GLSL_CODE, EShLangCompute);
    Rhi::ComputeKernel &kernel =
        rsys->GetDeviceContext().RequestComputeKernel("test/compute-buffer", spirv, "Test Compute Shader");

    RenderGraphBuilder rgb{*rsys};
    auto cbi1 = rgb.ImportExternalResource(compbuf1->GetComputeBuffer());
    auto cbi2 = rgb.ImportExternalResource(compbuf2->GetComputeBuffer());

    rgb.AddPass(
        RenderGraphPassBuilder{*rsys}
            .SetName("Compute")
            .UseBuffer(cbi1, {Rhi::MemoryAccessTypeBufferBits::ShaderRandomRead})
            .UseBuffer(cbi2, {Rhi::MemoryAccessTypeBufferBits::ShaderRandomWrite})
            .SetAffinity(RenderGraphPassAffinity::Compute)
            .SetPassFunction([&kernel, &compbuf1, &compbuf2](CommandBuffer &cb, const RenderGraph &) -> void {
                Rhi::CommandEncoder &encoder = cb.GetEncoder();
                kernel.Dispatch(
                    encoder,
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

    const auto &queues = rsys->GetDeviceInterface().GetQueueInfo();
    auto cbai = vk::CommandBufferAllocateInfo{queues.graphicsPool.get(), vk::CommandBufferLevel::ePrimary, 1};
    auto cb = rsys->GetDevice().allocateCommandBuffers(cbai);
    cb[0].begin(vk::CommandBufferBeginInfo{});
    Rhi::CommandEncoder graph_encoder{Rhi::FromVulkan(cb[0])};
    rg->RecordAllPasses(graph_encoder);
    cb[0].end();

    auto si = vk::SubmitInfo{{}, {}, {cb}, {}};
    queues.graphicsQueue.submit(si);
    rsys->WaitForIdle();

    // The kernel wrote input + 1. The input was written through the window
    // without an explicit flush beyond its first element, so a correct result here
    // is also the implicit-flush contract holding: the submission saw bytes the
    // caller never flushed.
    bool all_correct = true;
    size_t element = 0;
    for (const auto &f : compbuf2->GetVMAddress()) {
        const float expected = written[element] + 1.0f;
        if (std::abs(f - expected) > 1e-5f) {
            std::cerr << "Compute buffer test FAILED: element " << element << " is " << f
                      << " but the shader should have produced " << expected << std::endl;
            all_correct = false;
        }
        element++;
        std::cout << f << " ";
    }
    std::cout << std::endl;
}




