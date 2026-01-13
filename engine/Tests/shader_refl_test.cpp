#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <format>
#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

#include "Asset/Shader/ShaderCompiler.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Memory/ShaderParameters/ShaderParameterSimple.h"
#include <cmake_config.h>

inline std::vector <uint32_t> GetSpirvBinaryFromGLSL(std::filesystem::path p, EShLanguage shaderType) {
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    assert(file.is_open());
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::string glsl_code;
    glsl_code.resize(file_size);
    file.read(glsl_code.data(), file_size);
    file.close();

    std::vector <uint32_t> binary{};
    Engine::ShaderCompiler compiler;
    compiler.CompileGLSLtoSPV(glsl_code, shaderType, binary);
    return binary;
}

inline void PrintLayout(const Engine::ShdrRfl::SPLayout & layout) {
    std::cout << "Interfaces: " << std::endl;
    for (const auto & i : layout.interfaces) {
        if (auto ptr = dynamic_cast<const Engine::ShdrRfl::SPInterfaceOpaqueImage *>(i)) {
            std::cout << "\t" << std::format(
                "{}: Set: {}, Binding: {}, Type: Image (size {}, flags {})",
                i->name,
                i->layout_set, 
                i->layout_binding, 
                ptr->array_size,
                static_cast<uint32_t>(ptr->flags)
            ) << std::endl;
        } else if (auto ptr = dynamic_cast<const Engine::ShdrRfl::SPInterfaceOpaqueStorageImage *>(i)) {
            std::cout << "\t" << std::format(
                "{}: Set: {}, Binding: {}, Type: Storage Image (size {})", 
                i->name,
                i->layout_set, 
                i->layout_binding, 
                ptr->array_size
            ) << std::endl;
        } else if (auto ptr = dynamic_cast<const Engine::ShdrRfl::SPInterfaceBuffer *>(i)) {
            std::cout << "\t" << std::format(
                "{}: Set: {}, Binding: {}, Type: {}",
                i->name,
                i->layout_set, 
                i->layout_binding, 
                ptr->type == Engine::ShdrRfl::SPInterfaceBuffer::Type::StorageBuffer ? "SSBO" : "UBO"
            ) << std::endl;
            if (auto tptr = dynamic_cast<const Engine::ShdrRfl::SPTypeSimpleStruct *>(ptr->underlying_type)) {
                std::cout << "\t\tTakes up " << tptr->expected_size << " bytes" << std::endl ;
            }
        } else {
            std::cout << "\t" << std::format(
                "{}: Set: {}, Binding: {}, Type: {}",
                i->name,
                i->layout_set, 
                i->layout_binding, 
                "Unknown"
            ) << std::endl;
        }
    }

    std::cout << "Assignables:" << std::endl;
    for (const auto & p : layout.name_mapping) {
        std::cout << "\t" << p.first << " (" << p.second->name << ")" << std::endl;
        auto ptr_simple = dynamic_cast<const Engine::ShdrRfl::SPAssignableInterface *>(p.second);
        if (ptr_simple) {
            std::cout << "\t\t" << std::format(
                "Contained in set {}, binding {}, offset {}", 
                ptr_simple->parent_interface->layout_set,
                ptr_simple->parent_interface->layout_binding,
                ptr_simple->absolute_offset
            ) << std::endl;
        } else {
            auto ptr_interface = dynamic_cast<const Engine::ShdrRfl::SPInterface *>(p.second);
            if (ptr_interface) {
                std::cout << "\t\t" << std::format(
                    "Occupies descriptor set {}, binding {}",
                    ptr_interface->layout_set,
                    ptr_interface->layout_binding
                ) << std::endl ;
            }
        }
    }
}

inline void PrintDescriptorSetLayoutBindings (const std::unordered_map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> & sets)
{
    for (const auto & kv : sets) {
        std::cout << "Set: " << kv.first << std::endl;
        for (const auto & bd : kv.second) {
            std::cout << std::format(
                "\tBinding {}: {} {}(s)",
                bd.binding,
                bd.descriptorCount,
                to_string(bd.descriptorType)
            ) << std::endl;
        }
    }
}

int main(int argc, char *argv[]) {
    glslang::InitializeProcess();

    auto p = argc == 1 ? 
        std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/pbr_base.vert.0.glsl" 
        : std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders" / argv[1];

    // Get SDL logging working.
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    
    auto binary = GetSpirvBinaryFromGLSL(p, EShLangVertex);
    auto layout = Engine::ShdrRfl::SPLayout::Reflect(binary, true);
    PrintLayout(layout);

    binary = GetSpirvBinaryFromGLSL(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/lambertian_cook_torrance.frag.0.glsl", EShLangFragment);
    layout.Merge(Engine::ShdrRfl::SPLayout::Reflect(binary, true));
    PrintLayout(layout);
    PrintDescriptorSetLayoutBindings(layout.GenerateAllLayoutBindings());

    binary = GetSpirvBinaryFromGLSL(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/fluid.comp.0.glsl", EShLangCompute);
    layout = Engine::ShdrRfl::SPLayout::Reflect(binary, false);
    PrintLayout(layout);
    PrintDescriptorSetLayoutBindings(layout.GenerateAllLayoutBindings());

    glslang::FinalizeProcess();
    return 0;
}
