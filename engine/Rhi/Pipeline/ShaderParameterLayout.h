#ifndef ENGINE_RHI_SHADERPARAMETERLAYOUT_INCLUDED
#define ENGINE_RHI_SHADERPARAMETERLAYOUT_INCLUDED

#include "Rhi/Pipeline/ShaderInterface.h"
#include "Rhi/Pipeline/ShaderInterfaceDescription.h"
#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>
#include <unordered_map>

namespace vk {
    struct DescriptorSetLayoutBinding;
    struct DescriptorImageInfo;
    struct DescriptorBufferInfo;

    enum class DescriptorType;
} // namespace vk

namespace Engine::Rhi {
    class StructuredBuffer;

    /**
     * @brief Reflected information of shaders.
     *
     * Contains detailed information on descriptor set layouts and memory
     * layouts of uniform buffers.
     */
    struct RHI_API SPLayout {
        // Interfaces are guaranteed to be sorted by set and binding numbers
        std::vector<std::unique_ptr<SPInterface>> interfaces;
        std::unordered_map<std::string, const SPInterface *> interface_name_mapping;

        // Owns unique_ptrs, so it is move-only by design.
        SPLayout() = default;
        SPLayout(const SPLayout &) = delete;
        SPLayout &operator=(const SPLayout &) = delete;
        SPLayout(SPLayout &&) noexcept = default;
        SPLayout &operator=(SPLayout &&) noexcept = default;

        /**
         * @brief Size in bytes of the shader's push constant block.
         *
         * 0 when the shader declares no push constants. Offsets always
         * start at 0; Vulkan permits exactly one range per shader stage.
         */
        uint32_t push_constant_size = 0;

        /**
         * @brief Place all simple variables (i.e. scalars, arrays,
         * vectors and matrices) into their corresponding buffers.
         *
         * @param buffer a CPU buffer to be copied to GPU.
         * Will be resized if necessary.
         *
         * @see StructuredBufferPlacer::WriteBuffer() for actual implementation.
         */
        void PlaceBufferVariable(
            std::vector<std::byte> &rbuffer,
            const SPInterfaceStructuredBuffer &interface,
            const StructuredBuffer &sbuffer
        ) const noexcept;

        /**
         * @brief Generate all descriptor set layout bindings.
         *
         * @return an unordered map from descriptor set index to its
         * corresponding binding.
         */
        std::unordered_map<uint32_t, DescriptorSetLayout> GenerateAllLayoutBindings() const;

        /**
         * @brief Generate descriptor set layout binding for a given set.
         *
         * These bindings are sorted by binding numbers.
         */
        DescriptorSetLayout GenerateLayoutBindings(
            uint32_t set, bool enforce_dynamic_uniform_buffer = false, bool enforce_dynamic_storage_buffer = false
        ) const;

        /**
         * @brief Merge a SPLayout from another shader.
         *
         * Merging is done by considering only names. If a variable
         * corresponding to a name is found out to be an interface,
         * then the interface is also merged.
         */
        void Merge(SPLayout &&other);

        /**
         * @brief Acquire a SPLayout by reflecting SPIR-V code.
         *
         * @param filter_out_low_descriptors Ignore descriptors whose set
         * is lower than 2. Useful when processing material shaders.
         */
        static SPLayout Reflect(const std::vector<uint32_t> &spirv_code, bool filter_out_low_descriptors = true);

        /**
         * @brief Acquire a SPLayout from a baked interface description.
         *
         * A runtime with no reflection library cannot read a module, so the table it would have
         * derived comes from the bake: the bindings a set declares, and for a uniform block the
         * offset of every variable a caller may place into it. The result is the same shape
         * @ref Reflect produces, so the code that consumes a layout does not care which one it got.
         *
         * @param description The description the bake wrote for this module.
         * @param filter_out_low_descriptors Ignore descriptors whose set is lower than 2.
         */
        static SPLayout FromDescription(
            const ShaderInterfaceDescription &description, bool filter_out_low_descriptors = true
        );
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SHADERPARAMETERLAYOUT_INCLUDED


