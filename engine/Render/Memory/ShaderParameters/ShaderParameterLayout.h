#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED

#include "ShaderParameterComplex.h"
#include <memory>
#include <unordered_map>

namespace vk {
    class DescriptorSetLayoutBinding;
    class DescriptorImageInfo;
    class DescriptorBufferInfo;

    enum class DescriptorType;
}

namespace Engine {
    namespace ShdrRfl {
        struct ShaderParameters;

        struct SPLayout {
            // Interfaces are guaranteed to be sorted by set and binding numbers
            std::vector <const SPInterface *> interfaces;
            std::unordered_map <std::string, const SPInterface *> interface_name_mapping;

            struct DescriptorSetWrite {
                std::vector <std::tuple<uint32_t, vk::DescriptorImageInfo, vk::DescriptorType>> image {};
                std::vector <std::tuple<uint32_t, vk::DescriptorBufferInfo, vk::DescriptorType>> buffer {};
            };

            /**
             * @brief Generate `vk::DescriptorWrite` according to
             * variables supplied.
             */
            DescriptorSetWrite GenerateDescriptorSetWrite(
                uint32_t set,
                const ShaderParameters & interfaces
            ) const noexcept;

            /**
             * @brief Place all simple variables (i.e. scalars, arrays,
             * vectors and matrices) into their corresponding buffers.
             * 
             * @param buffer a CPU buffer to be copied to GPU.
             * Will be resized if necessary.
             */
            void PlaceBufferVariable(
                std::vector <std::byte> & buffer,
                const SPInterfaceStructuredBuffer & interface,
                const ShaderParameters & arguments
            ) const noexcept;

            /**
             * @brief Generate all descriptor set layout bindings.
             * 
             * @return an unordered map from descriptor set index to its
             * corresponding binding.
             */
            std::unordered_map <uint32_t, std::vector <vk::DescriptorSetLayoutBinding>>
            GenerateAllLayoutBindings() const;

            /**
             * @brief Generate descriptor set layout binding for a given set.
             */
            std::vector <vk::DescriptorSetLayoutBinding>
            GenerateLayoutBindings(uint32_t set) const;

            /**
             * @brief Merge a SPLayout from another shader.
             * 
             * Merging is done by considering only names. If a variable
             * corresponding to a name is found out to be an interface,
             * then the interface is also merged.
             */
            void Merge(SPLayout && other);

            /**
             * @brief Acquire a SPLayout by reflecting SPIR-V code.
             * 
             * @param filter_out_low_descriptors Ignore descriptors whose set
             * is lower than 2. Useful when processing material shaders.
             */
            static SPLayout Reflect(
                const std::vector <uint32_t> & spirv_code,
                bool filter_out_low_descriptors = true
            );
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED
