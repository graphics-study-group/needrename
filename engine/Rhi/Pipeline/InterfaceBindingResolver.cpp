#include "Rhi/Pipeline/InterfaceBindingResolver.h"

#include "Rhi/Pipeline/ShaderInterface.h"

#include <cassert>

namespace Engine::Rhi {
    std::optional<InterfaceBinding> ResolveInterfaceBinding(
        const SPInterface &interface, bool enforce_dynamic_uniform, bool enforce_dynamic_storage
    ) {
        if (dynamic_cast<const SPInterfaceOpaqueImage *>(&interface)) {
            return InterfaceBinding{
                .type = vk::DescriptorType::eCombinedImageSampler,
                .image_layout = vk::ImageLayout::eReadOnlyOptimal,
                .is_image = true
            };
        }

        if (dynamic_cast<const SPInterfaceOpaqueStorageImage *>(&interface)) {
            return InterfaceBinding{
                .type = vk::DescriptorType::eStorageImage, .image_layout = vk::ImageLayout::eGeneral, .is_image = true
            };
        }

        if (auto *buffer = dynamic_cast<const SPInterfaceBuffer *>(&interface)) {
            switch (buffer->type) {
            case SPInterfaceBuffer::Type::UniformBuffer:
                return InterfaceBinding{
                    .type = enforce_dynamic_uniform ? vk::DescriptorType::eUniformBufferDynamic
                                                    : vk::DescriptorType::eUniformBuffer,
                    .image_layout = vk::ImageLayout::eReadOnlyOptimal,
                    .is_image = false
                };
            case SPInterfaceBuffer::Type::StorageBuffer:
                return InterfaceBinding{
                    .type = enforce_dynamic_storage ? vk::DescriptorType::eStorageBufferDynamic
                                                    : vk::DescriptorType::eStorageBuffer,
                    .image_layout = vk::ImageLayout::eReadOnlyOptimal,
                    .is_image = false
                };
            default:
                return std::nullopt;
            }
        }

        return std::nullopt;
    }
} // namespace Engine::Rhi
