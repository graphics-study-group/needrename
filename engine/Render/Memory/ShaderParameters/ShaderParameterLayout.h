#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED

#include "ShaderParameterComplex.h"
#include <memory>
#include <unordered_map>

namespace Engine {
    namespace ShdrRfl {
        struct SPLayout {
            // Types are only added and never removed.
            // We will have no more than ~100 shaders, so this
            // might be Ok anyway.
            static std::vector <std::unique_ptr<SPType>> types;
            std::vector <std::unique_ptr<SPVariable>> variables;

            std::vector <const SPInterface *> interfaces;
            std::unordered_map <std::string, const SPVariable *> name_mapping;

            /**
             * @brief Generate `vk::DescriptorWrite` according to
             * variables supplied.
             */
            void GenerateDescriptorWrite(
                const void * variables
            ) const noexcept;

            /**
             * @brief Place all simple variables (i.e. scalars, arrays,
             * vectors and matrices) into their corresponding buffers.
             */
            void PlaceBufferVariable(
                const void * variables
            ) const noexcept;

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
             */
            static SPLayout Reflect(
                const std::vector <uint32_t> & spirv_code,
                bool filter_out_low_descriptors = true
            );
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED
