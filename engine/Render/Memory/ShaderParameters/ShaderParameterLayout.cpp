#include "ShaderParameterLayout.h"

// CMake is messing with the SPIRV-Cross in Vulkan SDK
#include "../../../../third_party/SPIRV-Cross/spirv_cross.hpp"
#include <SDL3/SDL.h>

namespace {
    void ReflectSimpleStruct(
        Engine::ShdrRfl::SPLayout & layout,
        Engine::ShdrRfl::SPInterface & root,
        const spirv_cross::Resource & buffer,
        const spirv_cross::Compiler & compiler
    ) {
        using namespace Engine::ShdrRfl;
        // Construct the underlying type
        auto struct_type_ptr = std::unique_ptr<SPTypeSimpleStruct>(new SPTypeSimpleStruct{});
        auto &type = compiler.get_type(buffer.base_type_id);

        struct_type_ptr->expected_size = compiler.get_declared_struct_size(type);
        assert(struct_type_ptr->expected_size > 0 && "Dynamic array not supported.");

        unsigned member_count = type.member_types.size();
        for (unsigned i = 0; i < member_count; i++)
        {
            auto &member_type = compiler.get_type(type.member_types[i]);
            assert(
                (member_type.basetype == spirv_cross::SPIRType::Float 
                || member_type.basetype == spirv_cross::SPIRType::Int 
                || member_type.basetype == spirv_cross::SPIRType::UInt
                ) && "Unsupported member type."
            );

            if (!member_type.array.empty()){
                assert(!"Array type is unsupported.");
            }

            // Get member offset within this struct.
            size_t offset = compiler.type_struct_member_offset(type, i);
            const auto &name = compiler.get_member_name(type.self, i);

            if (member_type.basetype == spirv_cross::SPIRType::Struct) {
                // Recursive structure...
                assert(!"Recursive struct not supported.");
            } if (member_type.columns > 1) {
                // This is a matrix
                assert(member_type.vecsize == 4 && member_type.columns == 4);
                assert(member_type.basetype == spirv_cross::SPIRType::Float);
                auto mat_ptr = std::unique_ptr <SPMat4>(new SPMat4{});
                mat_ptr->absolute_offset = offset;
                mat_ptr->parent_interface = &root;

                struct_type_ptr->members.push_back(mat_ptr.get());
                if (layout.assignable_mapping.contains(name)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name: %s", name.c_str());
                }
                layout.assignable_mapping[name] = mat_ptr.get();

                layout.variables.emplace_back(std::move(mat_ptr));
            } else if (member_type.vecsize > 1) {
                // This is a vector
                assert(member_type.vecsize == 4);
                assert(member_type.basetype == spirv_cross::SPIRType::Float);
                auto vec_ptr = std::unique_ptr <SPVec4>(new SPVec4{});
                vec_ptr->absolute_offset = offset;
                vec_ptr->parent_interface = &root;

                struct_type_ptr->members.push_back(vec_ptr.get());
                if (layout.assignable_mapping.contains(name)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name: %s", name.c_str());
                }
                layout.assignable_mapping[name] = vec_ptr.get();

                struct_type_ptr->members.push_back(vec_ptr.get());
                layout.variables.emplace_back(std::move(vec_ptr));
            } else {
                // This is a scalar
                auto vptr = std::unique_ptr <SPScalar>(new SPScalar{});
                vptr->absolute_offset = offset;
                switch(member_type.basetype) {
                case spirv_cross::SPIRType::Float:
                    vptr->type = SPScalar::Type::Float;
                    break;
                case spirv_cross::SPIRType::Int:
                    vptr->type = SPScalar::Type::Sint;
                    break;
                case spirv_cross::SPIRType::UInt:
                    vptr->type = SPScalar::Type::Uint;
                    break;
                default:
                    assert(!"Unimplemented type.");
                }
                vptr->parent_interface = &root;

                struct_type_ptr->members.push_back(vptr.get());
                if (layout.assignable_mapping.contains(name)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name: %s", name.c_str());
                }
                layout.assignable_mapping[name] = vptr.get();

                struct_type_ptr->members.push_back(vptr.get());
                layout.variables.emplace_back(std::move(vptr));
            }
        }
        layout.types.emplace_back(std::move(struct_type_ptr));
        root.underlying_type = struct_type_ptr.get();
    }
}

namespace Engine::ShdrRfl {
    SPLayout Engine::ShdrRfl::SPLayout::Reflect(const std::vector<uint32_t> &spirv_code) {
        SPLayout layout{};
        spirv_cross::Compiler compiler(spirv_code);
        
        // Get all interfaces (a.k.a. resources)
        auto shader_resources = compiler.get_shader_resources();

        // Combined image samplers
        for (auto image : shader_resources.sampled_images) {
            auto ptr = std::unique_ptr<SPInterface>(
                    new SPInterface{
                        .layout_set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet),
                        .layout_binding = compiler.get_decoration(image.id, spv::DecorationBinding),
                        .type = SPInterface::Type::TextureCombinedSampler,
                        .underlying_type = nullptr
                    }
                );
            layout.interfaces.push_back(ptr.get());
            if (layout.assignable_mapping.contains(image.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", image.name.c_str());
            }
            layout.assignable_mapping[image.name] = ptr.get();
            layout.variables.emplace_back(std::move(ptr));
        }

        // Storage images
        for (auto image : shader_resources.storage_images) {
            auto ptr = std::unique_ptr<SPInterface>(
                    new SPInterface{
                        .layout_set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet),
                        .layout_binding = compiler.get_decoration(image.id, spv::DecorationBinding),
                        .type = SPInterface::Type::Image,
                        .underlying_type = nullptr
                    }
                );
            layout.interfaces.push_back(ptr.get());
            if (layout.assignable_mapping.contains(image.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", image.name.c_str());
            }
            layout.assignable_mapping[image.name] = ptr.get();
            layout.variables.emplace_back(std::move(ptr));
        }

        // UBOs
        for (auto ubo : shader_resources.uniform_buffers) {
            auto buffer_type_ptr = std::unique_ptr<SPInterface>(new SPInterface{});
            buffer_type_ptr->type = SPInterface::UniformBuffer;

            ReflectSimpleStruct(layout, *buffer_type_ptr, ubo, compiler);

            buffer_type_ptr->layout_set = compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
            buffer_type_ptr->layout_binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
            layout.variables.emplace_back(std::move(buffer_type_ptr));
        }

        // SSBOs
        for (auto ssbo : shader_resources.storage_buffers) {
            auto buffer_type_ptr = std::unique_ptr<SPInterface>(new SPInterface{});
            buffer_type_ptr->type = SPInterface::StorageBuffer;

            ReflectSimpleStruct(layout, *buffer_type_ptr, ssbo, compiler);

            buffer_type_ptr->layout_set = compiler.get_decoration(ssbo.id, spv::DecorationDescriptorSet);
            buffer_type_ptr->layout_binding = compiler.get_decoration(ssbo.id, spv::DecorationBinding);
            layout.variables.emplace_back(std::move(buffer_type_ptr));
        }

        return layout;
    }
}
