#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED

#include <variant>
#include <cstdint>
#include <vector>
#include "Core/flagbits.h"
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
        };

        struct SPInterfaceOpaque : SPInterface {
            uint32_t array_size {0};
        };

        struct SPInterfaceOpaqueImage : SPInterfaceOpaque {
            enum class ImageFlagBits : uint32_t {
                HasSampler = 1 << 0,
                Arrayed = 1 << 1,
                Multisampled = 1 << 2,
                d1D = 1 << 3,
                d2D = 1 << 4,
                d3D = 1 << 5,
                CubeMap = 1 << 6
            };
            using ImageFlags = Flags <ImageFlagBits>;
            ImageFlags flags{};
        };

        struct SPInterfaceOpaqueStorageImage : SPInterfaceOpaque {
            // vk::Format format;
        };

        struct SPInterfaceOpaqueOther : SPInterfaceOpaque {
            enum class Type {
                Unknown,
                // Separate sampler (`sampler`)
                Sampler
            } type {Type::Unknown};
        };

        struct SPInterfaceBuffer : SPInterface {
            enum class Type {
                Unknown,
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
