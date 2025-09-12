#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED

#include <variant>
#include <cstdint>
#include <vector>
#include "ShaderParameterSimple.h"

namespace Engine {
    namespace ShdrRfl {
        struct SPType {
            virtual ~SPType(){};
        };
        /**
         * @brief A shader variable of struct.
         * 
         * This type of struct contains only assignable members.
         * No recursive struct is allowed.
         */
        struct SPTypeSimpleStruct : SPType {
            size_t expected_size {};
            std::vector <const SPAssignable *> members {};
        };

        /**
         * @brief One-dimensional array of an assignable type.
         */
        struct SPTypeSimpleArray : SPType {
            size_t array_length {~0ULL};
            SPAssignableSimple::Type type {SPAssignableSimple::Type::Unknown};
        };

        /**
         * @brief A shader parameter that occupies a descriptor slot.
         * Including opaque types and uniform or storage buffers.
         */
        struct SPInterface : SPAssignable {
            uint32_t layout_set {~0U};
            uint32_t layout_binding {~0U};
            enum Type {
                Unknown,
                // Combined image and sampler (`samplerX`)
                TextureCombinedSampler,
                // Storage image (`imageX`)
                Image,
                // Atomic counter (`atomic_X`), not available for Vulkan
                // AtomicCounter,
                // Seperate texture image (`textureX`)
                Texture,
                // Seperate sampler (`sampler`)
                Sampler,
                // UBOs (`uniform StructName`)
                UniformBuffer,
                // SSBOs (`buffer StructName`)
                StorageBuffer
            } type {Type::Unknown};

            // Underlying type for buffers.
            const SPType * underlying_type {nullptr};
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
