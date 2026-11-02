#include "Rhi/Pipeline/ShaderParameterLayout.h"

#include "Rhi/Buffer/StructuredBufferPlacer.h"
#include "Rhi/Pipeline/ShaderInterfaceDescription.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace Engine::Rhi {
    namespace {
        /// @brief Owns the placers a description-built layout hands out pointers to.
        ///
        /// The same shape the reflection uses: a layout names its blocks by pointer, so whatever
        /// built it keeps them alive for as long as the layout lives. They are never removed,
        /// because a layout is copied around and nothing counts the references.
        struct DescriptionLayoutStorage {
            std::vector<std::unique_ptr<StructuredBufferPlacer>> placers;
        };
        DescriptionLayoutStorage description_layout_storage;
    } // namespace

    SPLayout SPLayout::FromDescription(
        const ShaderInterfaceDescription &description, bool filter_out_low_descriptors
    ) {
        SPLayout layout{};
        layout.push_constant_size = description.has_constant_block ? description.constant_block.size : 0;

        for (const ShaderInterfaceBinding &binding : description.bindings) {
            if (filter_out_low_descriptors && binding.set < 2) {
                continue;
            }

            const DescriptorType type = ShaderInterfaceDescription::ToDescriptorType(binding.kind);
            std::unique_ptr<SPInterface> interface{};
            switch (type) {
            case DescriptorType::UniformBuffer:
            case DescriptorType::StorageBuffer:
            case DescriptorType::StorageBufferDynamic: {
                auto buffer = std::unique_ptr<SPInterfaceStructuredBuffer>(new SPInterfaceStructuredBuffer{});
                buffer->type = type == DescriptorType::UniformBuffer ? SPInterfaceBuffer::Type::UniformBuffer
                                                                     : SPInterfaceBuffer::Type::StorageBuffer;
                buffer->read_only = binding.storage_read_only;
                if (!binding.members.empty() || binding.block_size != 0) {
                    // The block a caller places variables into. Its variables keep the names the
                    // reflection gives them - `Block::member` - so a caller written against the
                    // reflected layout works against this one.
                    auto placer = std::unique_ptr<StructuredBufferPlacer>(new StructuredBufferPlacer{});
                    for (const ShaderInterfaceBinding::Member &member : binding.members) {
                        switch (static_cast<StructuredBufferPlacer::Kind>(member.kind)) {
                        case StructuredBufferPlacer::Kind::Float:
                            placer->AddVariable<float>(member.name, member.offset);
                            break;
                        case StructuredBufferPlacer::Kind::Float4:
                            placer->AddVariable<float[4]>(member.name, member.offset);
                            break;
                        case StructuredBufferPlacer::Kind::Matrix4:
                            placer->AddVariable<float[16]>(member.name, member.offset);
                            break;
                        case StructuredBufferPlacer::Kind::Int32:
                            placer->AddVariable<int32_t>(member.name, member.offset);
                            break;
                        case StructuredBufferPlacer::Kind::UInt32:
                            placer->AddVariable<uint32_t>(member.name, member.offset);
                            break;
                        default:
                            // A kind this build does not know: placed without a type check, which
                            // is what a placer entry with no type information means.
                            placer->AddVariable(member.name, member.offset, member.size, nullptr);
                            break;
                        }
                    }
                    if (binding.block_size != 0) {
                        placer->SetBlockSize(binding.block_size);
                    }
                    description_layout_storage.placers.emplace_back(std::move(placer));
                    buffer->buffer_placer = description_layout_storage.placers.back().get();
                }
                interface = std::move(buffer);
                break;
            }
            case DescriptorType::CombinedImageSampler:
            case DescriptorType::SampledImage: {
                auto image = std::unique_ptr<SPInterfaceOpaqueImage>(new SPInterfaceOpaqueImage{});
                image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::HasSampler);
                // How the binding is declared is what the layout has to restate: a skybox is a
                // cube and a shadow map an array, and a descriptor that says plain 2D is one the
                // device refuses at the draw.
                switch (static_cast<TextureViewDimension>(binding.view_dimension)) {
                case TextureViewDimension::D1:
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::d1D);
                    break;
                case TextureViewDimension::D3:
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::d3D);
                    break;
                case TextureViewDimension::D2Array:
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::Arrayed);
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::d2D);
                    break;
                case TextureViewDimension::Cube:
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::CubeMap);
                    break;
                case TextureViewDimension::CubeArray:
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::CubeMap);
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::Arrayed);
                    break;
                default:
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::d2D);
                    break;
                }
                if (static_cast<TextureSampleType>(binding.sample_type) == TextureSampleType::Depth) {
                    image->flags.Set(SPInterfaceOpaqueImage::ImageFlagBits::Depth);
                }
                interface = std::move(image);
                break;
            }
            case DescriptorType::StorageImage: {
                auto image = std::unique_ptr<SPInterfaceOpaqueStorageImage>(new SPInterfaceOpaqueStorageImage{});
                image->format = binding.storage_format;
                image->read_only = binding.storage_read_only;
                interface = std::move(image);
                break;
            }
            case DescriptorType::Sampler: {
                auto sampler = std::unique_ptr<SPInterfaceOpaqueOther>(new SPInterfaceOpaqueOther{});
                sampler->type = SPInterfaceOpaqueOther::Type::Sampler;
                interface = std::move(sampler);
                break;
            }
            default:
                continue;
            }

            interface->name = binding.name;
            interface->layout_set = binding.set;
            interface->layout_binding = binding.binding;
            layout.interface_name_mapping[binding.name] = interface.get();
            layout.interfaces.push_back(std::move(interface));
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
