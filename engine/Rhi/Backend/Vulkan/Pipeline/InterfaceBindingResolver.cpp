#include "Rhi/Pipeline/InterfaceBindingResolver.h"
#include "Rhi/Backend/Vulkan/VulkanHandles.h"

#include "Rhi/Pipeline/ShaderInterface.h"

#include <cassert>

namespace Engine::Rhi {
    std::optional<InterfaceBinding> ResolveInterfaceBinding(
        const SPInterface &interface, bool enforce_dynamic_uniform, bool enforce_dynamic_storage
    ) {
        if (dynamic_cast<const SPInterfaceOpaqueImage *>(&interface)) {
            return InterfaceBinding{
                .type = DescriptorType::CombinedImageSampler,
                .image_layout = ResourceState::ShaderReadOnly,
                .is_image = true
            };
        }

        if (dynamic_cast<const SPInterfaceOpaqueStorageImage *>(&interface)) {
            return InterfaceBinding{
                .type = DescriptorType::StorageImage, .image_layout = ResourceState::General, .is_image = true
            };
        }

        if (auto *buffer = dynamic_cast<const SPInterfaceBuffer *>(&interface)) {
            switch (buffer->type) {
            case SPInterfaceBuffer::Type::UniformBuffer:
                return InterfaceBinding{
                    .type = enforce_dynamic_uniform ? DescriptorType::UniformBufferDynamic
                                                    : DescriptorType::UniformBuffer,
                    .image_layout = ResourceState::ShaderReadOnly,
                    .is_image = false
                };
            case SPInterfaceBuffer::Type::StorageBuffer:
                return InterfaceBinding{
                    .type = enforce_dynamic_storage ? DescriptorType::StorageBufferDynamic
                                                    : DescriptorType::StorageBuffer,
                    .image_layout = ResourceState::ShaderReadOnly,
                    .is_image = false
                };
            default:
                return std::nullopt;
            }
        }

        return std::nullopt;
    }
} // namespace Engine::Rhi


