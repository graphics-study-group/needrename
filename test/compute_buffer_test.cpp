#include "MainClass.h"
#include "Render/FullRenderSystem.h"
#include "Asset/Shader/ShaderCompiler.h"
#include <random>
#include <SDL3/SDL.h>
#include <iostream>

using namespace Engine;

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

std::vector <uint32_t> GetSpirvBinaryFromGLSL(const std::string & glsl_code, EShLanguage shaderType) {
    std::vector <uint32_t> binary{};
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
    StartupOptions opt{.resol_x = 1280, .resol_y = 720, .title = "Compute Shader Test"};
    auto cmc = MainClass::GetInstance();
    cmc->Initialize(&opt, SDL_INIT_VIDEO, SDL_LOG_PRIORITY_VERBOSE);

    auto rsys = cmc->GetRenderSystem();

    std::shared_ptr compbuf1 = ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    std::shared_ptr compbuf2 = ComputeBufferTyped<float>::CreateUniqueTyped(rsys->GetAllocatorState(), BUFFER_SIZE, true, false, false, false);
    std::random_device seed_rd{};
    std::mt19937 mt{seed_rd()};
    std::uniform_real_distribution<float> urd{};
    for (auto & f : compbuf1->GetVMAddress()) {
        f = urd(mt);
        std::cout << f << " ";
    }
    std::cout << std::endl;


    auto spirv = GetSpirvBinaryFromGLSL(GLSL_CODE, EShLangCompute);
    auto cstage = ComputeStage{*rsys};
    cstage.Instantiate(spirv, "Test Compute Shader");
    cstage.AssignComputeBuffer("Input", compbuf1->GetComputeBuffer());
    cstage.AssignComputeBuffer("Output", compbuf2->GetComputeBuffer());

    RenderGraphBuilder rgb{*rsys};
    rgb.UseBuffer(compbuf1->GetComputeBuffer(), {MemoryAccessTypeBufferBits::ShaderRandomRead});
    rgb.UseBuffer(compbuf2->GetComputeBuffer(), {MemoryAccessTypeBufferBits::ShaderRandomWrite});
    rgb.RecordComputePass([&cstage](ComputeCommandBuffer & ccb) -> void {
        ccb.BindComputeStage(cstage);
        ccb.DispatchCompute(BUFFER_SIZE / 16 + 1, 1, 1);
    });
    rgb.UseBuffer(compbuf2->GetComputeBuffer(), {MemoryAccessTypeBufferBits::HostAccess});
    rgb.RecordSynchronization();
    auto rg{rgb.BuildRenderGraph()};

    const auto & queues = rsys->GetDeviceInterface().GetQueueInfo();
    auto cbai = vk::CommandBufferAllocateInfo{
        queues.graphicsPool.get(), 
        vk::CommandBufferLevel::ePrimary,
        1
    };
    auto cb = rsys->GetDevice().allocateCommandBuffers(cbai);
    rg->Record(cb[0]);

    auto si = vk::SubmitInfo{{}, {}, {cb}, {}};
    queues.graphicsQueue.submit(si);
    queues.graphicsQueue.waitIdle();

    for (const auto & f : compbuf2->GetVMAddress()) {
        std::cout << f << " ";
    }
    std::cout << std::endl;
}
