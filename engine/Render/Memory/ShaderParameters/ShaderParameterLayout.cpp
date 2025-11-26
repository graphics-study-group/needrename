#include "ShaderParameterLayout.h"
#include "ShaderParameter.h"

// CMake is messing with the SPIRV-Cross in Vulkan SDK
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#include "../../../../third_party/SPIRV-Cross/spirv_cross.hpp"
#pragma GCC diagnostic pop
#include <vulkan/vulkan.hpp>
#include <SDL3/SDL.h>

namespace {
    Engine::ShdrRfl::SPAssignableSimple::Type GetSimpleAssignableType (
        const spirv_cross::SPIRType & type
    ) {
        using namespace Engine::ShdrRfl;
        if (type.basetype == spirv_cross::SPIRType::Struct) {
            // Recursive structure...
            assert(!"Recursive struct not supported.");
            return SPAssignableSimple::Type::Unknown;
        } 
        if (type.columns > 1) {
            // This is a matrix
            assert(type.vecsize == 4 && type.columns == 4);
            assert(type.basetype == spirv_cross::SPIRType::Float);
            return SPAssignableSimple::Type::FMat4;
        }
        if (type.vecsize > 1) {
            // This is a vector
            assert(type.vecsize == 4);
            assert(type.basetype == spirv_cross::SPIRType::Float);
            return SPAssignableSimple::Type::FVec4;
        }
        // This is a scalar
        switch(type.basetype) {
        case spirv_cross::SPIRType::Float:
            return SPAssignableSimple::Type::Float;
            break;
        case spirv_cross::SPIRType::Int:
            return SPAssignableSimple::Type::Sint;
            break;
        case spirv_cross::SPIRType::UInt:
            return SPAssignableSimple::Type::Uint;
            break;
        default:
            assert(!"Unimplemented type.");
        }
        return SPAssignableSimple::Type::Unknown;
    }

    void ReflectSimpleStruct(
        Engine::ShdrRfl::SPLayout & layout,
        Engine::ShdrRfl::SPInterfaceBuffer & root,
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

            // Get member offset within this struct.
            const size_t offset = compiler.type_struct_member_offset(type, i);
            const auto &name = compiler.get_member_name(type.self, i);
            if (!member_type.array.empty()){
                assert(member_type.array.size() == 1 && "Multi-dimensional array is unsupported");
                SPTypeSimpleArray * array_type_ptr = nullptr;
                SPTypeSimpleArray array_type;
                array_type.array_length = member_type.array.size();
                array_type.type = GetSimpleAssignableType(member_type);
                for (const auto & type : SPLayout::types) {
                    auto found_array_type_ptr = dynamic_cast<SPTypeSimpleArray *>(type.get()); 
                    if (found_array_type_ptr) {
                        if (found_array_type_ptr->array_length == array_type.array_length 
                            && found_array_type_ptr->type == array_type.type) {
                            array_type_ptr = found_array_type_ptr;
                        }
                    }
                }
                if (array_type_ptr == nullptr) {
                    auto new_unique_type_ptr = std::unique_ptr<SPTypeSimpleArray>(new SPTypeSimpleArray(array_type));
                    array_type_ptr = new_unique_type_ptr.get();
                    SPLayout::types.emplace_back(std::move(new_unique_type_ptr));
                }

                auto mem_ptr = std::unique_ptr <SPAssignableArray>(new SPAssignableArray{});
                mem_ptr->name = root.name + "::" + name;
                mem_ptr->absolute_offset = offset;
                mem_ptr->parent_interface = &root;
                mem_ptr->underlying_type = array_type_ptr;

                struct_type_ptr->members.push_back(mem_ptr.get());
                if (layout.name_mapping.contains(name)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name: %s", name.c_str());
                }
                layout.name_mapping[name] = mem_ptr.get();
                layout.variables.emplace_back(std::move(mem_ptr));
            } else {
                auto mem_ptr = std::unique_ptr <SPAssignableSimple>(new SPAssignableSimple{});
                mem_ptr->name = root.name + "::" + name;
                mem_ptr->absolute_offset = offset;
                mem_ptr->parent_interface = &root;
                mem_ptr->type = GetSimpleAssignableType(member_type);
                struct_type_ptr->members.push_back(mem_ptr.get());
                if (layout.name_mapping.contains(name)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name: %s", name.c_str());
                }
                layout.name_mapping[name] = mem_ptr.get();
                layout.variables.emplace_back(std::move(mem_ptr));
            }
            
        }
        root.underlying_type = struct_type_ptr.get();
        SPLayout::types.emplace_back(std::move(struct_type_ptr));
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

    template <typename T> requires std::is_trivially_copyable_v<T>
    bool CopyToBuffer (
        std::byte * pbuf,
        const Engine::ShdrRfl::SPAssignable * assignable,
        const Engine::ShdrRfl::ShaderParameters::ParameterVariant & v
    ) {
        auto pv = std::get_if<T>(&v);
        if (!pv) {
            SDL_LogWarn(
                SDL_LOG_CATEGORY_RENDER,
                "Skipping mistyped variable %s.",
                assignable->name.c_str()
            );
            return false;
        }
        std::memcpy(
            pbuf,
            pv,
            sizeof(T)
        );
        return true;
    };
}

namespace Engine::ShdrRfl {
    std::vector <std::unique_ptr<SPType>> Engine::ShdrRfl::SPLayout::types{};

    SPLayout::DescriptorSetWrite SPLayout::GenerateDescriptorSetWrite(
        uint32_t set, const ShaderParameters &interfaces
    ) const noexcept {
        DescriptorSetWrite write;

        for (auto pinterface : this->interfaces) {
            if (pinterface->layout_set != set) continue;

            auto itr = interfaces.interfaces.find(pinterface->name);
            if (itr == interfaces.interfaces.end()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Skipping unassigned interface %s.",
                    pinterface->name.c_str()
                );
                continue;
            }

            if (auto popaque = dynamic_cast<const SPInterfaceOpaqueImage *>(pinterface)) {
                auto pimg = std::get_if<std::shared_ptr<const Texture>>(&itr->second);
                if (pimg) {
                    assert(popaque->array_size == 0);
                    write.image.push_back(
                        std::make_tuple(
                            popaque->layout_binding,
                            vk::DescriptorImageInfo {
                                (*pimg)->GetSampler(),
                                (*pimg)->GetImageView(),
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
            } else if (auto pstorage = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(pinterface)) {
                auto pimg = std::get_if<std::shared_ptr<const Texture>>(&itr->second);
                if (pimg) {
                    assert(pstorage->array_size == 0);
                    assert((*pimg)->SupportRandomAccess());

                    write.image.push_back(
                        std::make_tuple(
                            pstorage->layout_binding,
                            vk::DescriptorImageInfo {
                                (*pimg)->GetSampler(),
                                (*pimg)->GetImageView(),
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
            } else if (auto pbuffer = dynamic_cast<const SPInterfaceBuffer *>(pinterface)) {
                if (auto pbuf = std::get_if<std::tuple<std::reference_wrapper<const Buffer>, size_t, size_t>>(&itr->second)) {
                    auto offset = std::get<1>(*pbuf);
                    auto range = std::get<2>(*pbuf) > 0 ? std::get<2>(*pbuf) : vk::WholeSize;

                    vk::DescriptorType type;
                    if (pbuffer->type == SPInterfaceBuffer::Type::StorageBuffer) {
                        type = vk::DescriptorType::eStorageBuffer;
                    } else if (pbuffer->type == SPInterfaceBuffer::Type::UniformBuffer) {
                        type = vk::DescriptorType::eUniformBuffer;
                    } 

                    write.buffer.push_back(
                        std::make_tuple(
                            pbuffer->layout_binding,
                            vk::DescriptorBufferInfo {
                                std::get<0>(*pbuf).get().GetBuffer(),
                                offset,
                                range
                            },
                            type
                        )
                    );
                } else if (auto pbuf = std::get_if<std::tuple<std::shared_ptr<const Buffer>, size_t, size_t>>(&itr->second)) {
                    auto offset = std::get<1>(*pbuf);
                    auto range = std::get<2>(*pbuf) > 0 ? std::get<2>(*pbuf) : vk::WholeSize;

                    vk::DescriptorType type;
                    if (pbuffer->type == SPInterfaceBuffer::Type::StorageBuffer) {
                        type = vk::DescriptorType::eStorageBuffer;
                    } else if (pbuffer->type == SPInterfaceBuffer::Type::UniformBuffer) {
                        type = vk::DescriptorType::eUniformBuffer;
                    } 

                    write.buffer.push_back(
                        std::make_tuple(
                            pbuffer->layout_binding,
                            vk::DescriptorBufferInfo {
                                std::get<0>(*pbuf)->GetBuffer(),
                                offset,
                                range
                            },
                            type
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
        std::vector<std::byte> &buffer, const SPInterfaceBuffer *interface, const ShaderParameters &arguments
    ) const noexcept {
        assert(interface);
        auto ptype = dynamic_cast<const SPTypeSimpleStruct *>(interface->underlying_type);
        assert(ptype);
        buffer.resize(ptype->expected_size);

        for (auto pmember : ptype->members) {
            auto itr = arguments.arguments.find(pmember->name);
            if (itr == arguments.arguments.end()) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Skipping unassigned variable %s.",
                    pmember->name.c_str()
                );
                continue;
            }
            if (auto ps = dynamic_cast<const SPAssignableSimple *>(pmember)) {
                switch(ps->type) {
                case SPAssignableSimple::Type::Uint:
                    CopyToBuffer<uint32_t>(&buffer[ps->absolute_offset], ps, itr->second);
                    break;
                case SPAssignableSimple::Type::Sint:
                    CopyToBuffer<int32_t>(&buffer[ps->absolute_offset], ps, itr->second);
                    break;
                case SPAssignableSimple::Type::Float:
                    CopyToBuffer<float>(&buffer[ps->absolute_offset], ps, itr->second);
                    break;
                case SPAssignableSimple::Type::FVec4:
                    CopyToBuffer<glm::vec4>(&buffer[ps->absolute_offset], ps, itr->second);
                    break;
                case SPAssignableSimple::Type::FMat4:
                    CopyToBuffer<glm::mat4>(&buffer[ps->absolute_offset], ps, itr->second);
                    break;
                default:
                    SDL_LogWarn(
                        SDL_LOG_CATEGORY_RENDER,
                        "Unknown type for variable %s",
                        ps->name.c_str()
                    );
                }
            } else if (auto pa = dynamic_cast<const SPAssignableArray *>(pmember)) {
                auto ptype = dynamic_cast<const SPTypeSimpleArray *>(pa->underlying_type);
                assert(ptype);

                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Ignoring array typed assignable %s.",
                    pmember->name.c_str()
                );
            } else {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Unknown shader assignable %s.",
                    pmember->name.c_str()
                );
            }
        }
    }

    std::unordered_map<uint32_t, std::vector<vk::DescriptorSetLayoutBinding>> SPLayout::
        GenerateAllLayoutBindings() const {
        std::unordered_map <uint32_t, std::vector <vk::DescriptorSetLayoutBinding>> sets;

        for (auto interface : this->interfaces) {
            if (auto ptr = dynamic_cast<const SPInterfaceBuffer *>(interface)) {
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
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueImage *>(interface)) {
                sets[ptr->layout_set].emplace_back(vk::DescriptorSetLayoutBinding{
                    ptr->layout_binding,
                    vk::DescriptorType::eCombinedImageSampler,
                    std::max(ptr->array_size, 1u),
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(interface)) {
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

        for (auto interface : this->interfaces) {
            if (auto ptr = dynamic_cast<const SPInterfaceBuffer *>(interface)) {
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
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueImage *>(interface)) {
                bindings.emplace_back(vk::DescriptorSetLayoutBinding{
                    ptr->layout_binding,
                    vk::DescriptorType::eCombinedImageSampler,
                    std::max(ptr->array_size, 1u),
                    vk::ShaderStageFlagBits::eAll,
                    {}
                });
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(interface)) {
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
        for (auto & kv : other.name_mapping) {
            const auto & name = kv.first;
            if (this->name_mapping.contains(name)) {
                SDL_LogDebug(
                    SDL_LOG_CATEGORY_RENDER,
                    "Ignoring duplicated name %s.",
                    name.c_str()
                );
                continue;
            }

            // We don't have to do this recursively for now,
            // as no recursive struct is processed,
            // and named variables are all transferred whatsoever.

            // Add an entry pointing to the variable
            this->name_mapping[name] = kv.second;
            {
                auto ptr = dynamic_cast<const SPInterface *>(kv.second);
                if (ptr) {
                    bool found = false;
                    for (auto p : this->interfaces) {
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
                    if (!found) this->interfaces.push_back(ptr);
                }
            }
            
            // Transfer ownership of the variable
            for (auto & uptr : other.variables) {
                if (uptr.get() == kv.second) {
                    this->variables.emplace_back(std::move(uptr));
                    break;
                }
            }
        }

        other.variables.clear();
        other.interfaces.clear();
        other.name_mapping.clear();
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

            layout.interfaces.push_back(ptr.get());
            if (layout.name_mapping.contains(image.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", image.name.c_str());
            }
            layout.name_mapping[image.name] = ptr.get();
            layout.variables.emplace_back(std::move(ptr));
        }

        // Storage images
        for (auto image : shader_resources.storage_images) {
            auto desc_set = compiler.get_decoration(image.id, spv::DecorationDescriptorSet);
            if (filter_out_low_descriptors && desc_set < 2) continue;

            auto ptr = std::unique_ptr<SPInterfaceOpaqueStorageImage>(new SPInterfaceOpaqueStorageImage());
            ptr->name = image.name;
            ptr->layout_set = desc_set;
            ptr->layout_binding = compiler.get_decoration(image.id, spv::DecorationBinding);

            layout.interfaces.push_back(ptr.get());
            if (layout.name_mapping.contains(image.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", image.name.c_str());
            }
            layout.name_mapping[image.name] = ptr.get();
            layout.variables.emplace_back(std::move(ptr));
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

            auto buffer_interface_ptr = std::unique_ptr<SPInterfaceBuffer>(new SPInterfaceBuffer{});
            buffer_interface_ptr->type = SPInterfaceBuffer::Type::UniformBuffer;
            buffer_interface_ptr->name = ubo.name;

            ReflectSimpleStruct(layout, *buffer_interface_ptr, ubo, compiler);

            buffer_interface_ptr->layout_set = desc_set;
            buffer_interface_ptr->layout_binding = compiler.get_decoration(ubo.id, spv::DecorationBinding);
            layout.interfaces.push_back(buffer_interface_ptr.get());
            if (layout.name_mapping.contains(ubo.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", ubo.name.c_str());
            }
            layout.name_mapping[ubo.name] = buffer_interface_ptr.get();
            layout.variables.emplace_back(std::move(buffer_interface_ptr));
        }

        // SSBOs
        for (auto ssbo : shader_resources.storage_buffers) {
            auto desc_set = compiler.get_decoration(ssbo.id, spv::DecorationDescriptorSet);
            if (filter_out_low_descriptors && desc_set < 2) continue;

            auto buffer_interface_ptr = std::unique_ptr<SPInterfaceBuffer>(new SPInterfaceBuffer{});
            buffer_interface_ptr->type = SPInterfaceBuffer::Type::StorageBuffer;
            buffer_interface_ptr->name = ssbo.name;

            ReflectSimpleStruct(layout, *buffer_interface_ptr, ssbo, compiler);

            buffer_interface_ptr->layout_set = desc_set;
            buffer_interface_ptr->layout_binding = compiler.get_decoration(ssbo.id, spv::DecorationBinding);
            layout.interfaces.push_back(buffer_interface_ptr.get());
            if (layout.name_mapping.contains(ssbo.name)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated resource name: %s", ssbo.name.c_str());
            }
            layout.name_mapping[ssbo.name] = buffer_interface_ptr.get();
            layout.variables.emplace_back(std::move(buffer_interface_ptr));
        }

        std::sort(
            layout.interfaces.begin(),
            layout.interfaces.end(),
            [](const SPInterface * lhs, const SPInterface * rhs) -> bool {
                if (lhs->layout_set != rhs->layout_set) {
                    return lhs->layout_set < rhs->layout_set;
                }
                return lhs->layout_binding < rhs->layout_binding;
            }
        );
        

        return layout;
    }
}
