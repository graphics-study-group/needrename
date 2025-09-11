#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Memory/ShaderParameters/ShaderParameterSimple.h"
#include <cmake_config.h>
#include <format>

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

int main(int argc, char *argv[]) {

    auto p = argc == 1 ? 
        std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders/lambertian_cook_torrance.frag.spv" 
        : std::filesystem::path(ENGINE_BUILTIN_ASSETS_DIR) / "shaders" / argv[1];
    auto binary = ReadSpirvBinary(p);
    auto layout = Engine::ShdrRfl::SPLayout::Reflect(binary);

    std::cout << "Interfaces: " << std::endl;
    for (const auto & i : layout.interfaces) {
        std::cout << "\t" << std::format("Set: {}, Binding: {}", i->layout_set, i->layout_binding) << std::endl;
    }

    std::cout << "Assignables:" << std::endl;
    for (const auto & p : layout.assignable_mapping) {
        auto ptr_simple = dynamic_cast<const Engine::ShdrRfl::SPSimpleAssignable *>(p.second);
        if (ptr_simple) {
            std::cout << "\t" << p.first << "(offset: " << ptr_simple->absolute_offset << ")" << std::endl ;
            std::cout << "\t\t" << std::format(
                "Set: {}, Binding: {}", 
                ptr_simple->parent_interface->layout_set,
                ptr_simple->parent_interface->layout_binding
            ) << std::endl;
        }
        else
            std::cout << "\t" << p.first << std::endl ;
    }
}
