#ifndef ENGINE_RHI_INTERFACEBINDINGRESOLVER_INCLUDED
#define ENGINE_RHI_INTERFACEBINDINGRESOLVER_INCLUDED

#include "Rhi/rhi_export.h"

#include <optional>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct SPInterface;

    /**
     * @brief The descriptor binding a reflected shader interface implies.
     */
    struct RHI_API InterfaceBinding {
        /// @brief Descriptor type the interface is declared with.
        vk::DescriptorType type{vk::DescriptorType::eUniformBuffer};
        /// @brief Layout an image interface is bound in; meaningless for buffers.
        vk::ImageLayout image_layout{vk::ImageLayout::eReadOnlyOptimal};
        /// @brief Whether the interface is an image rather than a buffer.
        bool is_image{false};
    };

    /**
     * @brief Resolve the descriptor a reflected interface implies.
     *
     * A sampled image maps to a combined image sampler in `eReadOnlyOptimal`, a
     * storage image to a storage image in `eGeneral`, and a buffer to the
     * uniform or storage descriptor type its declaration and the enforced
     * dynamic-offset flags select.
     *
     * This is the single place that decision is made; both
     * `ShaderResourceBinding` and the compute kernel call it.
     *
     * @param interface The reflected interface to resolve.
     * @param enforce_dynamic_uniform Use the dynamic uniform descriptor type.
     * @param enforce_dynamic_storage Use the dynamic storage descriptor type.
     * @return The resolved binding, or `std::nullopt` for an interface kind the
     * renderer does not bind.
     */
    RHI_API std::optional<InterfaceBinding> ResolveInterfaceBinding(
        const SPInterface &interface, bool enforce_dynamic_uniform, bool enforce_dynamic_storage
    );
} // namespace Engine::Rhi

#endif // ENGINE_RHI_INTERFACEBINDINGRESOLVER_INCLUDED
