#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>
#include <format>
#include <SDL3/SDL.h>

#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Memory/ShaderParameters/ShaderParameterSimple.h"
#include <cmake_config.h>

inline std::vector <uint32_t> ReadSpirvBinary(std::filesystem::path p) {
    std::vector <uint32_t> binary{};
    // Read SPIR-V code
    std::ifstream file(p, std::ios::binary | std::ios::ate);
    assert(file.is_open());
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    printf("Loading shader %s, size %zu bytes\n", p.string().c_str(), file_size);
    binary.resize((file_size - sizeof(uint32_t) + 1) / sizeof(uint32_t) + 1);
    file.read(reinterpret_cast<char *>(binary.data()), file_size);
    file.close();
    return binary;
}

inline void PrintLayout(const Engine::ShdrRfl::SPLayout & layout) {
    std::cout << "Interfaces: " << std::endl;
    for (const auto & i : layout.interfaces) {
        std::cout << "\t" << std::format(
            "Set: {}, Binding: {}, Type: {}", 
            i->layout_set, 
            i->layout_binding, 
            static_cast<int>(i->type)
        ) << std::endl;
    }

    std::cout << "Assignables:" << std::endl;
    for (const auto & p : layout.name_mapping) {
        std::cout << "\t" << p.first << std::endl;
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

int main(int argc, char *argv[]) {

    auto p = argc == 1 ? 
        std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/lambertian_cook_torrance.frag.spv" 
        : std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders" / argv[1];

    // Get SDL logging working.
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
    
    auto binary = ReadSpirvBinary(p);
    auto layout = Engine::ShdrRfl::SPLayout::Reflect(binary, false);
    PrintLayout(layout);

    binary = ReadSpirvBinary(std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/pbr_base.vert.spv");
    layout.Merge(Engine::ShdrRfl::SPLayout::Reflect(binary, false));
    PrintLayout(layout);
}
