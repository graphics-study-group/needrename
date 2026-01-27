#include "ShaderParameterLayout.h"
#include "ShaderParameter.h"

#include "../StructuredBuffer.h"
#include "../StructuredBufferPlacer.h"

// CMake is messing with the SPIRV-Cross in Vulkan SDK
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "../../../../third_party/SPIRV-Cross/spirv_cross.hpp"
#pragma GCC diagnostic pop
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>

namespace Engine::ShdrRfl {
    // Types are only added and never removed.
    // We will have no more than ~100 shaders, so this
    // might be Ok anyway.
    struct TypeStorage {
        std::vector <std::unique_ptr<StructuredBufferPlacer>> structured_buffers;
    };
    static TypeStorage type_storage;
}

namespace {
    void ReflectSimpleStruct(
        Engine::ShdrRfl::SPLayout & layout,
        Engine::ShdrRfl::SPInterfaceStructuredBuffer & root,
        const spirv_cross::Resource & buffer,
        const spirv_cross::Compiler & compiler
    ) {
        using namespace Engine;
        using namespace Engine::ShdrRfl;
        // Construct the underlying type
        auto placer = std::unique_ptr<StructuredBufferPlacer>(new StructuredBufferPlacer{});
        auto &type = compiler.get_type(buffer.base_type_id);

        unsigned member_count = type.member_types.size();

        // For every member
        for (unsigned i = 0; i < member_count; i++)
        {
            auto &member_type = compiler.get_type(type.member_types[i]);
            assert(
                (member_type.basetype == spirv_cross::SPIRType::Float 
                || member_type.basetype == spirv_cross::SPIRType::Int 
                || member_type.basetype == spirv_cross::SPIRType::UInt
                ) && "Unsupported member type."
            );

            // Get member offset within this struct.
            const size_t offset = compiler.type_struct_member_offset(type, i);
            const auto &name = compiler.get_member_name(type.self, i);

            // Simple array
            if (!member_type.array.empty()){
                assert(!"Support for array variables is unimplemented.");
            } else {
                const auto qualified_name = root.name + "::" + name;
                if (member_type.basetype == spirv_cross::SPIRType::Struct) {
                    // Recursive structure...
                    assert(!"Recursive struct not supported.");
                } else if (member_type.columns > 1) {
                    // This is a matrix
                    assert(member_type.vecsize == 4 && member_type.columns == 4);
                    assert(member_type.basetype == spirv_cross::SPIRType::Float);
                    placer->AddVariable<float[16]>(qualified_name, offset);
                } else if (member_type.vecsize > 1) {
                    // This is a vector
                    assert(member_type.vecsize == 4);
                    assert(member_type.basetype == spirv_cross::SPIRType::Float);
                    placer->AddVariable<float[4]>(qualified_name, offset);
                } else {
                    // This is a scalar
                    switch(member_type.basetype) {
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
            
        }
        root.buffer_placer = placer.get();
        type_storage.structured_buffers.emplace_back(std::move(placer));
    }

    void FillImageInfo(
        Engine::ShdrRfl::SPInterfaceOpaqueImage & image, 
        const spirv_cross::SPIRType & type
    ) {
        if (!type.array.empty()) {
            assert(type.array.size() == 1);
            image.array_size = type.array[0];
        } else {
            image.array_size = 0;
        }

        using enum Engine::ShdrRfl::SPInterfaceOpaqueImage::ImageFlagBits;
        if (type.image.arrayed) image.flags.Set(Arrayed);
        if (type.image.ms) image.flags.Set(Multisampled);
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
}

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
namespace Engine::ShdrRfl {

    SPLayout::DescriptorSetWrite SPLayout::GenerateDescriptorSetWrite(
        uint32_t set, const ShaderParameters &interfaces
    ) const noexcept {
        DescriptorSetWrite write;

        for (const auto & pinterface : this->interfaces) {
            if (pinterface->layout_set != set) continue;
            auto & intfc = interfaces.GetInterfaces();
            auto itr = intfc.find(pinterface->name);
            if (itr == intfc.end()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Skipping unassigned interface %s.",
                    pinterface->name.c_str()
                );
                continue;
            }

            if (auto popaque = dynamic_cast<const SPInterfaceOpaqueImage *>(pinterface.get())) {
                auto pimg = std::get_if<std::reference_wrapper<const Texture>>(&itr->second);
                if (pimg) {
                    assert(popaque->array_size == 0);
                    write.image.push_back(
                        std::make_tuple(
                            popaque->layout_binding,
                            vk::DescriptorImageInfo {
                                pimg->get().GetSampler(),
                                pimg->get().GetImageView(),
                                vk::ImageLayout::eReadOnlyOptimal
                            },
                            vk::DescriptorType::eCombinedImageSampler
                        )
                    );
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Interface %s is not assigned to an image.",
                        pinterface->name.c_str()
                    );
                }
            } else if (auto pstorage = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(pinterface.get())) {
                auto pimg = std::get_if<std::reference_wrapper<const Texture>>(&itr->second);
                if (pimg) {
                    assert(pstorage->array_size == 0);
                    assert((pimg)->get().SupportRandomAccess());

                    write.image.push_back(
                        std::make_tuple(
                            pstorage->layout_binding,
                            vk::DescriptorImageInfo {
                                pimg->get().GetSampler(),
                                pimg->get().GetImageView(),
                                vk::ImageLayout::eGeneral
                            },
                            vk::DescriptorType::eStorageImage
                        )
                    );
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Interface %s is not assigned to an image.",
                        pinterface->name.c_str()
                    );
                }
            } 
            // The interface is a buffer
            else if (auto pbuffer = dynamic_cast<const SPInterfaceBuffer *>(pinterface.get())) {
                if (auto pbuf = std::get_if<std::tuple<std::reference_wrapper<const DeviceBuffer>, size_t, size_t>>(&itr->second)) {
                    auto desctp = 
                        pbuffer->type == SPInterfaceBuffer::Type::StorageBuffer ?
                        vk::DescriptorType::eStorageBuffer : vk::DescriptorType::eUniformBuffer;
                    auto [buffer, offset, range] = *pbuf;
                    if (desctp == vk::DescriptorType::eStorageBuffer) {
                        if (!buffer.get().GetType().Test(BufferTypeBits::ShaderWrite)) {
                            SDL_LogWarn(
                                SDL_LOG_CATEGORY_RENDER,
                                "Interface %s is not assigned to a storage buffer.",
                                pinterface->name.c_str()
                            );
                            continue;
                        }
                    } else if (desctp == vk::DescriptorType::eUniformBuffer) {
                        if (!buffer.get().GetType().Test(BufferTypeBits::ShaderReadOnly)) {
                            SDL_LogWarn(
                                SDL_LOG_CATEGORY_RENDER,
                                "Interface %s is not assigned to a uniform buffer.",
                                pinterface->name.c_str()
                            );
                            continue;
                        }
                    }

                    write.buffer.push_back(
                        std::make_tuple(
                            pbuffer->layout_binding,
                            vk::DescriptorBufferInfo {
                                std::get<0>(*pbuf).get().GetBuffer(),
                                offset,
                                range == 0 ? vk::WholeSize : range
                            },
                            desctp
                        )
                    );
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Interface %s is not assigned to a buffer nor image.",
                        pinterface->name.c_str()
                    );
                }
            }
        }

        return write;
    }

    void SPLayout::PlaceBufferVariable(
        std::vector<std::byte> &buffer, const SPInterfaceStructuredBuffer & interface, const ShaderParameters &arguments
    ) const noexcept {
        assert(interface.buffer_placer);
        interface.buffer_placer->WriteBuffer(arguments.GetStructuredBuffer(), buffer);
    }

    std::unordered_map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> SPLayout::
        GenerateAllLayoutBindings() const {
        std::unordered_map <uint32_t, std::vector <vk::DescriptorSetLayoutBinding>> sets;

        for (const auto & interface : this->interfaces) {
            if (auto ptr = dynamic_cast<const SPInterfaceBuffer *>(interface.get())) {
                if (ptr->type == SPInterfaceBuffer::Type::UniformBuffer) {
                    sets[ptr->layout_set].emplace_back(vk::DescriptorSetLayoutBinding{
                        ptr->layout_binding,
                        vk::DescriptorType::eUniformBuffer,
                        1,
                        vk::ShaderStageFlagBits::eAll,
                        {}
                    });
                } else if (ptr->type == SPInterfaceBuffer::Type::StorageBuffer) {
                    sets[ptr->layout_set].emplace_back(vk::DescriptorSetLayoutBinding{
                        ptr->layout_binding,
                        vk::DescriptorType::eStorageBuffer,
                        1,
                        vk::ShaderStageFlagBits::eAll,
                        {}
                    });
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Ignoring buffer with unknown type."
                    );
                }
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueImage *>(interface.get())) {
                sets[ptr->layout_set].emplace_back(vk::DescriptorSetLayoutBinding{
                    ptr->layout_binding,
                    vk::DescriptorType::eCombinedImageSampler,
                    std::max(ptr->array_size, 1u),
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(interface.get())) {
                sets[ptr->layout_set].emplace_back(vk::DescriptorSetLayoutBinding{
                    ptr->layout_binding,
                    vk::DescriptorType::eStorageImage,
                    std::max(ptr->array_size, 1u),
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Ignoring interface with unknown type."
                );
            }
        }

        return sets;
    }

    std::vector<vk::DescriptorSetLayoutBinding> SPLayout::GenerateLayoutBindings(uint32_t set) const {
        std::vector <vk::DescriptorSetLayoutBinding> bindings;

        for (const auto & interface : this->interfaces) {
            if (auto ptr = dynamic_cast<const SPInterfaceBuffer *>(interface.get())) {
                if (ptr->layout_set != set) continue;

                if (ptr->type == SPInterfaceBuffer::Type::UniformBuffer) {
                    bindings.emplace_back(vk::DescriptorSetLayoutBinding{
                        ptr->layout_binding,
                        vk::DescriptorType::eUniformBuffer,
                        1,
                        vk::ShaderStageFlagBits::eAll,
                        {}
                    });
                } else if (ptr->type == SPInterfaceBuffer::Type::StorageBuffer) {
                    bindings.emplace_back(vk::DescriptorSetLayoutBinding{
                        ptr->layout_binding,
                        vk::DescriptorType::eStorageBuffer,
                        1,
                        vk::ShaderStageFlagBits::eAll,
                        {}
                    });
                } else {
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Ignoring buffer with unknown type."
                    );
                }
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueImage *>(interface.get())) {
                bindings.emplace_back(vk::DescriptorSetLayoutBinding{
                    ptr->layout_binding,
                    vk::DescriptorType::eCombinedImageSampler,
                    std::max(ptr->array_size, 1u),
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(interface.get())) {
                bindings.emplace_back(vk::DescriptorSetLayoutBinding{
                    ptr->layout_binding,
                    vk::DescriptorType::eStorageImage,
                    std::max(ptr->array_size, 1u),
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Ignoring interface with unknown type."
                );
            }
        }

        return bindings;
    }

    void SPLayout::Merge(SPLayout &&other) {
        for (auto & kv : other.interface_name_mapping) {
            const auto & name = kv.first;
            if (this->interface_name_mapping.contains(name)) {
                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    "Ignoring duplicated name %s.",
                    name.c_str()
                );
                continue;
            }

            // Add an entry pointing to the variable
            this->interface_name_mapping[name] = kv.second;
            {
                auto ptr = dynamic_cast<const SPInterface *>(kv.second);
                if (ptr) {
                    bool found = false;
                    for (const auto & p : this->interfaces) {
                        if (p->layout_set == ptr->layout_set && p->layout_binding == ptr->layout_binding) {
                            SDL_LogWarn(
                                SDL_LOG_CATEGORY_RENDER,
                                "Interface %s occupies descriptor set %u binding %u that is already assigned.",
                                name.c_str(),
                                ptr->layout_set,
                                ptr->layout_binding
                            );
                            found = true;
                        }
                    }
                    if (!found) {
                        auto other_ptr = std::find_if(
                            other.interfaces.begin(),
                            other.interfaces.end(),
                            [ptr] (const std::unique_ptr <SPInterface> & p) {
                                return p.get() == ptr;
                            }
                        );
                        assert(other_ptr != other.interfaces.end());
                        this->interfaces.push_back(std::move(*other_ptr));
                    }
                }
            }
            
        }

        other.interfaces.clear();
        other.interface_name_mapping.clear();
    }

    SPLayout Engine::ShdrRfl::SPLayout::Reflect(
        const std::vector<uint32_t> &spirv_code, bool filter_out_low_descriptors
    ) {
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
            
            if (layout.interface_name_mapping.contains(ssbo.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", ssbo.name.c_str());
            }
            layout.interface_name_mapping[ssbo.name] = buffer_interface_ptr.get();
            layout.interfaces.push_back(std::move(buffer_interface_ptr));
        }

        std::sort(
            layout.interfaces.begin(),
            layout.interfaces.end(),
            [](const std::unique_ptr <SPInterface> & lhs, const std::unique_ptr <SPInterface> & rhs) -> bool {
                if (lhs->layout_set != rhs->layout_set) {
                    return lhs->layout_set < rhs->layout_set;
                }
                return lhs->layout_binding < rhs->layout_binding;
            }
        );

        return layout;
    }
}
