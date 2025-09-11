#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED

#include <variant>
#include <cstdint>
#include <vector>
#include "ShaderParameterSimple.h"

namespace Engine {
    namespace ShdrRfl {
        struct SPType {};
        /**
         * @brief A shader variable of struct.
         * 
         * This type of struct contains only simple members. No
         * arrays or recursive struct is allowed.
         */
        struct SPTypeSimpleStruct : SPType {
            size_t expected_size;
            std::vector <const SPSimpleAssignable *> members;
        };
        /**
         * @brief A shader parameter that occupies a descriptor slot.
         * Including opaque types and uniform or storage buffers.
         */
        struct SPInterface : SPAssignable {
            uint32_t layout_set;
            uint32_t layout_binding;
            enum Type {
                TextureCombinedSampler,
                Image,
                AtomicCounter,
                Texture,
                Sampler,
                UniformBuffer,
                StorageBuffer
            } type;

            // Underlying type for buffers.
            const SPType * underlying_type;
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
