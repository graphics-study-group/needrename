#include "Rhi/Buffer/StructuredBuffer.h"
#include "Rhi/Buffer/StructuredBufferPlacer.h"
#include <Render/Asset/Shader/ShaderCompiler.h>
#include <Rhi/Pipeline/ShaderInterface.h>
#include <Rhi/Pipeline/ShaderParameterLayout.h>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <format>
#include <glm.hpp>
#include <gtc/type_ptr.hpp>
#include <iostream>
#include <span>
#include <string>
#include <vector>

// Deliberately NOT packed: these layouts are identical on x64 MSVC, Clang and
// GCC (uint32/double/float natural alignment), and the trailing padding on
// sub_buffer is exactly what StructuredBufferPlacer used to under-report.
struct sub_buffer {
    uint32_t v1;
    double v2;
    float v3[3];
};

struct super_buffer {
    uint64_t v1;
    sub_buffer v2;
    float v3[16];
};

// No trailing padding: required size equals block size.
struct aligned_buffer {
    double a;
    double b;
};

namespace {
    const Engine::Rhi::StructuredBufferPlacer *FindStructuredBufferPlacer(
        const Engine::Rhi::SPLayout &layout, uint32_t binding
    ) {
        for (const auto &i : layout.interfaces) {
            auto sb = dynamic_cast<const Engine::Rhi::SPInterfaceStructuredBuffer *>(i.get());
            if (sb != nullptr && sb->layout_binding == binding) {
                return sb->buffer_placer;
            }
        }
        return nullptr;
    }
} // namespace

int main() {
    using namespace Engine;
    Rhi::StructuredBufferPlacer sub_placer, super_placer, aligned_placer;

    sub_placer.AddVariable<uint32_t>("v1", offsetof(sub_buffer, v1));
    sub_placer.AddVariable<double>("v2", offsetof(sub_buffer, v2));
    sub_placer.AddVariable<float[3]>("v3", offsetof(sub_buffer, v3));
    sub_placer.SetBlockSize(sizeof(sub_buffer));

    super_placer.AddVariable<uint64_t>("v1", offsetof(super_buffer, v1));
    super_placer.AddStructuredBuffer("v2", offsetof(super_buffer, v2), sub_placer);
    super_placer.AddVariable<float[16]>("v3", offsetof(super_buffer, v3));
    super_placer.SetBlockSize(sizeof(super_buffer));

    aligned_placer.AddVariable<double>("a", offsetof(aligned_buffer, a));
    aligned_placer.AddVariable<double>("b", offsetof(aligned_buffer, b));
    aligned_placer.SetBlockSize(sizeof(aligned_buffer));

    // Exact sizes (see design): required is the last member end, block includes trailing padding.
    assert(sub_placer.GetRequiredSize() == 28);
    assert(sub_placer.GetBlockSize() == sizeof(sub_buffer));
    assert(sizeof(sub_buffer) == 32);
    assert(super_placer.GetRequiredSize() == 104);
    assert(super_placer.GetBlockSize() == sizeof(super_buffer));
    assert(sizeof(super_buffer) == 104);
    assert(aligned_placer.GetRequiredSize() == sizeof(aligned_buffer));
    assert(aligned_placer.GetBlockSize() == sizeof(aligned_buffer));

    // Invariant: the block size must cover the required write footprint.
    assert(sub_placer.GetRequiredSize() <= sub_placer.GetBlockSize());
    assert(super_placer.GetRequiredSize() <= super_placer.GetBlockSize());
    assert(aligned_placer.GetRequiredSize() <= aligned_placer.GetBlockSize());

    std::cout << std::format(
        "sub placer required {}, block {}, sizeof {}",
        sub_placer.GetRequiredSize(),
        sub_placer.GetBlockSize(),
        sizeof(sub_buffer)
    ) << std::endl;
    std::cout << std::format(
        "super placer required {}, block {}, sizeof {}",
        super_placer.GetRequiredSize(),
        super_placer.GetBlockSize(),
        sizeof(super_buffer)
    ) << std::endl;

    Rhi::StructuredBuffer subsb, supersb;
    glm::vec3 v{1.0f, 2.0f, 3.0f};
    glm::mat4 m{1.0f};

    subsb.SetVariable<uint32_t>("v1", 0x55AA55AA);
    subsb.SetVariable<double>("v2", 1e9);
    subsb.SetVariable<const float, 3>("v3", std::span<const float, 3>{glm::value_ptr(v), glm::value_ptr(v) + 3});

    supersb.SetVariable<uint64_t>("v1", 0xAA55AA55);
    supersb.SetStructuredBuffer("v2", subsb);
    supersb.SetVariable<const float, 16>("v3", std::span<const float, 16>{glm::value_ptr(m), glm::value_ptr(m) + 16});

    // The staging overload resizes to exactly the required size.
    std::vector<std::byte> staging;
    sub_placer.WriteBuffer(subsb, staging);
    assert(staging.size() == sub_placer.GetRequiredSize());

    // Write into a buffer sized exactly by the block size; any write past the block
    // would be out of bounds here (rather than hidden by an oversized scratch buffer).
    {
        std::vector<std::byte> buffer(sub_placer.GetBlockSize());
        sub_placer.WriteBuffer(subsb, buffer.data());

        sub_buffer sb;
        std::memcpy(&sb, buffer.data(), sizeof(sb));
        assert(sb.v1 == 0x55AA55AA);
        assert(sb.v2 == 1e9);
        assert(sb.v3[0] == 1.0f && sb.v3[1] == 2.0f && sb.v3[2] == 3.0f);
    }

    {
        std::vector<std::byte> buffer(super_placer.GetBlockSize());
        super_placer.WriteBuffer(supersb, buffer.data());

        super_buffer supb;
        std::memcpy(&supb, buffer.data(), sizeof(supb));
        assert(supb.v1 == 0xAA55AA55);
        assert(supb.v2.v1 == 0x55AA55AA);
        assert(supb.v2.v2 == 1e9);
        assert(supb.v2.v3[0] == 1.0f && supb.v2.v3[1] == 2.0f && supb.v2.v3[2] == 3.0f);
    }

    // --- Reflection regression: std140 block size rounding ---
    // A block whose last member ends short of its base alignment gets trailing
    // padding in the block size but not in the required size.
    {
        const std::string glsl = R"(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(std140, set = 0, binding = 0) uniform PaddedBlock { vec4 a; float b; } padded_block;
layout(std140, set = 0, binding = 1) uniform ScalarBlock { uint count; } scalar_block;
layout(std140, set = 0, binding = 2) uniform VecBlock { vec4 a; mat4 m; } vec_block;
void main() {
    vec4 x = padded_block.a + vec4(padded_block.b)
           + vec4(float(scalar_block.count))
           + vec_block.a + vec_block.m[0];
    if (x.x != 0.0) { return; }
}
)";
        glslang::InitializeProcess();
        std::vector<uint32_t> spirv{};
        ShaderCompiler compiler;
        if (!compiler.CompileGLSLtoSPV(spirv, glsl, EShLangCompute)) {
            std::cerr << "Failed to compile reflection test GLSL.\n";
            glslang::FinalizeProcess();
            return 1;
        }
        auto layout = Rhi::SPLayout::Reflect(spirv, /*filter_out_low_descriptors=*/false);

        const auto *padded = FindStructuredBufferPlacer(layout, 0);
        const auto *scalar = FindStructuredBufferPlacer(layout, 1);
        const auto *vec_block = FindStructuredBufferPlacer(layout, 2);
        assert(padded != nullptr && scalar != nullptr && vec_block != nullptr);

        assert(padded->GetRequiredSize() == 20);
        assert(padded->GetBlockSize() == 32);
        assert(scalar->GetRequiredSize() == 4);
        assert(scalar->GetBlockSize() == 4);
        assert(vec_block->GetRequiredSize() == 80);
        assert(vec_block->GetBlockSize() == 80);

        glslang::FinalizeProcess();
    }

    return 0;
}
