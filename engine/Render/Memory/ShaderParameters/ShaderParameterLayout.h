#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED

#include "ShaderParameterComplex.h"
#include <memory>
#include <unordered_map>

namespace Engine {
    namespace ShdrRfl {
        struct SPLayout {
            std::vector <std::unique_ptr<SPType>> types;
            std::vector <std::unique_ptr<SPVariable>> variables;

            std::vector <const SPInterface *> interfaces;
            std::unordered_map <std::string, const SPAssignable *> assignable_mapping;

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

            static SPLayout Reflect(const std::vector <uint32_t> & spirv_code);
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERLAYOUT_INCLUDED
