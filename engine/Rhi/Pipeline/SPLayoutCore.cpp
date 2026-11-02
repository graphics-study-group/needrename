#include "Rhi/Pipeline/ShaderParameterLayout.h"

#include "Rhi/Buffer/StructuredBufferPlacer.h"
#include "Rhi/Texture/ImageUtils.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// The parts of SPLayout that are not reflection.
//
// Merging the layouts of a shader's stages, restating them as descriptor set layouts and placing a
// variable into a buffer are arithmetic on the table: they need the table to exist, not to have
// been parsed here. Reflection is what fills it, and that is the part a backend without SPIRV-Cross
// cannot do. This unit is compiled everywhere, so a layout built from a baked description is
// merged, restated and written exactly the way a reflected one is.

namespace Engine::Rhi {
    Engine::Rhi::DescriptorBinding MakeStorageImageBinding(const Engine::Rhi::SPInterfaceOpaqueStorageImage &image) {
        using namespace Engine::Rhi;
        DescriptorBinding binding{};
        binding.binding = image.layout_binding;
        binding.type = DescriptorType::StorageImage;
        binding.count = static_cast<uint32_t>(std::max(image.array_size, 1u));
        binding.storage_format = image.format;
        binding.storage_read_only = image.read_only;
        return binding;
    }

    Engine::Rhi::DescriptorBinding MakeImageBinding(
        const Engine::Rhi::SPInterfaceOpaqueImage &image, Engine::Rhi::DescriptorType type
    ) {
        using namespace Engine::Rhi;
        using enum SPInterfaceOpaqueImage::ImageFlagBits;

        DescriptorBinding binding{};
        binding.binding = image.layout_binding;
        binding.type = type;
        binding.count = static_cast<uint32_t>(std::max(image.array_size, 1u));
        if (image.flags.Test(Depth)) {
            binding.sample_type = TextureSampleType::Depth;
        }
        if (image.flags.Test(CubeMap)) {
            // A cube map is arrayed in Vulkan terms - six faces - so the array flag alone does
            // not make it an array *of* cubes on an interface that has both dimensions. Only a
            // texture holding more than one cube is the array form.
            binding.view_dimension =
                image.array_size > 1 ? TextureViewDimension::CubeArray : TextureViewDimension::Cube;
        } else if (image.flags.Test(d3D)) {
            binding.view_dimension = TextureViewDimension::D3;
        } else if (image.flags.Test(d1D)) {
            binding.view_dimension = TextureViewDimension::D1;
        } else {
            binding.view_dimension =
                image.flags.Test(Arrayed) ? TextureViewDimension::D2Array : TextureViewDimension::D2;
        }
        return binding;
    }

    void SPLayout::PlaceBufferVariable(
        std::vector<std::byte> &buffer, const SPInterfaceStructuredBuffer &interface, const StructuredBuffer &sb
    ) const noexcept {
        assert(interface.buffer_placer);
        interface.buffer_placer->WriteBuffer(sb, buffer);
    }

    std::unordered_map<uint32_t, DescriptorSetLayout> SPLayout::GenerateAllLayoutBindings() const {
        std::unordered_map<uint32_t, DescriptorSetLayout> sets;

        for (const auto &interface : this->interfaces) {
            if (auto ptr = dynamic_cast<const SPInterfaceBuffer *>(interface.get())) {
                if (ptr->type == SPInterfaceBuffer::Type::UniformBuffer) {
                    sets[ptr->layout_set].push_back(
                        DescriptorBinding{ptr->layout_binding, DescriptorType::UniformBuffer, 1}
                    );
                } else if (ptr->type == SPInterfaceBuffer::Type::StorageBuffer) {
                    DescriptorBinding entry{ptr->layout_binding, DescriptorType::StorageBuffer, 1};
                    entry.storage_read_only = ptr->read_only;
                    sets[ptr->layout_set].push_back(entry);
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring buffer with unknown type.");
                }
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueImage *>(interface.get())) {
                sets[ptr->layout_set].push_back(MakeImageBinding(*ptr, DescriptorType::CombinedImageSampler));
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(interface.get())) {
                sets[ptr->layout_set].push_back(MakeStorageImageBinding(*ptr));
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring interface with unknown type.");
            }
        }

        return sets;
    }

    DescriptorSetLayout SPLayout::GenerateLayoutBindings(
        uint32_t set, bool enforce_dynamic_uniform_buffer, bool enforce_dynamic_storage_buffer
    ) const {
        DescriptorSetLayout bindings;
        for (const auto &interface : this->interfaces) {
            if (auto ptr = dynamic_cast<const SPInterfaceBuffer *>(interface.get())) {
                if (ptr->layout_set != set) continue;

                if (ptr->type == SPInterfaceBuffer::Type::UniformBuffer) {
                    bindings.push_back(
                        DescriptorBinding{
                            ptr->layout_binding,
                            enforce_dynamic_uniform_buffer ? DescriptorType::UniformBufferDynamic
                                                           : DescriptorType::UniformBuffer,
                            1
                        }
                    );
                } else if (ptr->type == SPInterfaceBuffer::Type::StorageBuffer) {
                    DescriptorBinding entry{
                        ptr->layout_binding,
                        enforce_dynamic_storage_buffer ? DescriptorType::StorageBufferDynamic
                                                       : DescriptorType::StorageBuffer,
                        1
                    };
                    entry.storage_read_only = ptr->read_only;
                    bindings.push_back(entry);
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring buffer with unknown type.");
                }
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueImage *>(interface.get())) {
                bindings.push_back(MakeImageBinding(*ptr, DescriptorType::CombinedImageSampler));
            } else if (auto ptr = dynamic_cast<const SPInterfaceOpaqueStorageImage *>(interface.get())) {
                bindings.push_back(MakeStorageImageBinding(*ptr));
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Ignoring interface with unknown type.");
            }
        }

        std::sort(
            bindings.begin(),
            bindings.end(),
            [](const DescriptorBinding &lhs, const DescriptorBinding &rhs) -> bool { return lhs.binding < rhs.binding; }
        );
        return bindings;
    }

    void SPLayout::Merge(SPLayout &&other) {
        for (auto &kv : other.interface_name_mapping) {
            const auto &name = kv.first;
            if (this->interface_name_mapping.contains(name)) {
                SDL_LogDebug(SDL_LOG_CATEGORY_RENDER, "Ignoring duplicated name %s.", name.c_str());
                continue;
            }

            // Add an entry pointing to the variable
            this->interface_name_mapping[name] = kv.second;
            {
                auto ptr = dynamic_cast<const SPInterface *>(kv.second);
                if (ptr) {
                    bool found = false;
                    for (const auto &p : this->interfaces) {
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
                            [ptr](const std::unique_ptr<SPInterface> &p) { return p.get() == ptr; }
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
} // namespace Engine::Rhi
