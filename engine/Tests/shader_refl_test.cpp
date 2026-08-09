#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vulkan/vulkan.hpp>

#include "Asset/Shader/ShaderCompiler.h"
#include "Rhi/ShaderInterface.h"
#include "Rhi/ShaderParameterLayout.h"
#include <cmake_config.h>

inline std::vector<uint32_t> GetSpirvBinaryFromGLSL(std::filesystem::path p, EShLanguage shaderType) {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    assert(file.is_open());
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string glsl_code;
    glsl_code.resize(file_size);
    file.read(glsl_code.data(), file_size);
    file.close();

    std::vector<uint32_t> binary{};
    Engine::ShaderCompiler compiler;
    compiler.CompileGLSLtoSPV(binary, glsl_code, shaderType);
    return binary;
}

inline void PrintLayout(const Engine::Rhi::SPLayout &layout) {
    std::cout << "Interfaces: " << std::endl;
    for (const auto &i : layout.interfaces) {
        if (auto ptr = dynamic_cast<const Engine::Rhi::SPInterfaceOpaqueImage *>(i.get())) {
            std::cout << "\t"
                      << std::format(
                             "{}: Set: {}, Binding: {}, Type: Image (size {}, flags {})",
                             i->name,
                             i->layout_set,
                             i->layout_binding,
                             ptr->array_size,
                             static_cast<uint32_t>(ptr->flags)
                         )
                      << std::endl;
        } else if (auto ptr = dynamic_cast<const Engine::Rhi::SPInterfaceOpaqueStorageImage *>(i.get())) {
            std::cout << "\t"
                      << std::format(
                             "{}: Set: {}, Binding: {}, Type: Storage Image (size {})",
                             i->name,
                             i->layout_set,
                             i->layout_binding,
                             ptr->array_size
                         )
                      << std::endl;
        } else if (auto ptr = dynamic_cast<const Engine::Rhi::SPInterfaceBuffer *>(i.get())) {
            std::cout << "\t"
                      << std::format(
                             "{}: Set: {}, Binding: {}, Type: {}",
                             i->name,
                             i->layout_set,
                             i->layout_binding,
                             ptr->type == Engine::Rhi::SPInterfaceBuffer::Type::StorageBuffer ? "SSBO" : "UBO"
                         )
                      << std::endl;
        } else {
            std::cout << "\t"
                      << std::format(
                             "{}: Set: {}, Binding: {}, Type: {}", i->name, i->layout_set, i->layout_binding, "Unknown"
                         )
                      << std::endl;
        }
    }
}

inline void PrintDescriptorSetLayoutBindings(
    const std::unordered_map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> &sets
) {
    for (const auto &kv : sets) {
        std::cout << "Set: " << kv.first << std::endl;
        for (const auto &bd : kv.second) {
            std::cout << std::format(
                "\tBinding {}: {} {}(s)", bd.binding, bd.descriptorCount, to_string(bd.descriptorType)
            ) << std::endl;
        }
    }
}

int main(int argc, char *argv[]) {
    glslang::InitializeProcess();

    auto p = argc == 1 ? std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/pbr_base.vert.0.glsl"
                       : std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders" / argv[1];

    // Get SDL logging working.
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);

    auto binary = GetSpirvBinaryFromGLSL(p, EShLangVertex);
    auto layout1 = Engine::Rhi::SPLayout::Reflect(binary, true);
    std::cout << " - Vertex Shader: " << std::endl;
    PrintLayout(layout1);

    binary = GetSpirvBinaryFromGLSL(
        std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/lambertian_cook_torrance.frag.0.glsl",
        EShLangFragment
    );
    auto layout = Engine::Rhi::SPLayout::Reflect(binary, true);
    std::cout << " - Fragment Shader: " << std::endl;
    PrintLayout(layout);

    layout.Merge(std::move(layout1));
    std::cout << " - Merged Shader: " << std::endl;
    PrintLayout(layout);
    PrintDescriptorSetLayoutBindings(layout.GenerateAllLayoutBindings());

    binary = GetSpirvBinaryFromGLSL(
        std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/fluid.comp.0.glsl", EShLangCompute
    );
    layout = Engine::Rhi::SPLayout::Reflect(binary, false);
    std::cout << " - Compute Shader: " << std::endl;
    PrintLayout(layout);
    PrintDescriptorSetLayoutBindings(layout.GenerateAllLayoutBindings());

    // --- Push-constant reflection ---

    // vec4-only push block -> 16 bytes.
    {
        const std::string glsl = R"(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform Params { vec4 value; } params;
layout(set = 0, binding = 0) buffer OutBuffer { vec4 v[]; } out_buf;
void main() { out_buf.v[0] = params.value; }
)";
        std::vector<uint32_t> spirv{};
        Engine::ShaderCompiler compiler;
        compiler.CompileGLSLtoSPV(spirv, glsl, EShLangCompute);
        auto pc_layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
        std::cout << " - Push-constant vec4 shader: push_constant_size = " << pc_layout.push_constant_size << std::endl;
        assert(pc_layout.push_constant_size == 16u && "vec4 push block must reflect 16 bytes");
    }

    // Scalar-only push block -> 4 bytes.
    {
        const std::string glsl = R"(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform Params { uint count; } params;
layout(set = 0, binding = 0) buffer OutBuffer { uint v[]; } out_buf;
void main() { out_buf.v[0] = params.count; }
)";
        std::vector<uint32_t> spirv{};
        Engine::ShaderCompiler compiler;
        compiler.CompileGLSLtoSPV(spirv, glsl, EShLangCompute);
        auto pc_layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
        std::cout << " - Push-constant scalar shader: push_constant_size = " << pc_layout.push_constant_size << std::endl;
        assert(pc_layout.push_constant_size == 4u && "scalar push block must reflect 4 bytes");
    }

    // Mixed vec4 + scalar push block -> declared size only (20), no struct-level 16 padding.
    {
        const std::string glsl = R"(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform Params { vec4 value; uint count; } params;
layout(set = 0, binding = 0) buffer OutBuffer { uint v[]; } out_buf;
void main() { out_buf.v[0] = params.count; }
)";
        std::vector<uint32_t> spirv{};
        Engine::ShaderCompiler compiler;
        compiler.CompileGLSLtoSPV(spirv, glsl, EShLangCompute);
        auto pc_layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
        std::cout << " - Push-constant mixed shader: push_constant_size = " << pc_layout.push_constant_size << std::endl;
        assert(pc_layout.push_constant_size == 20u && "mixed push block must reflect declared size 20");
    }

    // Mixed vec4 + ivec4 + scalar -> 36, no struct-level 16 padding.
    {
        const std::string glsl = R"(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(push_constant) uniform Params { vec4 a; ivec4 b; uint c; } params;
layout(set = 0, binding = 0) buffer OutBuffer { uint v[]; } out_buf;
void main() { out_buf.v[0] = params.c; }
)";
        std::vector<uint32_t> spirv{};
        Engine::ShaderCompiler compiler;
        compiler.CompileGLSLtoSPV(spirv, glsl, EShLangCompute);
        auto pc_layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
        std::cout << " - Push-constant grid-like shader: push_constant_size = " << pc_layout.push_constant_size << std::endl;
        assert(pc_layout.push_constant_size == 36u && "grid-like push block must reflect declared size 36");
    }

    // No push constants -> 0.
    {
        const std::string glsl = R"(
#version 450 core
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) buffer OutBuffer { uint v[]; } out_buf;
void main() { out_buf.v[0] = 1u; }
)";
        std::vector<uint32_t> spirv{};
        Engine::ShaderCompiler compiler;
        compiler.CompileGLSLtoSPV(spirv, glsl, EShLangCompute);
        auto pc_layout = Engine::Rhi::SPLayout::Reflect(spirv, false);
        std::cout << " - No-push-constant shader: push_constant_size = " << pc_layout.push_constant_size << std::endl;
        assert(pc_layout.push_constant_size == 0u && "shader without push constants must reflect 0");
    }

    glslang::FinalizeProcess();
    return 0;
}
