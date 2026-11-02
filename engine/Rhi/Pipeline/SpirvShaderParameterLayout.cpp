#include "Rhi/Pipeline/ShaderParameterLayout.h"

#include "Rhi/Buffer/StructuredBuffer.h"
#include "Rhi/Texture/ImageUtils.h"
#include "Rhi/Buffer/StructuredBufferPlacer.h"

// CMake is messing with the SPIRV-Cross in Vulkan SDK
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "../../../third_party/SPIRV-Cross/spirv_cross.hpp"
#pragma GCC diagnostic pop
#include <SDL3/SDL.h>

#include <algorithm>
#include <string>

namespace Engine::Rhi {
    // Types are only added and never removed.
    // We will have no more than ~100 shaders, so this
    // might be Ok anyway.
    struct TypeStorage {
        std::vector<std::unique_ptr<StructuredBufferPlacer>> structured_buffers;
    };
    static TypeStorage type_storage;
} // namespace Engine::Rhi

namespace {
    void ReflectSimpleStruct(
        Engine::Rhi::SPLayout &layout,
        Engine::Rhi::SPInterfaceStructuredBuffer &root,
        const spirv_cross::Resource &buffer,
        const spirv_cross::Compiler &compiler
    ) {
        using namespace Engine;
        using namespace Engine::Rhi;
        // Construct the underlying type
        auto placer = std::unique_ptr<StructuredBufferPlacer>(new StructuredBufferPlacer{});
        auto &type = compiler.get_type(buffer.base_type_id);

        unsigned member_count = type.member_types.size();

        // std140 gives vec4/matrix members a 16-byte base alignment, so a block
        // containing any of them is padded up to a 16-byte boundary.
        bool has_non_scalar_member = false;

        // For every member
        for (unsigned i = 0; i < member_count; i++) {
            auto &member_type = compiler.get_type(type.member_types[i]);
            const auto &name = compiler.get_member_name(type.self, i);
            const size_t offset = compiler.type_struct_member_offset(type, i);

            // A member the placer cannot describe is reported and left out rather than
            // asserting: a block reached through the reflection of a whole module can hold
            // members the engine never writes (nested structs and arrays in the scene block,
            // for instance), and refusing to reflect the module at all would make the bake of
            // a shader depend on which other shaders happen to share its include tree.
            if (member_type.basetype == spirv_cross::SPIRType::Struct
                || !member_type.array.empty()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "SPLayout: member '%s' of the block '%s' is a %s, which the CPU-side placer does "
                    "not describe; a caller writing it gets nothing placed.",
                    name.c_str(),
                    root.name.c_str(),
                    member_type.basetype == spirv_cross::SPIRType::Struct ? "nested structure" : "array"
                );
                continue;
            }
            if (member_type.basetype != spirv_cross::SPIRType::Float
                && member_type.basetype != spirv_cross::SPIRType::Int
                && member_type.basetype != spirv_cross::SPIRType::UInt) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "SPLayout: member '%s' of the block '%s' has a base type the placer does not "
                    "handle; it is left out.",
                    name.c_str(),
                    root.name.c_str()
                );
                continue;
            }

            if (member_type.columns > 1) {
                // This is a matrix
                assert(member_type.vecsize == 4 && member_type.columns == 4);
                has_non_scalar_member = true;
                placer->AddVariable<float[16]>(root.name + "::" + name, offset);
            } else if (member_type.vecsize > 1) {
                // This is a vector
                assert(member_type.vecsize == 4);
                has_non_scalar_member = true;
                placer->AddVariable<float[4]>(root.name + "::" + name, offset);
            } else {
                // This is a scalar
                const auto qualified_name = root.name + "::" + name;
                switch (member_type.basetype) {
                case spirv_cross::SPIRType::Float:
                    placer->AddVariable<float>(qualified_name, offset);
                    break;
                case spirv_cross::SPIRType::Int:
                    placer->AddVariable<int32_t>(qualified_name, offset);
                    break;
                case spirv_cross::SPIRType::UInt:
                    placer->AddVariable<uint32_t>(qualified_name, offset);
                    break;
                default:
                    assert(!"Unimplemented type.");
                }
            }
        }

        // Declare the std140 block size (get_declared_struct_size returns only
        // the last member end, without trailing padding; pad it to the struct's
        // base alignment so UBO allocations and descriptor ranges cover the block).
        if (member_count > 0) {
            const size_t declared_size = compiler.get_declared_struct_size(type);
            const size_t struct_alignment = has_non_scalar_member ? 16 : 4;
            const size_t block_size = (declared_size + struct_alignment - 1) & ~(struct_alignment - 1);
            placer->SetBlockSize(block_size);
        }

        root.buffer_placer = placer.get();
        type_storage.structured_buffers.emplace_back(std::move(placer));
    }

    void FillImageInfo(Engine::Rhi::SPInterfaceOpaqueImage &image, const spirv_cross::SPIRType &type) {
        if (!type.array.empty()) {
            assert(type.array.size() == 1);
            image.array_size = type.array[0];
        } else {
            image.array_size = 0;
        }

        using enum Engine::Rhi::SPInterfaceOpaqueImage::ImageFlagBits;
        if (type.image.arrayed) image.flags.Set(Arrayed);
        if (type.image.ms) image.flags.Set(Multisampled);
        // A depth image is declared with a comparison sampler in the shader. A backend that
        // states the binding kind has to distinguish it from colour data, so the reflection
        // records it rather than letting the layout guess.
        if (type.image.depth) image.flags.Set(Depth);
        switch (type.image.dim) {
        case spv::Dim1D:
            image.flags.Set(d1D);
            break;
        case spv::Dim2D:
            image.flags.Set(d2D);
            break;
        case spv::Dim3D:
            image.flags.Set(d3D);
            break;
        case spv::DimCube:
            // In Vulkan Cubemap is simply arrayed 2D texture
            image.flags.Set(d2D);
            image.flags.Set(Arrayed);
            image.flags.Set(CubeMap);
            break;
        default:
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Unidentified image dimension.");
        }
    }

    /// @brief Translate the image format a storage image declares.
    Engine::Rhi::ImageFormat StorageImageFormat(spv::ImageFormat format) {
        using namespace Engine::Rhi;
        switch (format) {
        case spv::ImageFormatRgba8:
            return ImageFormat::R8G8B8A8UNorm;
        case spv::ImageFormatRgba8Snorm:
            return ImageFormat::R8G8B8A8SNorm;
        case spv::ImageFormatRgba32f:
            return ImageFormat::R32G32B32A32SFloat;
        case spv::ImageFormatR32f:
            return ImageFormat::R32SFloat;
        case spv::ImageFormatR11fG11fB10f:
            return ImageFormat::R11G11B10UFloat;
        case spv::ImageFormatR32ui:
            return ImageFormat::R32UInt;
        case spv::ImageFormatRg32ui:
            return ImageFormat::R16G16UInt;
        default:
            return ImageFormat::UNDEFINED;
        }
    }

    /// @brief Translate a reflected storage image into the portable terms a layout states.

    /// @brief Translate a reflected image's flags into the portable terms a layout states.
} // namespace

/**
 * @todo Differentiating whether a variable can be assigned directly by its type
 * is making the whole interface unnecessarily complex. We should first process
 * all types and variables in the shader, and determine whether they can be directly
 * assigned or not after that.
 *
 * We should then try to model the type system of GLSL with only five components:
 * 1. simple types (e.g. float, int, uint, etc.);
 * 2. vector types (e.g. vector of 3 ints, matrix of 3x3 floats);
 * 3. array types;
 * 4. composed types (e.g. structure);
 * 5. opaque types (e.g. image).
 *
 * When reflecting the SPIR-V codes, we process the resources in a from-top-to-bottom
 * fashion, starting with interfaces exposed as UBO or SSBO and process recursively.
 * On the way back from leave nodes, we can determine whether a variable can be
 * assigned or not by looking at its type and name.
 *
 * In the same line of thought, interfaces and type information should therefore not
 * be intermingled. This can save us a lot of headaches.
 */
namespace Engine::Rhi {





    SPLayout Engine::Rhi::SPLayout::Reflect(const std::vector<uint32_t> &spirv_code, bool filter_out_low_descriptors) {
        SPLayout layout{};
        spirv_cross::Compiler compiler(spirv_code);

        // Get all interfaces (a.k.a. resources)
        auto shader_resources = compiler.get_shader_resources();

        // Combined image samplers
        for (auto image : shader_resources.sampled_images) {
            auto desc_set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
            if (filter_out_low_descriptors && desc_set < 2) continue;

            auto ptr = std::unique_ptr<SPInterfaceOpaqueImage>(new SPInterfaceOpaqueImage());
            ptr->name = image.name;
            ptr->layout_set = desc_set;
            ptr->layout_binding = compiler.get_decoration(image.id, spv::DecorationBinding);
            ptr->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::HasSampler);

            const auto &type = compiler.get_type(image.type_id);
            FillImageInfo(*ptr, type);

            if (layout.interface_name_mapping.contains(image.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", image.name.c_str());
            }
            layout.interface_name_mapping[image.name] = ptr.get();
            layout.interfaces.push_back(std::move(ptr));
        }

        // Storage images
        for (auto image : shader_resources.storage_images) {
            auto desc_set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
            if (filter_out_low_descriptors && desc_set < 2) continue;

            auto ptr = std::unique_ptr<SPInterfaceOpaqueStorageImage>(new SPInterfaceOpaqueStorageImage());
            ptr->name = image.name;
            ptr->layout_set = desc_set;
            ptr->layout_binding = compiler.get_decoration(image.id, spv::DecorationBinding);

            // A storage image's format and access are part of its declaration in GLSL, and a
            // backend whose binding description names them cannot derive either from the
            // texture. `readonly` arrives as `NonWritable` on the variable; `writeonly` as
            // `NonReadable`, and a declaration with neither is read-write.
            const auto &storage_type = compiler.get_type(image.type_id);
            ptr->format = static_cast<uint32_t>(StorageImageFormat(storage_type.image.format));
            ptr->read_only = compiler.has_decoration(image.id, spv::DecorationNonWritable)
                             && !compiler.has_decoration(image.id, spv::DecorationNonReadable);

            if (layout.interface_name_mapping.contains(image.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", image.name.c_str());
            }
            layout.interface_name_mapping[image.name] = ptr.get();
            layout.interfaces.push_back(std::move(ptr));
        }

        // Other opaque types are currently unsupported
        if (!shader_resources.separate_images.empty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Separate images are unsupported.");
        }
        if (!shader_resources.separate_samplers.empty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Separate samplers are unsupported.");
        }
        if (!shader_resources.atomic_counters.empty()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Vulkan SPIR-V does not support atomic counters.");
        }

        // UBOs
        for (auto ubo : shader_resources.uniform_buffers) {
            auto desc_set = compiler.get_decoration(ubo.id, spv::DecorationDescriptorSet);
            if (filter_out_low_descriptors && desc_set < 2) continue;

            auto buffer_interface_ptr = std::unique_ptr<SPInterfaceStructuredBuffer>(new SPInterfaceStructuredBuffer{});
            buffer_interface_ptr->type = SPInterfaceBuffer::Type::UniformBuffer;
            buffer_interface_ptr->name = ubo.name;

            ReflectSimpleStruct(layout, *buffer_interface_ptr, ubo, compiler);

            buffer_interface_ptr->layout_set = desc_set;
            buffer_interface_ptr->layout_binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);

            if (layout.interface_name_mapping.contains(ubo.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", ubo.name.c_str());
            }
            layout.interface_name_mapping[ubo.name] = buffer_interface_ptr.get();
            layout.interfaces.push_back(std::move(buffer_interface_ptr));
        }

        // SSBOs
        for (auto ssbo : shader_resources.storage_buffers) {
            auto desc_set = compiler.get_decoration(ssbo.id, spv::DecorationDescriptorSet);
            if (filter_out_low_descriptors && desc_set < 2) continue;

            auto buffer_interface_ptr = std::unique_ptr<SPInterfaceBuffer>(new SPInterfaceBuffer{});
            buffer_interface_ptr->type = SPInterfaceBuffer::Type::StorageBuffer;
            buffer_interface_ptr->name = ssbo.name;

            buffer_interface_ptr->layout_set = desc_set;
            buffer_interface_ptr->layout_binding = compiler.get_decoration(ssbo.id, spv::DecorationBinding);
            // `readonly buffer` arrives as `NonWritable` on the variable, and the layout has to
            // declare the same access the shader does.
            buffer_interface_ptr->read_only =
                compiler.has_decoration(ssbo.id, spv::DecorationNonWritable)
                && !compiler.has_decoration(ssbo.id, spv::DecorationNonReadable);

            if (layout.interface_name_mapping.contains(ssbo.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", ssbo.name.c_str());
            }
            layout.interface_name_mapping[ssbo.name] = buffer_interface_ptr.get();
            layout.interfaces.push_back(std::move(buffer_interface_ptr));
        }

        // Push constant blocks: not descriptor interfaces, but the pipeline
        // layout must declare a range covering them. SPIRV-Cross folds all
        // push constant blocks of a stage into one resource.
        for (auto &pc : shader_resources.push_constant_buffers) {
            auto &type = compiler.get_type(pc.base_type_id);
            const uint32_t size = static_cast<uint32_t>(compiler.get_declared_struct_size(type));
            layout.push_constant_size = std::max(layout.push_constant_size, size);
        }

        std::sort(
            layout.interfaces.begin(),
            layout.interfaces.end(),
            [](const std::unique_ptr<SPInterface> &lhs, const std::unique_ptr<SPInterface> &rhs) -> bool {
                if (lhs->layout_set != rhs->layout_set) {
                    return lhs->layout_set < rhs->layout_set;
                }
                return lhs->layout_binding < rhs->layout_binding;
            }
        );

        return layout;
    }
} // namespace Engine::Rhi